#include <libudev.h>
#include <libevdev-1.0/libevdev/libevdev.h>  // Adjust include path if needed
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "momentum_mouse.h"

static struct libevdev *evdev = NULL;
static char *mouse_device_path = NULL;
static int inertia_already_stopped = 0;

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
        if (debug_mode) {
            printf("Using override mouse device: %s\n", mouse_device_path);
        }
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
        if (debug_mode) {
            printf("Found mouse device: %s\n", mouse_device_path);
        }
    }
    
    int fd = open(mouse_device_path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("Error opening mouse device");
        free(mouse_device_path);
        mouse_device_path = NULL;
        return -1;
    }

    // Only grab the device if explicitly requested
    if (grab_device) {
        if (ioctl(fd, EVIOCGRAB, 1) < 0) {
            perror("Failed to grab device");
            // Continue anyway, don't return error
        } else if (debug_mode) {
            printf("Device grabbed exclusively\n");
        }
    }

    int rc = libevdev_new_from_fd(fd, &evdev);
    if (rc < 0) {
        fprintf(stderr, "Failed to initialize libevdev: %s\n", strerror(-rc));
        free(mouse_device_path);
        mouse_device_path = NULL;
        close(fd);
        return -1;
    }
    return 0;
}

// Clean up resources used by input capture
void cleanup_input_capture(void) {
    if (evdev) {
        libevdev_free(evdev);
        evdev = NULL;
    }
    if (mouse_device_path) {
        free(mouse_device_path);
        mouse_device_path = NULL;
    }
}

// Capture a single input event from the physical mouse.
// If it's a scroll (REL_WHEEL) event, update inertia logic.
int capture_input_event(void) {
    struct input_event ev;
    int rc = libevdev_next_event(evdev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
    if (rc == 0) {
        if (ev.type == EV_REL && 
            ((scroll_axis == SCROLL_AXIS_VERTICAL && ev.code == REL_WHEEL) ||
             (scroll_axis == SCROLL_AXIS_HORIZONTAL && ev.code == REL_HWHEEL))) {
            if (debug_mode) {
                printf("Captured %s scroll event: %d\n", 
                       (scroll_axis == SCROLL_AXIS_HORIZONTAL) ? "horizontal" : "vertical", 
                       ev.value);
            }
            inertia_already_stopped = 0;  // Reset flag when scrolling
            update_inertia(ev.value);
            
            // We'll let the inertia logic handle emitting events
            // Don't pass through directly here
            return 1;
        }
        // Check for Escape key to reset scrolling (emergency stop)
        else if (ev.type == EV_KEY && ev.code == KEY_ESC && ev.value == 1) {
            if (is_inertia_active()) {
                if (debug_mode) {
                    printf("Escape key pressed, stopping inertia\n");
                }
                stop_inertia();
                inertia_already_stopped = 1;
            }
            // Still pass through the key event
            emit_passthrough_event(&ev);
            return 1;
        }
        // Apply friction when mouse moves (REL_X or REL_Y events)
        else if (ev.type == EV_REL && (ev.code == REL_X || ev.code == REL_Y)) {
            // Instead of stopping inertia completely, apply friction based on movement
            if (is_inertia_active()) {
                // Calculate movement magnitude (simple approximation)
                int movement = abs(ev.value);
                
                // Apply friction proportional to movement magnitude, but much gentler
                apply_mouse_friction(movement);
                
                if (debug_mode && movement > 5) {
                    printf("Mouse movement: %d, applying friction\n", movement);
                }
        
                // Only stop inertia completely for very large movements
                if (movement > 50) {  // Increased from 20 to 50
                    if (!inertia_already_stopped) {
                        if (debug_mode) {
                            printf("Large mouse movement: %d, stopping inertia\n", movement);
                        }
                        stop_inertia();
                        inertia_already_stopped = 1;
                    }
                }
            }
            
            // Pass through the mouse movement event
            // Don't report errors for mouse movement events to reduce console spam
            emit_passthrough_event(&ev);
            return 1;
        }
        // Check for mouse button clicks - they often indicate the user wants to stop scrolling
        else if (ev.type == EV_KEY && (ev.code == BTN_LEFT || ev.code == BTN_RIGHT || ev.code == BTN_MIDDLE) && ev.value == 1) {
            if (is_inertia_active()) {
                if (debug_mode) {
                    printf("Mouse button clicked, stopping inertia\n");
                }
                stop_inertia();
                inertia_already_stopped = 1;
            }
            // Pass through the button event
            emit_passthrough_event(&ev);
            return 1;
        }
        else if (ev.type == EV_REL || ev.type == EV_KEY || ev.type == EV_SYN) {
            // Only pass through relevant events and check return value
            if (emit_passthrough_event(&ev) < 0) {
                // Only print for non-SYN events and only in debug mode
                if (ev.type != EV_SYN && debug_mode) {
                    fprintf(stderr, "Warning: Failed to pass through event type %d, code %d\n", 
                            ev.type, ev.code);
                }
            }
            return 1;
        }
    }
    return 0;
}
