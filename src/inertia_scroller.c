#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "inertia_scroller.h"

// Global configuration variables
int use_multitouch = 1;
int grab_device = 0;  // Default to not grabbing
ScrollDirection scroll_direction = SCROLL_DIRECTION_TRADITIONAL;  // Default
int auto_detect_direction = 1;  // Try to auto-detect by default
int debug_mode = 0;  // Default to no debug output
double scroll_sensitivity = 1.0;  // Default sensitivity
double scroll_multiplier = 1.0;   // Default multiplier
double scroll_friction = 1.0;     // Default friction

int main(int argc, char *argv[]) {
    // Parse command line arguments
    const char *device_override = NULL;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Inertia Scroller - Smooth scrolling for Linux\n\n");
            printf("Usage: %s [OPTIONS] [DEVICE_PATH]\n\n", argv[0]);
            printf("Options:\n");
            printf("  --help, -h                  Show this help message and exit\n");
            printf("  --debug                     Enable debug logging\n");
            printf("  --grab                      Grab the input device exclusively\n");
            printf("  --no-multitouch             Use wheel events instead of multitouch\n");
            printf("  --natural                   Force natural scrolling direction\n");
            printf("  --traditional               Force traditional scrolling direction\n");
            printf("  --no-auto-detect            Don't auto-detect system scroll direction\n");
            printf("  --sensitivity=VALUE         Set scroll sensitivity (default: 1.0)\n");
            printf("  --multiplier=VALUE          Set repeating scroll multiplier (default: 1.0)\n");
            printf("  --friction=VALUE            Set scroll friction (default: 1.0)\n");
            printf("                              Lower values make scrolling last longer\n");
            printf("\n");
            printf("If DEVICE_PATH is provided, use that input device instead of auto-detecting\n");
            return 0;
        } else if (strcmp(argv[i], "--debug") == 0) {
            debug_mode = 1;
        } else if (strcmp(argv[i], "--grab") == 0) {
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
        } else if (strncmp(argv[i], "--sensitivity=", 14) == 0) {
            // Parse sensitivity value
            double value = atof(argv[i] + 14);
            if (value > 0.0) {
                scroll_sensitivity = value;
            } else {
                fprintf(stderr, "Invalid sensitivity value: %s\n", argv[i] + 14);
                fprintf(stderr, "Using default sensitivity: 1.0\n");
            }
        } else if (strncmp(argv[i], "--multiplier=", 13) == 0) {
            // Parse multiplier value
            double value = atof(argv[i] + 13);
            if (value > 0.0) {
                scroll_multiplier = value;
            } else {
                fprintf(stderr, "Invalid multiplier value: %s\n", argv[i] + 13);
                fprintf(stderr, "Using default multiplier: 1.0\n");
            }
        } else if (strncmp(argv[i], "--friction=", 11) == 0) {
            // Parse friction value
            double value = atof(argv[i] + 11);
            if (value > 0.0) {
                scroll_friction = value;
            } else {
                fprintf(stderr, "Invalid friction value: %s\n", argv[i] + 11);
                fprintf(stderr, "Using default friction: 1.0\n");
            }
        } else if (argv[i][0] != '-') {
            // Assume this is the device path
            device_override = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            fprintf(stderr, "Use --help for usage information\n");
            return 1;
        }
    }
    
    // Try to auto-detect scroll direction if enabled
    if (auto_detect_direction) {
        if (!detect_scroll_direction()) {
            if (debug_mode) {
                printf("Could not auto-detect scroll direction, using traditional\n");
            }
        }
    }
    
    if (debug_mode) {
        printf("Configuration: multitouch=%s, grab=%s, scroll_direction=%s, debug=%s\n", 
               use_multitouch ? "enabled" : "disabled",
               grab_device ? "enabled" : "disabled",
               scroll_direction == SCROLL_DIRECTION_NATURAL ? "natural" : "traditional",
               debug_mode ? "enabled" : "disabled");
        printf("Sensitivity: %.2f, Multiplier: %.2f, Friction: %.2f\n", 
               scroll_sensitivity, scroll_multiplier, scroll_friction);
    }
    
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
    
    if (debug_mode) {
        printf("Inertia scroller running. Scroll your mouse wheel!\n");
    }
    
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
