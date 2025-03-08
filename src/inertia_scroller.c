#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "inertia_scroller.h"

// Global configuration variables
int use_multitouch = 1;
int grab_device = 0;  // Default to not grabbing
ScrollDirection scroll_direction = SCROLL_DIRECTION_TRADITIONAL;  // Default
int auto_detect_direction = 1;  // Try to auto-detect by default

int main(int argc, char *argv[]) {
    // Parse command line arguments
    const char *device_override = NULL;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--grab") == 0) {
            grab_device = 1;
        } else if (strcmp(argv[i], "--no-multitouch") == 0) {
            use_multitouch = 0;
        } else if (strcmp(argv[i], "--natural") == 0) {
            scroll_direction = SCROLL_DIRECTION_NATURAL;
            auto_detect_direction = 0;  // Override auto-detection
        } else if (strcmp(argv[i], "--traditional") == 0) {
            scroll_direction = SCROLL_DIRECTION_TRADITIONAL;
            auto_detect_direction = 0;  // Override auto-detection
        } else if (strcmp(argv[i], "--no-auto-detect") == 0) {
            auto_detect_direction = 0;  // Don't auto-detect
        } else if (argv[i][0] != '-') {
            // Assume this is the device path
            device_override = argv[i];
        }
    }
    
    // Try to auto-detect scroll direction if enabled
    if (auto_detect_direction) {
        if (!detect_scroll_direction()) {
            printf("Could not auto-detect scroll direction, using traditional\n");
        }
    }
    
    printf("Configuration: multitouch=%s, grab=%s, scroll_direction=%s\n", 
           use_multitouch ? "enabled" : "disabled",
           grab_device ? "enabled" : "disabled",
           scroll_direction == SCROLL_DIRECTION_NATURAL ? "natural" : "traditional");
    
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
        if (use_multitouch) {
            process_inertia_mt();
        } else {
            process_inertia();
        }
        usleep(5000);
    }
    
    // Clean up resources
    cleanup_input_capture();
    if (use_multitouch) {
        destroy_virtual_multitouch_device();
    } else {
        destroy_virtual_device();
    }
    
    return 0;
}
