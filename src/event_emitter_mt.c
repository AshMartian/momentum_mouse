#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/uinput.h>
#include <sys/time.h>
#include <X11/Xlib.h>
#include <math.h>
#include "momentum_mouse.h"

// External references to inertia logic variables
extern double current_position;
extern double current_velocity;

// We'll store the uinput file descriptor for multitouch events here.
static int uinput_mt_fd = -1;
static int touch_active = 0;  // Track if touch is currently active
static struct timeval last_gesture_end_time = {0, 0};
static const int MIN_GESTURE_INTERVAL_MS = 50; // Reduced minimum time between gestures
BoundaryResetInfo boundary_reset_info = {{0, 0}, 0.0, 0.0, 0};
int boundary_reset_in_progress = 0;
struct timeval last_boundary_reset_time = {0, 0};

// Screen dimensions - defaults that will be updated by detection
int screen_width = 1920;  // Default fallback width
int screen_height = 1080; // Default fallback height

// Global state for finger positions (initial positions)
static int finger0_x;  // left of center
static int finger0_y;
static int finger1_x;  // right of center
static int finger1_y;

// Detect the screen size using basic X11
static void detect_screen_size(void) {
    Display *display = XOpenDisplay(NULL);
    if (!display) {
        if (debug_mode) {
            printf("Could not open X display, using default screen size: %dx%d\n", 
                   screen_width, screen_height);
        }
        return;
    }
    
    int screen = DefaultScreen(display);
    
    // Get the screen dimensions using basic Xlib functions
    screen_width = DisplayWidth(display, screen) * 10;
    screen_height = DisplayHeight(display, screen) * 10;
    
    XCloseDisplay(display);
    
    if (debug_mode) {
        printf("Detected screen size: %dx%d\n", screen_width, screen_height);
    }
}

// Set up a virtual multitouch device.
// Reset finger positions based on screen size
static void reset_finger_positions(void) {
    finger0_x = screen_width / 2 - 50;  // left of center
    finger0_y = screen_height / 2;
    finger1_x = screen_width / 2 + 50;  // right of center
    finger1_y = screen_height / 2;
}

int setup_virtual_multitouch_device(void) {
    // Detect screen size first
    detect_screen_size();
    // Initialize finger positions based on screen size
    reset_finger_positions();
    
    uinput_mt_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (uinput_mt_fd < 0) {
        perror("Error opening /dev/uinput for multitouch");
        return -1;
    }
    
    // Enable event types
    if (ioctl(uinput_mt_fd, UI_SET_EVBIT, EV_ABS) < 0) { perror("Error setting EV_ABS"); return -1; }
    if (ioctl(uinput_mt_fd, UI_SET_EVBIT, EV_KEY) < 0) { perror("Error setting EV_KEY"); return -1; }
    if (ioctl(uinput_mt_fd, UI_SET_EVBIT, EV_SYN) < 0) { perror("Error setting EV_SYN"); return -1; }
    
    // Enable multitouch capabilities
    if (ioctl(uinput_mt_fd, UI_SET_ABSBIT, ABS_MT_SLOT) < 0) { perror("Error setting ABS_MT_SLOT"); return -1; }
    if (ioctl(uinput_mt_fd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID) < 0) { perror("Error setting ABS_MT_TRACKING_ID"); return -1; }
    if (ioctl(uinput_mt_fd, UI_SET_ABSBIT, ABS_MT_POSITION_X) < 0) { perror("Error setting ABS_MT_POSITION_X"); return -1; }
    if (ioctl(uinput_mt_fd, UI_SET_ABSBIT, ABS_MT_POSITION_Y) < 0) { perror("Error setting ABS_MT_POSITION_Y"); return -1; }
    
    // Enable touch buttons
    if (ioctl(uinput_mt_fd, UI_SET_KEYBIT, BTN_TOUCH) < 0) { perror("Error setting BTN_TOUCH"); return -1; }
    if (ioctl(uinput_mt_fd, UI_SET_KEYBIT, BTN_TOOL_FINGER) < 0) { perror("Error setting BTN_TOOL_FINGER"); return -1; }
    if (ioctl(uinput_mt_fd, UI_SET_KEYBIT, BTN_TOOL_DOUBLETAP) < 0) { perror("Error setting BTN_TOOL_DOUBLETAP"); return -1; }
    
    // Configure the virtual device
    struct uinput_user_dev uidev;
    memset(&uidev, 0, sizeof(uidev));
    snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "momentum mouse Touchpad");
    uidev.id.bustype = BUS_USB;
    uidev.id.vendor  = 0x1234;
    uidev.id.product = 0x5678;
    uidev.id.version = 1;
    
    // Set absolute axis ranges - use detected screen size
    uidev.absmin[ABS_MT_POSITION_X] = 0;
    uidev.absmax[ABS_MT_POSITION_X] = screen_width;
    uidev.absmin[ABS_MT_POSITION_Y] = 0;
    uidev.absmax[ABS_MT_POSITION_Y] = screen_height;
    uidev.absmin[ABS_MT_SLOT] = 0;
    uidev.absmax[ABS_MT_SLOT] = 1;
    
    if (write(uinput_mt_fd, &uidev, sizeof(uidev)) < 0) {
        perror("Error writing multitouch uinput device");
        return -1;
    }
    if (ioctl(uinput_mt_fd, UI_DEV_CREATE) < 0) {
        perror("Error creating multitouch uinput device");
        return -1;
    }
    
    if (debug_mode) {
        printf("Virtual multitouch device created successfully\n");
    }
    return 0;
}
// Forward declaration of the reset function
static void reset_finger_positions(void);

