#include <libudev.h>
#include <libevdev-1.0/libevdev/libevdev.h>  // Adjust include path if needed
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "inertia_scroller.h"

static struct libevdev *evdev = NULL;
static char *mouse_device_path = NULL;

// Helper to extract the event number from a device node string (e.g. "/dev/input/event5")
static int extract_event_number(const char *devnode) {
    int event_number = -1;
    if (devnode) {
        sscanf(devnode, "/dev/input/event%d", &event_number);
    }
    return event_number;
}

// Discover the default mouse device using libudev and initialize libevdev on it.
// If device_override is provided (non-NULL), that device node is used.
int initialize_input_capture(const char *device_override) {
    if (device_override) {
        mouse_device_path = strdup(device_override);
        printf("Using override mouse device: %s\n", mouse_device_path);
    } else {
        struct udev *udev;
        struct udev_enumerate *enumerate;
        struct udev_list_entry *devices, *dev_list_entry;
        const char *devnode;
        
        udev = udev_new();
        if (!udev) {
            fprintf(stderr, "Cannot create udev context.\n");
            return -1;
        }
        
        enumerate = udev_enumerate_new(udev);
        udev_enumerate_add_match_subsystem(enumerate, "input");
        // Filter for devices with the property ID_INPUT_MOUSE=1
        udev_enumerate_add_match_property(enumerate, "ID_INPUT_MOUSE", "1");
        udev_enumerate_scan_devices(enumerate);
        devices = udev_enumerate_get_list_entry(enumerate);
        
        int min_event = INT_MAX;
        char *selected_path = NULL;
        
        udev_list_entry_foreach(dev_list_entry, devices) {
            const char *path = udev_list_entry_get_name(dev_list_entry);
            struct udev_device *dev = udev_device_new_from_syspath(udev, path);
            devnode = udev_device_get_devnode(dev);
            if (devnode) {
                int event_num = extract_event_number(devnode);
                if (event_num >= 0 && event_num < min_event) {
                    min_event = event_num;
                    if (selected_path)
                        free(selected_path);
                    selected_path = strdup(devnode);
                }
            }
            udev_device_unref(dev);
        }
        
        udev_enumerate_unref(enumerate);
        udev_unref(udev);
        
        if (!selected_path) {
            fprintf(stderr, "No mouse device found.\n");
            return -1;
        }
        mouse_device_path = selected_path;
        printf("Found mouse device: %s\n", mouse_device_path);
    }
    
    int fd = open(mouse_device_path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("Error opening mouse device");
        free(mouse_device_path);
        return -1;
    }

    int rc = libevdev_new_from_fd(fd, &evdev);
    if (rc < 0) {
        fprintf(stderr, "Failed to initialize libevdev: %s\n", strerror(-rc));
        free(mouse_device_path);
        return -1;
    }
    return 0;
}

// Capture a single input event from the physical mouse.
// If it's a scroll (REL_WHEEL) event, update inertia logic.
int capture_input_event(void) {
    struct input_event ev;
    int rc = libevdev_next_event(evdev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
    if (rc == 0) {
        if (ev.type == EV_REL && ev.code == REL_WHEEL) {
            printf("Captured scroll event: %d\n", ev.value);
            update_inertia(ev.value);
            
            // We'll let the inertia logic handle emitting events
            // Don't pass through directly here
        }
        else if (ev.type != EV_SYN) {
            // For non-scroll events, we could also pass them through
            // This would require a more general event emitter function
            // For now, we're just handling scroll events
        }
        return 1;
    }
    return 0;
}
