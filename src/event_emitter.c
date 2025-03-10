#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/uinput.h>
#include "momentum_mouse.h"

// We'll store the uinput file descriptor here
static int uinput_fd = -1;

int setup_virtual_device(void) {
    uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (uinput_fd < 0) {
        perror("Error opening /dev/uinput");
        return -1;
    }

    // Enable relative events and specifically the REL_WHEEL event for scrolling
    if (ioctl(uinput_fd, UI_SET_EVBIT, EV_REL) < 0) {
        perror("Error setting EV_REL");
        return -1;
    }
    if (ioctl(uinput_fd, UI_SET_RELBIT, REL_WHEEL) < 0) {
        perror("Error setting REL_WHEEL");
        return -1;
    }
    // Enable horizontal wheel for horizontal scrolling
    if (ioctl(uinput_fd, UI_SET_RELBIT, REL_HWHEEL) < 0) {
        perror("Error setting REL_HWHEEL");
        return -1;
    }

    // Prepare uinput device structure
    struct uinput_user_dev uidev;
    memset(&uidev, 0, sizeof(uidev));
    snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "My momentum mouse");
    uidev.id.bustype = BUS_USB;
    uidev.id.vendor  = 0x1234;
    uidev.id.product = 0x5678;
    uidev.id.version = 1;

    if (write(uinput_fd, &uidev, sizeof(uidev)) < 0) {
        perror("Error writing uinput device");
        return -1;
    }
    if (ioctl(uinput_fd, UI_DEV_CREATE) < 0) {
        perror("Error creating uinput device");
        return -1;
    }
    return 0;
}

int emit_scroll_event(int value) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    
    // Send the scroll event (REL_WHEEL or REL_HWHEEL based on scroll_axis)
    ev.type = EV_REL;
    ev.code = (scroll_axis == SCROLL_AXIS_HORIZONTAL) ? REL_HWHEEL : REL_WHEEL;
    ev.value = value;  // value > 0 scrolls up/right, < 0 scrolls down/left
    if (write(uinput_fd, &ev, sizeof(ev)) < 0) {
        perror("Error writing scroll event");
        return -1;
    }
    
    // Send a synchronization event so the OS processes the batch
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    if (write(uinput_fd, &ev, sizeof(ev)) < 0) {
        perror("Error writing sync event");
        return -1;
    }
    return 0;
}

// Pass through all non-scroll events from the original mouse
int emit_passthrough_event(struct input_event *ev) {
    // If grab_device is enabled, block all wheel events completely
    if (grab_device && ev->type == EV_REL && (ev->code == REL_WHEEL || ev->code == REL_HWHEEL) && ev->value != 0) {
        if (debug_mode) {
            printf("Blocking wheel event (grab mode): type=%d, code=%d, value=%d\n", 
                   ev->type, ev->code, ev->value);
        }
        return 0;
    }
    
    // Check if the file descriptor is valid
    if (uinput_fd < 0) {
        return -1;
    }
    
    // Pass through all other events
    if (write(uinput_fd, ev, sizeof(struct input_event)) < 0) {
        // Only log errors for non-SYN events to reduce noise
        // And don't log mouse movement errors to reduce spam
        if (ev->type != EV_SYN && !(ev->type == EV_REL && (ev->code == REL_X || ev->code == REL_Y))) {
            perror("Error writing passthrough event");
        }
        return -1;
    }
    
    return 0;
}

void destroy_virtual_device(void) {
    if (ioctl(uinput_fd, UI_DEV_DESTROY) < 0) {
        perror("Error destroying uinput device");
    }
    close(uinput_fd);
    uinput_fd = -1;
}