// Helper to get time difference in seconds between two timeval structs.
static double time_diff_in_seconds(struct timeval *start, struct timeval *end) {
    double seconds = (double)(end->tv_sec - start->tv_sec);
    double microseconds = (double)(end->tv_usec - start->tv_usec) / 1000000.0;
    return seconds + microseconds;
}

// Helper function to write an event and check for errors
static int write_event_mt(struct input_event *ev, const char *error_msg, int *ending_flag) {
    if (write(uinput_mt_fd, ev, sizeof(*ev)) < 0) {
        if (debug_mode) {
            perror(error_msg);
        }
        if (ending_flag) {
            *ending_flag = 0;
        }
        return -1;
    }
    return 0;
}

// Flag to track post-boundary transition frames
int post_boundary_frames = 0;


// Helper function to handle boundary reset logic for both vertical and horizontal scrolling
static int handle_boundary_reset(int new_pos, int delta, int screen_size, int is_vertical) {
    struct timeval now;
    gettimeofday(&now, NULL);
    static int reset_count = 0;     // Count consecutive resets
    static struct timeval last_reset_time = {0, 0};
    
    // Calculate time since last reset
    double time_since_reset = 0.0;
    if (last_reset_time.tv_sec != 0) {
        time_since_reset = (now.tv_sec - last_reset_time.tv_sec) + 
                           (now.tv_usec - last_reset_time.tv_usec) / 1000000.0;
    }
    
    // Reset the consecutive reset counter if it's been a while
    if (time_since_reset > 0.5) {  // 500ms
        reset_count = 0;
    }
    
    // Calculate boundary buffer based on velocity
    double abs_velocity = fabs(current_velocity);
    int boundary_buffer = (int)(abs_velocity * 0.2);  // 20% of velocity as buffer
    
    // Check if we've hit a boundary
    if ((new_pos >= (screen_size - boundary_buffer) && delta > 0) || 
        (new_pos <= boundary_buffer && delta < 0) || 
        new_pos < 0) {
        
        reset_count++;
        last_reset_time = now;
        
        if (debug_mode) {
            printf("BOUNDARY: Hit %s boundary at %d, delta=%d, resetting to %s (reset #%d in %.2fs, buffer=%d)\n", 
                is_vertical ? "vertical" : "horizontal", new_pos, delta, 
                (delta > 0) ? (is_vertical ? "top" : "left") : (is_vertical ? "bottom" : "right"), 
                reset_count, time_since_reset, boundary_buffer);
            printf("BOUNDARY: Current velocity before reset: %.2f\n", current_velocity);
            printf("BOUNDARY: Current position before reset: %.2f\n", current_position);
            printf("BOUNDARY: Ending touch gesture before reset\n");
        }
        
        // End the current touch gesture
        end_multitouch_gesture();
        
        // Skip the delay measurement and compensation - it's adding overhead
        // Just set a fixed boost factor
        double delay_compensation = 1.2;  // 20% boost
        double old_velocity = current_velocity;
        current_velocity *= delay_compensation;
        
        if (debug_mode) {
            printf("BOUNDARY: Applied fixed 20%% velocity boost at boundary: %.2f -> %.2f\n", 
                   old_velocity, current_velocity);
        }
        
        // Adjust velocity - reduce it but keep direction
        double direction = (current_velocity > 0) ? 1.0 : -1.0;
        double abs_velocity = fabs(current_velocity);
        
        current_velocity = direction * abs_velocity;
        
        // Set position to a small value in the right direction
        current_position = (current_velocity > 0) ? 10.0 : -10.0;
        
        // Set boundary reset flags
        boundary_reset_in_progress = 1;
        gettimeofday(&last_boundary_reset_time, NULL);
        
        // Store reset info
        boundary_reset_info.reset_time = now;
        boundary_reset_info.reset_velocity = current_velocity;
        boundary_reset_info.reset_position = current_position;
        boundary_reset_info.reset_direction = (delta > 0) ? 1 : -1;
        
        // Skip post-boundary transition
        post_boundary_frames = 0;
        
        // Check if velocity is still significant enough to continue scrolling
        if (fabs(current_velocity) < INERTIA_STOP_THRESHOLD) {
            if (debug_mode) {
                printf("BOUNDARY: Velocity too low after reset (%.2f), stopping inertia\n", current_velocity);
            }
            stop_inertia();
        }
        
        return 1; // Boundary reset occurred
    }
    
    return 0; // No boundary reset
}

