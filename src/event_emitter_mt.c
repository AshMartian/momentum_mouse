#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/uinput.h>
#include <sys/time.h>
#include <X11/Xlib.h>
#include <math.h>
#include "inertia_scroller.h"

// External references to inertia logic variables
extern double current_position;
extern double current_velocity;

// We'll store the uinput file descriptor for multitouch events here.
static int uinput_mt_fd = -1;
static int touch_active = 0;  // Track if touch is currently active
static struct timeval last_gesture_end_time = {0, 0};
static const int MIN_GESTURE_INTERVAL_MS = 100; // Minimum time between gestures in milliseconds
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
    screen_width = DisplayWidth(display, screen) / 2;  // Smaller trackpad size than screen
    screen_height = DisplayHeight(display, screen) / 2;
    
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
    snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "Inertia Scroller Touchpad");
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

// Flag to track post-boundary transition frames
int post_boundary_frames = 0;


// This function updates finger positions by adding the delta
// and sends out updated multitouch events.
int emit_two_finger_scroll_event(int delta) {
    struct input_event ev;
    static int boundary_reset = 0;  // Flag to track if we just did a boundary reset
    static int reset_count = 0;     // Count consecutive resets
    static struct timeval last_reset_time = {0, 0};
    
    // Get current time for timing calculations
    struct timeval now;
    gettimeofday(&now, NULL);
    
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
    
    // Calculate new positions based on scroll axis
    if (scroll_axis == SCROLL_AXIS_VERTICAL) {
        // Vertical scrolling - update Y positions
        int new_finger0_y = finger0_y + delta;
        int new_finger1_y = finger1_y + delta;
        
        // Log detailed position information before boundary check
        if (debug_mode) {
            printf("PRE-CHECK: finger0_y=%d, delta=%d, new_finger0_y=%d, velocity=%.2f, position=%.2f\n",
                   finger0_y, delta, new_finger0_y, current_velocity, current_position);
        }
        
        // Calculate boundary buffer based on velocity
        double abs_velocity = fabs(current_velocity);
        int boundary_buffer = (int)(abs_velocity * 0.2);  // 20% of velocity as buffer
        
        // Better boundary handling - reset to opposite edge when hitting or about to hit screen edges
        if ((new_finger0_y >= (screen_height - boundary_buffer) && delta > 0) || 
            (new_finger0_y <= boundary_buffer && delta < 0) || 
            new_finger0_y < 0) {
            reset_count++;
            last_reset_time = now;
            
            // Print boundary hit information
            printf("BOUNDARY: Hit vertical boundary at y=%d, delta=%d, resetting to %s (reset #%d in %.2fs, buffer=%d)\n", 
                   new_finger0_y, delta, (delta > 0) ? "top" : "bottom", reset_count, time_since_reset, boundary_buffer);
            printf("BOUNDARY: Current velocity before reset: %.2f\n", current_velocity);
            printf("BOUNDARY: Current position before reset: %.2f\n", current_position);
            
            // First, properly end the current touch gesture
            if (debug_mode) {
                printf("BOUNDARY: Ending touch gesture before reset\n");
            }
            end_multitouch_gesture();
            
            // Ensure a proper delay between ending and starting a new gesture
            // This is critical for proper touch event handling
            usleep(15000);  // 15ms delay
            
            // Adjust velocity - reduce it but keep direction
            double direction = (current_velocity > 0) ? 1.0 : -1.0;
            double abs_velocity = fabs(current_velocity);
            
            // Use a fixed reduction factor for simplicity
            double velocity_scale = 0.7;  // 30% reduction
            current_velocity = direction * abs_velocity * velocity_scale;
            printf("BOUNDARY: Adjusted velocity to %.2f\n", current_velocity);
            
            // Set position to a small value in the right direction
            double old_position = current_position;
            current_position = (current_velocity > 0) ? 10.0 : -10.0;
            printf("BOUNDARY: Adjusted position from %.2f to %.2f\n", old_position, current_position);
            
            // Set new finger positions based on direction
            if (delta > 0) {  // Scrolling down, hit bottom edge
                // Reset to top edge
                finger0_y = 20;
                finger1_y = 20;
                printf("BOUNDARY: Reset fingers to top: y=%d\n", finger0_y);
            } else {  // Scrolling up, hit top edge
                // Reset to bottom edge
                finger0_y = screen_height - 20;
                finger1_y = screen_height - 20;
                printf("BOUNDARY: Reset fingers to bottom: y=%d\n", finger0_y);
            }
            
            // Set boundary reset flags
            boundary_reset = 1;
            boundary_reset_in_progress = 1;
            gettimeofday(&last_boundary_reset_time, NULL);
            
            // Store reset info
            boundary_reset_info.reset_time = now;
            boundary_reset_info.reset_velocity = current_velocity;
            boundary_reset_info.reset_position = current_position;
            boundary_reset_info.reset_direction = (delta > 0) ? 1 : -1;
            
            // Set post-boundary frames - use a fixed value for simplicity
            post_boundary_frames = 10;
            
            // Skip the normal position update since we just reset positions
            delta = 0;
            
            // The touch_active flag was reset by end_multitouch_gesture()
            // The next call to this function will start a new touch gesture
            
            // If we're resetting too frequently, reduce velocity more aggressively
            if (reset_count > 1 && time_since_reset < 0.3) {
                current_velocity *= 0.5;  // 50% reduction for rapid resets
                printf("BOUNDARY: Too many resets (%d in %.2fs), reducing velocity to %.2f\n", 
                       reset_count, time_since_reset, current_velocity);
            }
            
            // Check if velocity is still significant enough to continue scrolling
            if (fabs(current_velocity) < 1.0) {
                printf("BOUNDARY: Velocity too low after reset (%.2f), stopping inertia\n", current_velocity);
                stop_inertia();
            }
            
            // Return early - we'll start the new gesture on the next call
            return 0;
            boundary_reset = 1;  // Set flag to prevent immediate position update
            
            // Ensure finger positions are within reasonable bounds after reset
            if (finger0_y < 0) finger0_y = 0;
            if (finger0_y > screen_height) finger0_y = screen_height;
            if (finger1_y < 0) finger1_y = 0;
            if (finger1_y > screen_height) finger1_y = screen_height;
            
            // Always print this message to help diagnose the issue
            printf("BOUNDARY: Hit vertical boundary at y=%d, delta=%d, resetting to %s y=%d (reset #%d in %.2fs, buffer=%d)\n", 
                   new_finger0_y, delta, (delta > 0) ? "top" : "bottom", finger0_y, reset_count, time_since_reset, boundary_buffer);
            printf("BOUNDARY: Current velocity before reset: %.2f\n", current_velocity);
            printf("BOUNDARY: Current position before reset: %.2f\n", current_position);
            
            // Don't reduce velocity as much - just enough to prevent jumps
            // but maintain the smooth scrolling feel
            double h_direction = (current_velocity > 0) ? 1.0 : -1.0;
            // Ensure velocity direction matches the scroll direction after reset
            if ((delta > 0 && h_direction < 0) || (delta < 0 && h_direction > 0)) {
                // If velocity direction doesn't match scroll direction, flip it
                h_direction = -h_direction;
                printf("BOUNDARY: Direction mismatch detected, flipping velocity direction\n");
            }
            
            // Scale velocity reduction based on how high the velocity is
            double h_velocity_scale = 0.7;  // Always reduce velocity by 30% at boundaries
            double h_abs_velocity = fabs(current_velocity);
            
            // Apply progressively more aggressive reduction as velocity increases
            if (h_abs_velocity > 500) {
                h_velocity_scale = 0.3;  // 70% reduction for extremely high velocities
            } else if (h_abs_velocity > 300) {
                h_velocity_scale = 0.4;  // 60% reduction for very high velocities
            } else if (h_abs_velocity > 200) {
                h_velocity_scale = 0.5;  // 50% reduction for high velocities
            }
            
            current_velocity = h_direction * h_abs_velocity * h_velocity_scale;
            printf("BOUNDARY: Adjusted velocity to %.2f\n", current_velocity);
            
            // Don't reset position to 0, just adjust it to maintain momentum
            // Reset position to a small fixed value to prevent jumps
            double h_old_position = current_position;
            // Just set to a small fixed value in the right direction
            current_position = (current_velocity > 0) ? 10.0 : -10.0;
            
            // Print position adjustment information
            printf("BOUNDARY: Adjusted position from %.2f to %.2f\n", h_old_position, current_position);
    
            // Set flag for post-boundary transition
            // Use more frames for higher velocities to ensure smoother transitions
            // Reuse the abs_velocity variable from above
            if (abs_velocity > 300) {
                post_boundary_frames = 30;  // More frames for high velocities
            } else if (abs_velocity > 150) {
                post_boundary_frames = 25;  // Standard for medium velocities
            } else {
                post_boundary_frames = 20;  // Fewer frames for low velocities
            }
            
            // Set the boundary reset flag and timestamp
            boundary_reset_in_progress = 1;
            gettimeofday(&last_boundary_reset_time, NULL);
            
            // Store the reset information for the transition handler
            boundary_reset_info.reset_time = now;
            boundary_reset_info.reset_velocity = current_velocity;
            boundary_reset_info.reset_position = current_position;
            boundary_reset_info.reset_direction = (delta > 0) ? 1 : -1;
            
            // The touch_active flag was reset by end_multitouch_gesture()
            // The next time this function is called, it will start a new touch gesture
            // with the new finger positions
            
            // Add a small delay after boundary reset to ensure smooth transition
            if (debug_mode) {
                printf("BOUNDARY: Adding small delay after reset\n");
            }
            usleep(10000);  // 10ms delay - slightly longer to ensure proper processing
            
            // Skip the normal position update since we just reset positions
            delta = 0;
            
            // If we're resetting too frequently, reduce velocity more aggressively
            if (reset_count > 1 && time_since_reset < 0.3) {
                // More aggressive reduction based on reset count
                double reduction_factor = 0.3 / reset_count;  // Gets smaller with more resets
                if (reduction_factor < 0.05) reduction_factor = 0.05;  // Minimum 5%
                current_velocity *= reduction_factor;
                printf("BOUNDARY: Too many resets (%d in %.2fs), reducing velocity to %.2f\n", 
                       reset_count, time_since_reset, current_velocity);
            }
        }
        
        // Update positions if delta is non-zero
        if (delta != 0 && !boundary_reset) {
            // Check for unreasonably large jumps in finger positions
            if (abs(new_finger0_y - finger0_y) > 100 && !boundary_reset_in_progress) {
                printf("WARNING: Preventing large jump in finger position: %d -> %d\n", 
                       finger0_y, new_finger0_y);
                // Limit the jump to a reasonable amount
                if (new_finger0_y > finger0_y) {
                    new_finger0_y = finger0_y + 100;
                    new_finger1_y = finger1_y + 100;
                } else {
                    new_finger0_y = finger0_y - 100;
                    new_finger1_y = finger1_y - 100;
                }
            }
            
            finger0_y = new_finger0_y;
            finger1_y = new_finger1_y;
        }
        boundary_reset = 0;  // Reset the flag for next time
        
        // Keep fingers within screen bounds
        if (finger0_y < 0) {
            printf("WARNING: finger0_y < 0 (%d), clamping to 0\n", finger0_y);
            finger0_y = 0;
        }
        if (finger0_y > screen_height) {
            printf("WARNING: finger0_y > screen_height (%d > %d), clamping to %d\n", 
                   finger0_y, screen_height, screen_height);
            finger0_y = screen_height;
        }
        if (finger1_y < 0) {
            printf("WARNING: finger1_y < 0 (%d), clamping to 0\n", finger1_y);
            finger1_y = 0;
        }
        if (finger1_y > screen_height) {
            printf("WARNING: finger1_y > screen_height (%d > %d), clamping to %d\n", 
                   finger1_y, screen_height, screen_height);
            finger1_y = screen_height;
        }
        
        // Log final position after all adjustments
        if (debug_mode) {
            printf("POST-CHECK: finger0_y=%d, finger1_y=%d, velocity=%.2f, position=%.2f\n",
                   finger0_y, finger1_y, current_velocity, current_position);
        }
    } else {
        // Horizontal scrolling - update X positions
        int new_finger0_x = finger0_x + delta;
        int new_finger1_x = finger1_x + delta;
        
        // Log detailed position information before boundary check
        if (debug_mode) {
            printf("PRE-CHECK: finger0_x=%d, delta=%d, new_finger0_x=%d, velocity=%.2f, position=%.2f\n",
                   finger0_x, delta, new_finger0_x, current_velocity, current_position);
        }
        
        // Calculate boundary buffer based on velocity
        double abs_velocity = fabs(current_velocity);
        int boundary_buffer = (int)(abs_velocity * 0.2);  // 20% of velocity as buffer
        
        // Better boundary handling - reset to opposite edge when hitting or about to hit screen edges
        if ((new_finger0_x >= (screen_width - boundary_buffer) && delta > 0) || 
            (new_finger0_x <= boundary_buffer && delta < 0) || 
            new_finger0_x < 0) {
            reset_count++;
            last_reset_time = now;
            
            // We've hit a boundary, reset to opposite edge based on direction
            if (delta > 0) {  // Scrolling right, hit right edge
                finger0_x = 20;  // Fixed position near left edge
                finger1_x = 120;  // Keep the 100px separation
            } else {  // Scrolling left, hit left edge
                finger0_x = screen_width - 120;  // Fixed position near right edge
                finger1_x = screen_width - 20;
            }
            boundary_reset = 1;  // Set flag to prevent immediate position update
            
            // Always print this message to help diagnose the issue
            printf("BOUNDARY: Hit horizontal boundary at x=%d, delta=%d, resetting to %s x=%d (reset #%d in %.2fs, buffer=%d)\n", 
                   new_finger0_x, delta, (delta > 0) ? "left" : "right", finger0_x, reset_count, time_since_reset, boundary_buffer);
            printf("BOUNDARY: Current velocity before reset: %.2f\n", current_velocity);
            printf("BOUNDARY: Current position before reset: %.2f\n", current_position);
            
            // Don't reduce velocity as much - just enough to prevent jumps
            // but maintain the smooth scrolling feel
            double direction = (current_velocity > 0) ? 1.0 : -1.0;
            // Ensure velocity direction matches the scroll direction after reset
            if ((delta > 0 && direction < 0) || (delta < 0 && direction > 0)) {
                // If velocity direction doesn't match scroll direction, flip it
                direction = -direction;
                printf("BOUNDARY: Direction mismatch detected, flipping velocity direction\n");
            }
            
            // Scale velocity reduction based on how high the velocity is
            double velocity_scale = 0.7;  // Always reduce velocity by 30% at boundaries
            double abs_velocity = fabs(current_velocity);
            
            // Apply progressively more aggressive reduction as velocity increases
            if (abs_velocity > 500) {
                velocity_scale = 0.3;  // 70% reduction for extremely high velocities
            } else if (abs_velocity > 300) {
                velocity_scale = 0.4;  // 60% reduction for very high velocities
            } else if (abs_velocity > 200) {
                velocity_scale = 0.5;  // 50% reduction for high velocities
            }
            
            current_velocity = direction * abs_velocity * velocity_scale;
            printf("BOUNDARY: Adjusted velocity to %.2f\n", current_velocity);
            
            // Don't reset position to 0, just adjust it to maintain momentum
            // Reset position to a small fixed value to prevent jumps
            double old_position = current_position;
            // Instead of using a fixed position value, calculate a position that maintains
            // the same relative position within the scrollable area
            // This ensures continuity in the scrolling experience
            double position_scale = 0.01;  // Very small scale factor
            // Preserve the sign of the velocity for direction consistency
            current_position = direction * fabs(current_velocity) * position_scale;
            
            // Ensure the position is at least a minimum value to prevent stalling
            double min_position = 5.0;
            if (fabs(current_position) < min_position) {
                current_position = direction * min_position;
            }
            
            // Ensure the position has the same sign as the velocity
            if ((current_position > 0 && current_velocity < 0) ||
                (current_position < 0 && current_velocity > 0)) {
                current_position = -current_position;
            }
            
            // Print position adjustment information
            printf("BOUNDARY: Adjusted position from %.2f to %.2f\n", old_position, current_position);
            
            // Set flag for post-boundary transition
            // Use more frames for higher velocities to ensure smoother transitions
            // Reuse the abs_velocity variable from above
            if (abs_velocity > 300) {
                post_boundary_frames = 30;  // More frames for high velocities
            } else if (abs_velocity > 150) {
                post_boundary_frames = 25;  // Standard for medium velocities
            } else {
                post_boundary_frames = 20;  // Fewer frames for low velocities
            }
            
            // Store the reset information for the transition handler
            boundary_reset_info.reset_time = now;
            boundary_reset_info.reset_velocity = current_velocity;
            boundary_reset_info.reset_position = current_position;
            boundary_reset_info.reset_direction = (delta > 0) ? 1 : -1;
            
            // Add a small delay after boundary reset to ensure smooth transition
            if (debug_mode) {
                printf("BOUNDARY: Adding small delay after reset\n");
            }
            usleep(5000);  // 5ms delay
            
            // Skip the normal position update since we just reset positions
            delta = 0;
        }
        
        // Update positions if delta is non-zero
        if (delta != 0 && !boundary_reset) {
            finger0_x = new_finger0_x;
            finger1_x = new_finger1_x;
            // Keep finger1 at the right offset from finger0
            finger1_x = finger0_x + 100;
        }
        boundary_reset = 0;  // Reset the flag for next time
        
        // Keep fingers within screen bounds
        if (finger0_x < 0) {
            printf("WARNING: finger0_x < 0 (%d), clamping to 0\n", finger0_x);
            finger0_x = 0;
        }
        if (finger0_x > screen_width) {
            printf("WARNING: finger0_x > screen_width (%d > %d), clamping to %d\n", 
                   finger0_x, screen_width, screen_width);
            finger0_x = screen_width;
        }
        if (finger1_x < 0) {
            printf("WARNING: finger1_x < 0 (%d), clamping to 0\n", finger1_x);
            finger1_x = 0;
        }
        if (finger1_x > screen_width) {
            printf("WARNING: finger1_x > screen_width (%d > %d), clamping to %d\n", 
                   finger1_x, screen_width, screen_width);
            finger1_x = screen_width;
        }
        
        // Log final position after all adjustments
        if (debug_mode) {
            printf("POST-CHECK: finger0_x=%d, finger1_x=%d, velocity=%.2f, position=%.2f\n",
                   finger0_x, finger1_x, current_velocity, current_position);
        }
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
        if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: slot 0"); return -1; }
        
        // Set tracking ID for first finger
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_ABS;
        ev.code = ABS_MT_TRACKING_ID;
        ev.value = 100;
        if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: tracking ID for finger 0"); return -1; }
        
        // Set initial X position for first finger
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_ABS;
        ev.code = ABS_MT_POSITION_X;
        ev.value = finger0_x;
        if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: position X for finger 0"); return -1; }
        
        // Set initial Y position for first finger
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_ABS;
        ev.code = ABS_MT_POSITION_Y;
        ev.value = finger0_y;
        if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: position Y for finger 0"); return -1; }
        
        // Now select slot 1
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_ABS;
        ev.code = ABS_MT_SLOT;
        ev.value = 1;
        if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: slot 1"); return -1; }
        
        // Set tracking ID for second finger
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_ABS;
        ev.code = ABS_MT_TRACKING_ID;
        ev.value = 200;
        if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: tracking ID for finger 1"); return -1; }
        
        // Set initial X position for second finger
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_ABS;
        ev.code = ABS_MT_POSITION_X;
        ev.value = finger1_x;
        if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: position X for finger 1"); return -1; }
        
        // Set initial Y position for second finger
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_ABS;
        ev.code = ABS_MT_POSITION_Y;
        ev.value = finger1_y;
        if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: position Y for finger 1"); return -1; }
        
        // Now set BTN_TOUCH to indicate contact
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_KEY;
        ev.code = BTN_TOUCH;
        ev.value = 1;
        if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: BTN_TOUCH press"); return -1; }
        
        // Set BTN_TOOL_DOUBLETAP to indicate two fingers
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_KEY;
        ev.code = BTN_TOOL_DOUBLETAP;
        ev.value = 1;
        if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: BTN_TOOL_DOUBLETAP press"); return -1; }
        
        // Sync to apply the initial touch
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_SYN;
        ev.code = SYN_REPORT;
        ev.value = 0;
        if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: SYN_REPORT"); return -1; }
        
        touch_active = 1;
        
        // Small delay to ensure the initial touch is registered
        usleep(5000);
    }
    
    // Now update the positions for the movement
    
    // Update finger positions based on scroll axis
    if (scroll_axis == SCROLL_AXIS_VERTICAL) {
        // Update finger 0 Y position
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_ABS;
        ev.code = ABS_MT_SLOT;
        ev.value = 0;
        if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: slot 0"); return -1; }
        
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_ABS;
        ev.code = ABS_MT_POSITION_Y;
        ev.value = finger0_y;
        if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: position Y for finger 0"); return -1; }
        
        // Update finger 1 Y position
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_ABS;
        ev.code = ABS_MT_SLOT;
        ev.value = 1;
        if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: slot 1"); return -1; }
        
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_ABS;
        ev.code = ABS_MT_POSITION_Y;
        ev.value = finger1_y;
        if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: position Y for finger 1"); return -1; }
    } else {
        // Update finger 0 X position
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_ABS;
        ev.code = ABS_MT_SLOT;
        ev.value = 0;
        if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: slot 0"); return -1; }
        
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_ABS;
        ev.code = ABS_MT_POSITION_X;
        ev.value = finger0_x;
        if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: position X for finger 0"); return -1; }
        
        // Update finger 1 X position
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_ABS;
        ev.code = ABS_MT_SLOT;
        ev.value = 1;
        if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: slot 1"); return -1; }
        
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_ABS;
        ev.code = ABS_MT_POSITION_X;
        ev.value = finger1_x;
        if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: position X for finger 1"); return -1; }
    }
    
    // Final sync event
    // Add detailed logging for the first few events after a boundary reset
    static int post_reset_counter = 0;
    
    if (boundary_reset_in_progress && post_reset_counter < 5) {
        printf("POST-RESET-DEBUG[%d]: Sending event with finger0_y=%d, finger1_y=%d, delta=%d, velocity=%.2f\n",
               post_reset_counter, finger0_y, finger1_y, delta, current_velocity);
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
    if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: SYN_REPORT"); return -1; }
    
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
    if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: slot 0"); ending_in_progress = 0; return; }
    
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_ABS;
    ev.code = ABS_MT_TRACKING_ID;
    ev.value = -1;  // -1 means finger up
    if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: tracking ID release for finger 0"); ending_in_progress = 0; return; }
    
    // Release finger 1
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_ABS;
    ev.code = ABS_MT_SLOT;
    ev.value = 1;
    if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: slot 1"); ending_in_progress = 0; return; }
    
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_ABS;
    ev.code = ABS_MT_TRACKING_ID;
    ev.value = -1;  // -1 means finger up
    if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: tracking ID release for finger 1"); ending_in_progress = 0; return; }
    
    // Release BTN_TOUCH and BTN_TOOL_DOUBLETAP
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_KEY;
    ev.code = BTN_TOUCH;
    ev.value = 0;
    if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: BTN_TOUCH release"); ending_in_progress = 0; return; }
    
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_KEY;
    ev.code = BTN_TOOL_DOUBLETAP;
    ev.value = 0;
    if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: BTN_TOOL_DOUBLETAP release"); ending_in_progress = 0; return; }
    
    // Final sync event
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: SYN_REPORT"); ending_in_progress = 0; return; }
    
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
