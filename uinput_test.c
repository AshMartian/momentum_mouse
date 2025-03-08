#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/uinput.h>

int setup_uinput_device(int fd) {
    struct uinput_user_dev uidev;
    memset(&uidev, 0, sizeof(uidev));
    
    // Set the name and version of the virtual device
    snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "My Inertia Scroller");
    uidev.id.bustype = BUS_USB;
    uidev.id.vendor  = 0x1234;
    uidev.id.product = 0x5678;
    uidev.id.version = 1;

    // Enable relative events (for scrolling)
    if(ioctl(fd, UI_SET_EVBIT, EV_REL) < 0) {
        perror("Error: UI_SET_EVBIT");
        return -1;
    }
    if(ioctl(fd, UI_SET_RELBIT, REL_WHEEL) < 0) {
        perror("Error: UI_SET_RELBIT");
        return -1;
    }

    // Write device information to uinput
    if(write(fd, &uidev, sizeof(uidev)) < 0) {
        perror("Error: write");
        return -1;
    }

    // Create the device
    if(ioctl(fd, UI_DEV_CREATE) < 0) {
        perror("Error: UI_DEV_CREATE");
        return -1;
    }
    return 0;
}

int main(void) {
    int uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if(uinput_fd < 0) {
        perror("Error: opening /dev/uinput");
        return 1;
    }
    
    if(setup_uinput_device(uinput_fd) < 0) {
        close(uinput_fd);
        return 1;
    }
    
    // Example: Emit a single scroll event (simulate one notch up)
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));

    // First, send the scroll event
    ev.type = EV_REL;
    ev.code = REL_WHEEL;
    ev.value = 1; // positive value scrolls up, negative scrolls down
    write(uinput_fd, &ev, sizeof(ev));

    // Then, send a synchronization event to indicate the end of the event batch
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    write(uinput_fd, &ev, sizeof(ev));

    // Give some time to see the event (for testing purposes)
    sleep(20);

    // Destroy the virtual device before exiting
    if(ioctl(uinput_fd, UI_DEV_DESTROY) < 0) {
        perror("Error: UI_DEV_DESTROY");
    }
    close(uinput_fd);
    return 0;
}