// This function updates finger positions by adding the delta
// and sends out updated multitouch events.
int emit_two_finger_scroll_event(int delta) {
    struct input_event ev;
    static int boundary_reset = 0;  // Flag to track if we just did a boundary reset
    
    // Calculate new positions based on scroll axis
    int new_finger0_pos, new_finger1_pos;
    int screen_size;
    int *finger0_pos, *finger1_pos;
    
    if (scroll_axis == SCROLL_AXIS_VERTICAL) {
        // Vertical scrolling - update Y positions
        finger0_pos = &finger0_y;
        finger1_pos = &finger1_y;
        screen_size = screen_height;
    } else {
        // Horizontal scrolling - update X positions
        finger0_pos = &finger0_x;
        finger1_pos = &finger1_x;
        screen_size = screen_width;
    }
    
    new_finger0_pos = *finger0_pos + delta;
    new_finger1_pos = *finger1_pos + delta;
    
    // Log detailed position information before boundary check
    if (debug_mode) {
        printf("PRE-CHECK: finger0_%s=%d, delta=%d, new_finger0_%s=%d, velocity=%.2f, position=%.2f\n",
               (scroll_axis == SCROLL_AXIS_VERTICAL) ? "y" : "x", 
               *finger0_pos, delta, 
               (scroll_axis == SCROLL_AXIS_VERTICAL) ? "y" : "x", 
               new_finger0_pos, current_velocity, current_position);
    }
    
    // Check for boundary conditions and handle reset if needed
    if (handle_boundary_reset(new_finger0_pos, delta, screen_size, scroll_axis == SCROLL_AXIS_VERTICAL)) {
        // If we hit a boundary, set finger positions based on direction
        if (scroll_axis == SCROLL_AXIS_VERTICAL) {
            if (delta > 0) {  // Scrolling down, hit bottom edge
                // Reset to top edge
                finger0_y = 20;
                finger1_y = 20;
            } else {  // Scrolling up, hit top edge
                // Reset to bottom edge
                finger0_y = screen_height - 20;
                finger1_y = screen_height - 20;
            }
        } else {
            if (delta > 0) {  // Scrolling right, hit right edge
                finger0_x = 20;  // Fixed position near left edge
                finger1_x = 120;  // Keep the 100px separation
            } else {  // Scrolling left, hit left edge
                finger0_x = screen_width - 120;  // Fixed position near right edge
                finger1_x = screen_width - 20;
            }
        }
        boundary_reset = 1;
        
        // Return early - we'll start the new gesture on the next call
        return 0;
    }
    
    // Update positions if delta is non-zero
    if (delta != 0 && !boundary_reset) {
        // Check for unreasonably large jumps in finger positions
        if (abs(new_finger0_pos - *finger0_pos) > 100 && !boundary_reset_in_progress) {
            if (debug_mode) {
                printf("WARNING: Preventing large jump in finger position: %d -> %d\n", 
                   *finger0_pos, new_finger0_pos);
            }
            // Limit the jump to a reasonable amount
            if (new_finger0_pos > *finger0_pos) {
                new_finger0_pos = *finger0_pos + 100;
                new_finger1_pos = *finger1_pos + 100;
            } else {
                new_finger0_pos = *finger0_pos - 100;
                new_finger1_pos = *finger1_pos - 100;
            }
        }
        
        *finger0_pos = new_finger0_pos;
        *finger1_pos = new_finger1_pos;
        
        // For horizontal scrolling, ensure finger1 is at the right offset from finger0
        if (scroll_axis == SCROLL_AXIS_HORIZONTAL) {
            finger1_x = finger0_x + 100;
        }
    }
    boundary_reset = 0;  // Reset the flag for next time
    
    // Keep fingers within screen bounds
    if (scroll_axis == SCROLL_AXIS_VERTICAL) {
        // Clamp Y positions
        if (finger0_y < 0) {
            if (debug_mode) printf("WARNING: finger0_y < 0 (%d), clamping to 0\n", finger0_y);
            finger0_y = 0;
        }
        if (finger0_y > screen_height) {
            if (debug_mode) printf("WARNING: finger0_y > screen_height (%d > %d), clamping to %d\n", 
                   finger0_y, screen_height, screen_height);
            finger0_y = screen_height;
        }
        if (finger1_y < 0) {
            if (debug_mode) printf("WARNING: finger1_y < 0 (%d), clamping to 0\n", finger1_y);
            finger1_y = 0;
        }
        if (finger1_y > screen_height) {
            if (debug_mode) printf("WARNING: finger1_y > screen_height (%d > %d), clamping to %d\n", 
                   finger1_y, screen_height, screen_height);
            finger1_y = screen_height;
        }
    } else {
        // Clamp X positions
        if (finger0_x < 0) {
            if (debug_mode) printf("WARNING: finger0_x < 0 (%d), clamping to 0\n", finger0_x);
            finger0_x = 0;
        }
        if (finger0_x > screen_width) {
            if (debug_mode) printf("WARNING: finger0_x > screen_width (%d > %d), clamping to %d\n", 
                   finger0_x, screen_width, screen_width);
            finger0_x = screen_width;
        }
        if (finger1_x < 0) {
            if (debug_mode) printf("WARNING: finger1_x < 0 (%d), clamping to 0\n", finger1_x);
            finger1_x = 0;
        }
        if (finger1_x > screen_width) {
            if (debug_mode) printf("WARNING: finger1_x > screen_width (%d > %d), clamping to %d\n", 
                   finger1_x, screen_width, screen_width);
            finger1_x = screen_width;
        }
    }
    
    // Log final position after all adjustments
    if (debug_mode) {
        printf("POST-CHECK: finger0_%s=%d, finger1_%s=%d, velocity=%.2f, position=%.2f\n",
               (scroll_axis == SCROLL_AXIS_VERTICAL) ? "y" : "x", 
               *finger0_pos, 
               (scroll_axis == SCROLL_AXIS_VERTICAL) ? "y" : "x", 
               *finger1_pos, 
               current_velocity, current_position);
    }
    
    if (debug_mode) {
        if (scroll_axis == SCROLL_AXIS_VERTICAL) {
            printf("Emitting vertical multitouch event with delta: %d (Y positions: %d, %d)\n", 
                   delta, finger0_y, finger1_y);
        } else {
            printf("Emitting horizontal multitouch event with delta: %d (X positions: %d, %d)\n", 
                   delta, finger0_x, finger1_x);
        }
    }
    
    
    // If touch isn't active yet, check if we need to enforce a delay
    if (!touch_active) {
        // Check if enough time has passed since the last gesture ended
        struct timeval now;
        gettimeofday(&now, NULL);
        
        // Calculate milliseconds since last gesture ended
        long elapsed_ms = 0;
        if (last_gesture_end_time.tv_sec > 0) {
            elapsed_ms = (now.tv_sec - last_gesture_end_time.tv_sec) * 1000 + 
                         (now.tv_usec - last_gesture_end_time.tv_usec) / 1000;
        }
        
        // If we're starting a new gesture too soon after the last one ended,
        // add a delay to prevent right-click interpretation
        if (elapsed_ms < MIN_GESTURE_INTERVAL_MS) {
            int delay_needed = MIN_GESTURE_INTERVAL_MS - elapsed_ms;
            if (debug_mode) {
                printf("Adding %d ms delay between gestures to prevent right-click\n", delay_needed);
            }
            usleep(delay_needed * 1000);
        }
        
        // First, select slot 0
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_ABS;
        ev.code = ABS_MT_SLOT;
        ev.value = 0;
        if (write_event_mt(&ev, "Error: slot 0", NULL) < 0) return -1;
        
        // Set tracking ID for first finger
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_ABS;
        ev.code = ABS_MT_TRACKING_ID;
        ev.value = 100;
        if (write_event_mt(&ev, "Error: tracking ID for finger 0", NULL) < 0) return -1;
        
        // Set initial X position for first finger
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_ABS;
        ev.code = ABS_MT_POSITION_X;
        ev.value = finger0_x;
        if (write_event_mt(&ev, "Error: position X for finger 0", NULL) < 0) return -1;
        
        // Set initial Y position for first finger
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_ABS;
        ev.code = ABS_MT_POSITION_Y;
        ev.value = finger0_y;
        if (write_event_mt(&ev, "Error: position Y for finger 0", NULL) < 0) return -1;
        
        // Now select slot 1
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_ABS;
        ev.code = ABS_MT_SLOT;
        ev.value = 1;
        if (write_event_mt(&ev, "Error: slot 1", NULL) < 0) return -1;
        
        // Set tracking ID for second finger
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_ABS;
        ev.code = ABS_MT_TRACKING_ID;
        ev.value = 200;
        if (write_event_mt(&ev, "Error: tracking ID for finger 1", NULL) < 0) return -1;
        
        // Set initial X position for second finger
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_ABS;
        ev.code = ABS_MT_POSITION_X;
        ev.value = finger1_x;
        if (write_event_mt(&ev, "Error: position X for finger 1", NULL) < 0) return -1;
        
        // Set initial Y position for second finger
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_ABS;
        ev.code = ABS_MT_POSITION_Y;
        ev.value = finger1_y;
        if (write_event_mt(&ev, "Error: position Y for finger 1", NULL) < 0) return -1;
        
        // Now set BTN_TOUCH to indicate contact
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_KEY;
        ev.code = BTN_TOUCH;
        ev.value = 1;
        if (write_event_mt(&ev, "Error: BTN_TOUCH press", NULL) < 0) return -1;
        
        // Set BTN_TOOL_DOUBLETAP to indicate two fingers
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_KEY;
        ev.code = BTN_TOOL_DOUBLETAP;
        ev.value = 1;
        if (write_event_mt(&ev, "Error: BTN_TOOL_DOUBLETAP press", NULL) < 0) return -1;
        
        // Sync to apply the initial touch
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_SYN;
        ev.code = SYN_REPORT;
        ev.value = 0;
        if (write_event_mt(&ev, "Error: SYN_REPORT", NULL) < 0) return -1;
        
        touch_active = 1;
        
        // No delay needed - removed for faster response
    }
    
    // Now update the positions for the movement
    
    // Update finger positions based on scroll axis
    if (scroll_axis == SCROLL_AXIS_VERTICAL) {
        // Update finger 0 Y position
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_ABS;
        ev.code = ABS_MT_SLOT;
        ev.value = 0;
        if (write_event_mt(&ev, "Error: slot 0", NULL) < 0) return -1;
        
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_ABS;
        ev.code = ABS_MT_POSITION_Y;
        ev.value = finger0_y;
        if (write_event_mt(&ev, "Error: position Y for finger 0", NULL) < 0) return -1;
        
        // Update finger 1 Y position
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_ABS;
        ev.code = ABS_MT_SLOT;
        ev.value = 1;
        if (write_event_mt(&ev, "Error: slot 1", NULL) < 0) return -1;
        
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_ABS;
        ev.code = ABS_MT_POSITION_Y;
        ev.value = finger1_y;
        if (write_event_mt(&ev, "Error: position Y for finger 1", NULL) < 0) return -1;
    } else {
        // Update finger 0 X position
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_ABS;
        ev.code = ABS_MT_SLOT;
        ev.value = 0;
        if (write_event_mt(&ev, "Error: slot 0", NULL) < 0) return -1;
        
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_ABS;
        ev.code = ABS_MT_POSITION_X;
        ev.value = finger0_x;
        if (write_event_mt(&ev, "Error: position X for finger 0", NULL) < 0) return -1;
        
        // Update finger 1 X position
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_ABS;
        ev.code = ABS_MT_SLOT;
        ev.value = 1;
        if (write_event_mt(&ev, "Error: slot 1", NULL) < 0) return -1;
        
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_ABS;
        ev.code = ABS_MT_POSITION_X;
        ev.value = finger1_x;
        if (write_event_mt(&ev, "Error: position X for finger 1", NULL) < 0) return -1;
    }
    
    // Final sync event
    // Add detailed logging for the first few events after a boundary reset
    static int post_reset_counter = 0;
    
    if (boundary_reset_in_progress && post_reset_counter < 5 && debug_mode) {
        printf("POST-RESET-DEBUG[%d]: Sending event with finger0_%s=%d, finger1_%s=%d, delta=%d, velocity=%.2f\n",
           post_reset_counter, 
           (scroll_axis == SCROLL_AXIS_VERTICAL) ? "y" : "x",
           (scroll_axis == SCROLL_AXIS_VERTICAL) ? finger0_y : finger0_x,
           (scroll_axis == SCROLL_AXIS_VERTICAL) ? "y" : "x",
           (scroll_axis == SCROLL_AXIS_VERTICAL) ? finger1_y : finger1_x,
           delta, current_velocity);
        
        post_reset_counter++;
        if (post_reset_counter >= 5) {
            post_reset_counter = 0;
        }
    } else if (!boundary_reset_in_progress) {
        post_reset_counter = 0;
    }
    
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    if (write_event_mt(&ev, "Error: SYN_REPORT", NULL) < 0) return -1;
    
    return 0;
}


