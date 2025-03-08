#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/uinput.h>
#include "inertia_scroller.h"

// We'll store the uinput file descriptor for multitouch events here.
static int uinput_mt_fd = -1;

// Set up a virtual multitouch device.
int setup_virtual_multitouch_device(void) {
    uinput_mt_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (uinput_mt_fd < 0) {
        perror("Error opening /dev/uinput for multitouch");
        return -1;
    }
    
    // Enable absolute events (EV_ABS) and synchronization events (EV_SYN)
    if (ioctl(uinput_mt_fd, UI_SET_EVBIT, EV_ABS) < 0) {
        perror("Error setting EV_ABS");
        return -1;
    }
    if (ioctl(uinput_mt_fd, UI_SET_EVBIT, EV_SYN) < 0) {
        perror("Error setting EV_SYN");
        return -1;
    }
    
    // Enable multitouch events: ABS_MT_SLOT, ABS_MT_TRACKING_ID, POSITION_X/Y
    if (ioctl(uinput_mt_fd, UI_SET_ABSBIT, ABS_MT_SLOT) < 0) {
        perror("Error setting ABS_MT_SLOT");
        return -1;
    }
    if (ioctl(uinput_mt_fd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID) < 0) {
        perror("Error setting ABS_MT_TRACKING_ID");
        return -1;
    }
    if (ioctl(uinput_mt_fd, UI_SET_ABSBIT, ABS_MT_POSITION_X) < 0) {
        perror("Error setting ABS_MT_POSITION_X");
        return -1;
    }
    if (ioctl(uinput_mt_fd, UI_SET_ABSBIT, ABS_MT_POSITION_Y) < 0) {
        perror("Error setting ABS_MT_POSITION_Y");
        return -1;
    }
    
    // Enable BTN_TOUCH and BTN_TOOL_FINGER so the OS recognizes finger contact.
    if (ioctl(uinput_mt_fd, UI_SET_EVBIT, EV_KEY) < 0) {
        perror("Error setting EV_KEY");
        return -1;
    }
    if (ioctl(uinput_mt_fd, UI_SET_KEYBIT, BTN_TOUCH) < 0) {
        perror("Error setting BTN_TOUCH");
        return -1;
    }
    if (ioctl(uinput_mt_fd, UI_SET_KEYBIT, BTN_TOOL_FINGER) < 0) {
        perror("Error setting BTN_TOOL_FINGER");
        return -1;
    }
    
    // Configure the virtual device.
    struct uinput_user_dev uidev;
    memset(&uidev, 0, sizeof(uidev));
    snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "My Inertia Scroller MT");
    uidev.id.bustype = BUS_USB;
    uidev.id.vendor  = 0x1234;
    uidev.id.product = 0x5678;
    uidev.id.version = 1;
    
    // Set absolute axis ranges (for a hypothetical screen resolution).
    uidev.absmin[ABS_MT_POSITION_X] = 0;
    uidev.absmax[ABS_MT_POSITION_X] = 1920;
    uidev.absmin[ABS_MT_POSITION_Y] = 0;
    uidev.absmax[ABS_MT_POSITION_Y] = 1080;
    // For slots, we have 2 fingers (slots 0 and 1).
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
    return 0;
}
// Global state for finger positions (initial positions)
static int finger0_y = 540;
static int finger1_y = 540;

// This function updates finger positions by adding the delta
// and sends out updated multitouch events.
int emit_two_finger_scroll_event(int delta) {
    struct input_event ev;
    
    // Update positions based on delta
    finger0_y += delta;
    finger1_y += delta;
    
    // --- Finger 0 ---
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_ABS;
    ev.code = ABS_MT_SLOT;
    ev.value = 0;
    if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: slot 0"); return -1; }
    
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_ABS;
    ev.code = ABS_MT_TRACKING_ID;
    ev.value = 100; // fixed tracking id (ensure it stays the same during the gesture)
    if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: tracking ID for finger 0"); return -1; }
    
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_ABS;
    ev.code = ABS_MT_POSITION_X;
    ev.value = 960;  // roughly center horizontally
    if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: position X for finger 0"); return -1; }
    
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_ABS;
    ev.code = ABS_MT_POSITION_Y;
    ev.value = finger0_y;
    if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: position Y for finger 0"); return -1; }
    
    // --- Finger 1 ---
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_ABS;
    ev.code = ABS_MT_SLOT;
    ev.value = 1;
    if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: slot 1"); return -1; }
    
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_ABS;
    ev.code = ABS_MT_TRACKING_ID;
    ev.value = 200; // another fixed tracking id
    if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: tracking ID for finger 1"); return -1; }
    
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_ABS;
    ev.code = ABS_MT_POSITION_X;
    ev.value = 980;  // slightly to the right of finger 0
    if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: position X for finger 1"); return -1; }
    
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_ABS;
    ev.code = ABS_MT_POSITION_Y;
    ev.value = finger1_y;
    if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: position Y for finger 1"); return -1; }
    
    // Signal touch contact
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_KEY;
    ev.code = BTN_TOUCH;
    ev.value = 1;  // finger is touching
    if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: BTN_TOUCH press"); return -1; }
    
    // Final sync event
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    if (write(uinput_mt_fd, &ev, sizeof(ev)) < 0) { perror("Error: SYN_REPORT"); return -1; }
    
    return 0;
}


void destroy_virtual_multitouch_device(void) {
    if (ioctl(uinput_mt_fd, UI_DEV_DESTROY) < 0) {
        perror("Error destroying multitouch device");
    }
    close(uinput_mt_fd);
    uinput_mt_fd = -1;
}
