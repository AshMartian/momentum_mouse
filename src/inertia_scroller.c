#include <stdio.h>
#include <unistd.h>
#include "inertia_scroller.h"

// Set this flag based on command-line argument or configuration.
int use_multitouch = 1;

int main(int argc, char *argv[]) {
    // Initialize input capture (use override if provided)
    const char *device_override = (argc > 1) ? argv[1] : NULL;
    
    // Initialize the virtual device based on the mode first
    if (use_multitouch) {
        if (setup_virtual_multitouch_device() < 0) {
            fprintf(stderr, "Failed to set up virtual multitouch device.\n");
            return 1;
        }
    } else {
        if (setup_virtual_device() < 0) {
            fprintf(stderr, "Failed to set up virtual device.\n");
            return 1;
        }
    }
    
    // Then initialize input capture
    if (initialize_input_capture(device_override) < 0) {
        fprintf(stderr, "Failed to initialize input capture.\n");
        // Clean up the virtual device
        if (use_multitouch) {
            destroy_virtual_multitouch_device();
        } else {
            destroy_virtual_device();
        }
        return 1;
    }
    
    printf("Inertia scroller running. Scroll your mouse wheel!\n");
    
    while (1) {
        capture_input_event();
        // Process inertia:
        // Here, process_inertia() would call either emit_scroll_event() or emit_two_finger_scroll_event()
        if (use_multitouch) {
            // For example:
            // process_inertia_mt(); // You could create a variant that uses multitouch
            // For now, as a demo, we directly call the multitouch emitter:
            // (In a real implementation, you’d want to integrate the inertia physics here.)
            // Let's assume we have a current delta from inertia logic (replace 'current_delta' with actual value)
            int current_delta = 5;  // This would be computed dynamically.
            emit_two_finger_scroll_event(current_delta);
        } else {
            process_inertia();
        }
        usleep(5000);
    }
    
    if (use_multitouch) {
        destroy_virtual_multitouch_device();
    } else {
        destroy_virtual_device();
    }
    
    return 0;
}
