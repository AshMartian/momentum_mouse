#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/uinput.h>
#include <sys/time.h>
#include "inertia_scroller.h"

// We'll store the uinput file descriptor for multitouch events here.
static int uinput_mt_fd = -1;
static int touch_active = 0;  // Track if touch is currently active
static struct timeval last_gesture_end_time = {0, 0};
static const int MIN_GESTURE_INTERVAL_MS = 100; // Minimum time between gestures in milliseconds

// Set up a virtual multitouch device.
int setup_virtual_multitouch_device(void) {
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
    
    // Set absolute axis ranges
    uidev.absmin[ABS_MT_POSITION_X] = 0;
    uidev.absmax[ABS_MT_POSITION_X] = 1920;
    uidev.absmin[ABS_MT_POSITION_Y] = 0;
    uidev.absmax[ABS_MT_POSITION_Y] = 1080;
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
// Global state for finger positions (initial positions)
static int finger0_y = 540;
static int finger1_y = 540;


// This function updates finger positions by adding the delta
// and sends out updated multitouch events.
int emit_two_finger_scroll_event(int delta) {
    struct input_event ev;
    
    // Calculate new positions
    int new_finger0_y = finger0_y + delta;
    int new_finger1_y = finger1_y + delta;
    
    // Simple boundary handling - just wrap around when hitting screen edges
    if (new_finger0_y >= 1080 && delta > 0) {
        // We've hit the bottom boundary, wrap to top
        new_finger0_y = 0;
        new_finger1_y = 0;
    } else if (new_finger0_y <= 0 && delta < 0) {
        // We've hit the top boundary, wrap to bottom
        new_finger0_y = 1080;
        new_finger1_y = 1080;
    }
    
    // Update positions
    finger0_y = new_finger0_y;
    finger1_y = new_finger1_y;
    
    // Keep fingers within screen bounds
    if (finger0_y < 0) finger0_y = 0;
    if (finger0_y > 1080) finger0_y = 1080;
    if (finger1_y < 0) finger1_y = 0;
    if (finger1_y > 1080) finger1_y = 1080;
    
    if (debug_mode) {
        printf("Emitting multitouch event with delta: %d (positions: %d, %d)\n", 
               delta, finger0_y, finger1_y);
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
        ev.value = 960 - 50;  // left of center
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
        ev.value = 960 + 50;  // right of center
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
    
    // Update finger 0 position
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
    
    // Update finger 1 position
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
    
    // Final sync event
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
    finger0_y = 540;
    finger1_y = 540;
    
    ending_in_progress = 0;
}

void destroy_virtual_multitouch_device(void) {
    if (ioctl(uinput_mt_fd, UI_DEV_DESTROY) < 0) {
        perror("Error destroying multitouch device");
    }
    close(uinput_mt_fd);
    uinput_mt_fd = -1;
}
