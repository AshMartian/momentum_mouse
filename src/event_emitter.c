#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/uinput.h>
#include "inertia_scroller.h"

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

    // Prepare uinput device structure
    struct uinput_user_dev uidev;
    memset(&uidev, 0, sizeof(uidev));
    snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "My Inertia Scroller");
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
    
    // Send the scroll event (REL_WHEEL)
    ev.type = EV_REL;
    ev.code = REL_WHEEL;
    ev.value = value;  // value > 0 scrolls up, < 0 scrolls down
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

void destroy_virtual_device(void) {
    if (ioctl(uinput_fd, UI_DEV_DESTROY) < 0) {
        perror("Error destroying uinput device");
    }
    close(uinput_fd);
    uinput_fd = -1;
}