void end_multitouch_gesture(void) {
    static int ending_in_progress = 0;
    
    // Prevent multiple simultaneous calls
    if (ending_in_progress) {
        return;
    }
    
    ending_in_progress = 1;
    
    if (!touch_active || uinput_mt_fd < 0) {
        ending_in_progress = 0;
        return;
    }
    
    if (debug_mode) {
        printf("Ending multitouch gesture\n");
    }
    
    // Record the time when this gesture ends
    gettimeofday(&last_gesture_end_time, NULL);
    
    struct input_event ev;
    
    
    // Release finger 0
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_ABS;
    ev.code = ABS_MT_SLOT;
    ev.value = 0;
    if (write_event_mt(&ev, "Error: slot 0", &ending_in_progress) < 0) return;
    
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_ABS;
    ev.code = ABS_MT_TRACKING_ID;
    ev.value = -1;  // -1 means finger up
    if (write_event_mt(&ev, "Error: tracking ID release for finger 0", &ending_in_progress) < 0) return;
    
    // Release finger 1
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_ABS;
    ev.code = ABS_MT_SLOT;
    ev.value = 1;
    if (write_event_mt(&ev, "Error: slot 1", &ending_in_progress) < 0) return;
    
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_ABS;
    ev.code = ABS_MT_TRACKING_ID;
    ev.value = -1;  // -1 means finger up
    if (write_event_mt(&ev, "Error: tracking ID release for finger 1", &ending_in_progress) < 0) return;
    
    // Release BTN_TOUCH and BTN_TOOL_DOUBLETAP
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_KEY;
    ev.code = BTN_TOUCH;
    ev.value = 0;
    if (write_event_mt(&ev, "Error: BTN_TOUCH release", &ending_in_progress) < 0) return;
    
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_KEY;
    ev.code = BTN_TOOL_DOUBLETAP;
    ev.value = 0;
    if (write_event_mt(&ev, "Error: BTN_TOOL_DOUBLETAP release", &ending_in_progress) < 0) return;
    
    // Final sync event
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    if (write_event_mt(&ev, "Error: SYN_REPORT", &ending_in_progress) < 0) return;
    
    touch_active = 0;
    
    // Reset finger positions for next gesture
    reset_finger_positions();
    
    ending_in_progress = 0;
}

void destroy_virtual_multitouch_device(void) {
    if (ioctl(uinput_mt_fd, UI_DEV_DESTROY) < 0) {
        perror("Error destroying multitouch device");
    }
    close(uinput_mt_fd);
    uinput_mt_fd = -1;
}
