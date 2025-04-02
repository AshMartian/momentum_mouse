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
    
    // Get the screen dimensions using basic Xlib functions and apply resolution multiplier
    screen_width = DisplayWidth(display, screen) * resolution_multiplier;
    screen_height = DisplayHeight(display, screen) * resolution_multiplier;
    
    XCloseDisplay(display);
    
    if (debug_mode) {
        printf("Detected screen size: %dx%d (with resolution multiplier %.2f)\n", 
               screen_width, screen_height, resolution_multiplier);
    }
}

// Move finger positions to the opposite edge after hitting a boundary.
void jump_finger_positions(int delta) {
    const int JUMP_OFFSET = 50; // Pixels offset from the edge after jumping

    if (scroll_axis == SCROLL_AXIS_VERTICAL) {
        if (delta < 0) { // Hit top edge (0), jump to bottom
            finger0_y = screen_height - JUMP_OFFSET;
            finger1_y = screen_height - JUMP_OFFSET;
            if (debug_mode) printf("BOUNDARY JUMP: Hit Top -> Jumped to Y=%d\n", finger0_y);
        } else { // Hit bottom edge (screen_height), jump to top
            finger0_y = JUMP_OFFSET;
            finger1_y = JUMP_OFFSET;
            if (debug_mode) printf("BOUNDARY JUMP: Hit Bottom -> Jumped to Y=%d\n", finger0_y);
        }
        // Keep X positions centered
        finger0_x = screen_width / 2 - 50;
        finger1_x = screen_width / 2 + 50;
    } else { // SCROLL_AXIS_HORIZONTAL
        if (delta < 0) { // Hit left edge (0), jump to right
            finger0_x = screen_width - JUMP_OFFSET - 100; // Adjust for finger spacing
            finger1_x = screen_width - JUMP_OFFSET;
             if (debug_mode) printf("BOUNDARY JUMP: Hit Left -> Jumped to X=%d\n", finger1_x);
        } else { // Hit right edge (screen_width), jump to left
            finger0_x = JUMP_OFFSET;
            finger1_x = JUMP_OFFSET + 100; // Adjust for finger spacing
            if (debug_mode) printf("BOUNDARY JUMP: Hit Right -> Jumped to X=%d\n", finger0_x);
        }
        // Keep Y positions centered
        finger0_y = screen_height / 2;
        finger1_y = screen_height / 2;
    }
}

// Set up a virtual multitouch device.
// Reset finger positions based on screen size (Made non-static)
void reset_finger_positions(void) {
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
// Forward declaration of the reset function (now non-static)
void reset_finger_positions(void);

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
int post_boundary_frames = 0; // Note: This might need mutex protection if accessed by multiple threads, but currently only inertia thread uses it.


// This function updates finger positions by adding the delta
// and sends out updated multitouch events.
// It should ONLY be called by the inertia thread.
// It does NOT access shared state like velocity/position directly.
int emit_two_finger_scroll_event(int delta) {
    struct input_event ev;
    // static int boundary_reset = 0; // Removed - Use global boundary_reset_in_progress

    // Calculate new positions based on scroll axis
    int new_finger0_pos, new_finger1_pos;
    int *finger0_pos, *finger1_pos;
    int screen_limit; // Store screen limit for boundary check

    if (scroll_axis == SCROLL_AXIS_VERTICAL) {
        // Vertical scrolling - update Y positions
        finger0_pos = &finger0_y;
        finger1_pos = &finger1_y;
        // screen_size = screen_height; // Removed unused assignment
    } else {
        // Horizontal scrolling - update X positions
        finger0_pos = &finger0_x;
        finger1_pos = &finger1_x;
        // screen_size = screen_width; // Removed unused assignment
    }
    new_finger0_pos = *finger0_pos + delta;
    new_finger1_pos = *finger1_pos + delta;
    
    // Log detailed position information before boundary check (Removed velocity/position)
    if (debug_mode > 1) { // Reduce verbosity
        printf("EMIT_MT: finger0_%s=%d, delta=%d, new_finger0_%s=%d\n",
               (scroll_axis == SCROLL_AXIS_VERTICAL) ? "y" : "x",
               *finger0_pos, delta,
               (scroll_axis == SCROLL_AXIS_VERTICAL) ? "y" : "x",
               new_finger0_pos);
    }

    // --- Boundary Check ---
    screen_limit = (scroll_axis == SCROLL_AXIS_VERTICAL) ? screen_height : screen_width;
    // Check if *either* finger would go out of bounds based on the calculated delta
    if (!boundary_reset_in_progress && (new_finger0_pos < 0 || new_finger0_pos > screen_limit || new_finger1_pos < 0 || new_finger1_pos > screen_limit)) {
        if (debug_mode) {
            printf("BOUNDARY: Hit detected in emitter! finger0_pos=%d, finger1_pos=%d, delta=%d, limit=%d\n",
                   *finger0_pos, *finger1_pos, delta, screen_limit);
        }
        // boundary_reset = 1; // Removed
        boundary_reset_in_progress = 1; // Set global flag
        gettimeofday(&last_boundary_reset_time, NULL);

        end_multitouch_gesture();   // End the current touch sequence visually
        jump_finger_positions(delta); // Jump fingers to the opposite edge

        // DO NOT stop inertia here. Let velocity persist.
        // The visual gesture ends, positions reset, and cooldown prevents immediate re-trigger.

        return 0; // Indicate event was handled by reset, don't proceed further
    }
    // --- End Boundary Check ---


    // Update positions if delta is non-zero and not immediately after a boundary reset
    if (delta != 0) { // Removed !boundary_reset check, cooldown handles this
        *finger0_pos = new_finger0_pos;
        *finger1_pos = new_finger1_pos;

        // For horizontal scrolling, ensure finger1 is at the right offset from finger0
        if (scroll_axis == SCROLL_AXIS_HORIZONTAL) {
            finger1_x = finger0_x + 100; // Maintain relative horizontal position
        }
    }
    // boundary_reset = 0; // Removed

    // Clamping logic removed - boundary reset handles extremes by stopping/resetting

    // Log final position after all adjustments
    if (debug_mode > 1) { // Reduce verbosity
        printf("EMIT_MT: Final finger0_%s=%d, finger1_%s=%d\n",
               (scroll_axis == SCROLL_AXIS_VERTICAL) ? "y" : "x",
               *finger0_pos,
               (scroll_axis == SCROLL_AXIS_VERTICAL) ? "y" : "x",
               *finger1_pos);
    }

    if (debug_mode > 1) { // Reduce verbosity
        if (scroll_axis == SCROLL_AXIS_VERTICAL) {
            printf("EMIT_MT: Emitting vertical event delta: %d (Y: %d, %d)\n",
                   delta, finger0_y, finger1_y);
        } else {
            printf("EMIT_MT: Emitting horizontal event delta: %d (X: %d, %d)\n",
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
    // Removed post-reset debug logging block

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
