#include <libudev.h>
#include <libevdev-1.0/libevdev/libevdev.h>  // Adjust include path if needed
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <pthread.h> // Add this include
#include <errno.h>   // Add this include for EAGAIN
#include "momentum_mouse.h"

static struct libevdev *evdev = NULL;
static char *mouse_device_path = NULL;
// static int inertia_already_stopped = 0; // Removed

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

    // Note: We no longer use EVIOCGRAB as it blocks all events
    // Instead, we'll selectively handle events in capture_input_event
    if (debug_mode) {
        printf("Using selective event handling (grab_device=%d)\n", grab_device);
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


// --- Start Thread Helper Functions ---

// Function to add delta to the queue (thread-safe)
static void enqueue_scroll_delta(int delta) {
    pthread_mutex_lock(&scroll_queue.mutex);
    if (scroll_queue.count < SCROLL_QUEUE_SIZE) {
        scroll_queue.deltas[scroll_queue.head] = delta;
        scroll_queue.head = (scroll_queue.head + 1) % SCROLL_QUEUE_SIZE;
        scroll_queue.count++;
        // Signal the inertia thread that new data is available
        pthread_cond_signal(&scroll_queue.cond);
    } else {
        if (debug_mode) {
            fprintf(stderr, "Warning: Scroll queue full, dropping delta %d\n", delta);
        }
        // Optional: Implement dropping strategy (e.g., drop oldest by advancing tail)
    }
    pthread_mutex_unlock(&scroll_queue.mutex);
}

// Function to signal stop (thread-safe)
static void signal_stop_request() {
    pthread_mutex_lock(&state_mutex);
    stop_requested = true;
    pthread_cond_signal(&state_cond); // Signal inertia thread
    pthread_mutex_unlock(&state_mutex);
}

// Function to signal friction (thread-safe)
static void signal_friction_request(int magnitude) {
    pthread_mutex_lock(&state_mutex);
    // Only signal if dragging is enabled and magnitude is significant
    if (mouse_move_drag && magnitude > 0) {
         // Accumulate or just set the latest? Let's set latest for simplicity.
         // Use max to handle potentially rapid small movements resulting in larger friction signal
         if (magnitude > pending_friction_magnitude) {
             pending_friction_magnitude = magnitude;
         }
         pthread_cond_signal(&state_cond); // Signal inertia thread
    }
    pthread_mutex_unlock(&state_mutex);
}

// --- End Thread Helper Functions ---


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


// Input thread function
void* input_thread_func(void* arg) {
    (void)arg; // Mark parameter as unused
    printf("Input thread started.\n");
    struct input_event ev; // Move ev declaration outside the loop

    // Ensure evdev is initialized
    if (!evdev) {
        fprintf(stderr, "InputThread: Error - evdev not initialized.\n");
        running = 0; // Signal other threads to stop
        return NULL;
    }

    while (running) {
        // Use blocking read with a timeout to allow checking 'running' flag periodically
        // Or use non-blocking with a small sleep. Let's try blocking with timeout.
        // Get the underlying file descriptor
        int fd = libevdev_get_fd(evdev);
        fd_set read_fds;
        struct timeval timeout;
        int select_ret;

        FD_ZERO(&read_fds);
        FD_SET(fd, &read_fds);

        // Set timeout (e.g., 100ms)
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000; // 100ms

        select_ret = select(fd + 1, &read_fds, NULL, NULL, &timeout);

        if (select_ret < 0) {
            // Error in select
            if (errno == EINTR) continue; // Interrupted by signal, check running flag
            perror("InputThread: select error");
            running = 0;
            break;
        } else if (select_ret == 0) {
            // Timeout - no event, loop to check running flag
            continue;
        }

        // Event is available, read it using non-blocking flag now
        int rc = libevdev_next_event(evdev, LIBEVDEV_READ_FLAG_NORMAL, &ev);

        if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
             // --- Event Handling Logic (adapted from capture_input_event) ---

             // Scroll Wheel Event
             if (ev.type == EV_REL &&
                 ((scroll_axis == SCROLL_AXIS_VERTICAL && ev.code == REL_WHEEL) ||
                  (scroll_axis == SCROLL_AXIS_HORIZONTAL && ev.code == REL_HWHEEL))) {
                 if (debug_mode) {
                     printf("InputThread: Captured %s scroll event: %d\n",
                            (scroll_axis == SCROLL_AXIS_HORIZONTAL) ? "horizontal" : "vertical",
                            ev.value);
                 }
                 enqueue_scroll_delta(ev.value); // Enqueue delta

                 // If grab_device is enabled, don't pass through the scroll event
                 if (!grab_device) {
                     // Pass through a zeroed event if not grabbing to avoid double-scroll
                     struct input_event dummy_ev = ev;
                     dummy_ev.value = 0;
                     emit_passthrough_event(&dummy_ev);
                 }
                 // No return needed, loop continues
             }
             // Escape Key Event
             else if (ev.type == EV_KEY && ev.code == KEY_ESC && ev.value == 1) {
                  if (debug_mode) printf("InputThread: Escape key pressed, signaling stop\n");
                  signal_stop_request();
                  emit_passthrough_event(&ev); // Pass through key event
             }
             // Mouse Movement Event
             else if (ev.type == EV_REL && (ev.code == REL_X || ev.code == REL_Y)) {
                 int movement = abs(ev.value);
                 // Signal friction based on movement if enabled
                 if (movement > 0) {
                      signal_friction_request(movement);
                      if (debug_mode && movement > 5 && mouse_move_drag) {
                        //   printf("InputThread: Mouse movement: %d, signaling friction\n", movement);
                      }
                 }
                 // Signal stop for very large movements (optional, friction might be enough)
                 if (movement > 50) { // Threshold for stopping
                    //   if (debug_mode) printf("InputThread: Large mouse movement: %d, signaling stop\n", movement);
                      signal_stop_request();
                 }
                 emit_passthrough_event(&ev); // Pass through mouse movement
             }
             // Mouse Button Click Event
             else if (ev.type == EV_KEY && (ev.code == BTN_LEFT || ev.code == BTN_RIGHT || ev.code == BTN_MIDDLE) && ev.value == 1) {
                  if (debug_mode) printf("InputThread: Mouse button clicked, signaling stop\n");
                  signal_stop_request();
                  emit_passthrough_event(&ev); // Pass through button event
             }
             // Other relevant events to pass through
             else if (ev.type == EV_REL || ev.type == EV_KEY || ev.type == EV_SYN) {
                 // Pass through other relevant events
                 emit_passthrough_event(&ev);
             }
        } else if (rc == LIBEVDEV_READ_STATUS_SYNC) {
            // Handle sync event if necessary, usually just pass through
             if (debug_mode > 1) printf("InputThread: Received SYNC event\n");
             // Potentially pass through SYN events as well
             emit_passthrough_event(&ev);
        } else if (rc == -EAGAIN) {
            // Should not happen often with select(), but handle anyway
            // No events available, loop will continue
        } else {
            // Error reading event
            perror("InputThread: Error reading input event");
            running = 0; // Stop the application on error
        }
    }

    printf("Input thread exiting.\n");
    return NULL;
}
