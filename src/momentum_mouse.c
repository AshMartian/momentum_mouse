#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <syslog.h>
#include <pwd.h>
#include <stdarg.h>
#include "momentum_mouse.h"
#include <linux/limits.h>

// Global configuration variables
int use_multitouch = 1;
int grab_device = 1;  // Default to grabbing for better performance
int daemon_mode = 0;  // Default to foreground mode
ScrollDirection scroll_direction = SCROLL_DIRECTION_TRADITIONAL;  // Default
ScrollAxis scroll_axis = SCROLL_AXIS_VERTICAL;  // Default to vertical scrolling
int auto_detect_direction = 1;  // Try to auto-detect by default
int debug_mode = 0;  // Default to no debug output
double scroll_sensitivity = 1.0;  // Default sensitivity
double scroll_multiplier = 1.0;   // Default multiplier
double scroll_friction = 2.0;     // Default friction
double max_velocity_factor = 0.8; // Default max velocity (80% of screen dimension)
double sensitivity_divisor = 3; // Default sensitivity divisor
double resolution_multiplier = 10.0; // Default resolution multiplier
int refresh_rate = 200; // Default refresh rate (200 Hz)
const char *config_file_override = NULL;  // Config file override path
char *device_override = NULL;  // Device path override

// Implementation of debug_log function
void debug_log(const char *format, ...) {
    if (!debug_mode) return;
    
    va_list args;
    va_start(args, format);
    
    if (daemon_mode) {
        // Format the message first
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), format, args);
        // Log to syslog
        syslog(LOG_INFO, "%s", buffer);
    } else {
        // Log to stdout
        vprintf(format, args);
    }
    
    va_end(args);
}

int main(int argc, char *argv[]) {
    // Parse command line arguments first to get any config override and debug settings
    char *local_device_override = NULL;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("momentum mouse - Smooth scrolling for Linux\n\n");
            printf("Usage: %s [OPTIONS] [DEVICE_PATH]\n\n", argv[0]);
            printf("Options:\n");
            printf("  --help, -h                  Show this help message and exit\n");
            printf("  --debug                     Enable debug logging\n");
            printf("  --grab                      Grab the input device exclusively\n");
            printf("  --no-multitouch             Use wheel events instead of multitouch\n");
            printf("  --natural                   Force natural scrolling direction\n");
            printf("  --traditional               Force traditional scrolling direction\n");
            printf("  --horizontal                Use horizontal scrolling instead of vertical\n");
            printf("  --no-auto-detect            Don't auto-detect system scroll direction\n");
            printf("  --sensitivity=VALUE         Set scroll sensitivity (default: 1.0)\n");
            printf("  --multiplier=VALUE          Set repeating scroll multiplier (default: 1.0)\n");
            printf("  --friction=VALUE            Set scroll friction (default: 1.0)\n");
            printf("                              Lower values make scrolling last longer\n");
            printf("  --max-velocity=VALUE        Set maximum velocity as screen factor (default: 0.8)\n");
            printf("                              Higher values allow faster scrolling\n");
            printf("  --sensitivity-divisor=VALUE Set divisor for touchpad sensitivity (default: 2.5)\n");
            printf("                              Higher values reduce sensitivity for touchpads\n");
            printf("  --resolution-multiplier=VALUE Set resolution multiplier for virtual trackpad (default: 10.0)\n");
            printf("                              Higher values increase precision but may cause issues\n");
            printf("  --refresh-rate=VALUE         Set refresh rate in Hz for inertia updates (default: 200)\n");
            printf("                              Lower values reduce CPU usage but may feel less smooth\n");
            printf("  --config=PATH               Use the specified config file\n");
            printf("  --daemon                    Run as a background daemon\n");
            printf("\n");
            printf("If DEVICE_PATH is provided, use that input device instead of auto-detecting\n");
            return 0;
        } else if (strcmp(argv[i], "--debug") == 0) {
            debug_mode = 1;
        } else if (strcmp(argv[i], "--daemon") == 0) {
            daemon_mode = 1;
        } else if (strncmp(argv[i], "--config=", 9) == 0) {
            config_file_override = argv[i] + 9;
        }
        // Don't process other arguments yet
    }
    
    // Daemonize if requested (do this early, before loading configs)
    if (daemon_mode) {
        // Daemonize the process
        if (daemon(0, 0) < 0) {
            fprintf(stderr, "Failed to daemonize process\n");
            return 1;
        }
        
        // Open syslog for logging
        openlog("momentum_mouse", LOG_PID, LOG_DAEMON);
        syslog(LOG_INFO, "momentum mouse daemon started");
    }
    
    // Now load configs
    if (config_file_override) {
        debug_log("Loading config from override path: %s\n", config_file_override);
        load_config_file(config_file_override);
        // Skip other config loading
    } else {
        // Try to load configuration from system-wide config file first
        debug_log("Loading system-wide config from /etc/momentum_mouse.conf\n");
        load_config_file("/etc/momentum_mouse.conf");
        debug_log("Using system-wide configuration\n");
    }
    
    // Now process the rest of the command line arguments to override config settings
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0 ||
            strcmp(argv[i], "--debug") == 0 || strcmp(argv[i], "--daemon") == 0 ||
            strncmp(argv[i], "--config=", 9) == 0) {
            // Skip options we've already processed
            continue;
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
        } else if (strcmp(argv[i], "--horizontal") == 0) {
            scroll_axis = SCROLL_AXIS_HORIZONTAL;
            debug_log("Using horizontal scrolling\n");
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
        } else if (strncmp(argv[i], "--max-velocity=", 15) == 0) {
            // Parse max velocity factor
            double value = atof(argv[i] + 15);
            if (value > 0.0) {
                max_velocity_factor = value;
            } else {
                fprintf(stderr, "Invalid max velocity factor: %s\n", argv[i] + 15);
                fprintf(stderr, "Using default max velocity factor: 0.8\n");
            }
        } else if (strncmp(argv[i], "--sensitivity-divisor=", 22) == 0) {
            // Parse sensitivity divisor
            double value = atof(argv[i] + 22);
            if (value > 0.0) {
                sensitivity_divisor = value;
            } else {
                fprintf(stderr, "Invalid sensitivity divisor: %s\n", argv[i] + 22);
                fprintf(stderr, "Using default sensitivity divisor: 1.0\n");
            }
        } else if (strncmp(argv[i], "--resolution-multiplier=", 24) == 0) {
            // Parse resolution multiplier
            double value = atof(argv[i] + 24);
            if (value > 0.0) {
                resolution_multiplier = value;
            } else {
                fprintf(stderr, "Invalid resolution multiplier: %s\n", argv[i] + 24);
                fprintf(stderr, "Using default resolution multiplier: 10.0\n");
            }
        } else if (strncmp(argv[i], "--refresh-rate=", 15) == 0) {
            // Parse refresh rate
            int value = atoi(argv[i] + 15);
            if (value > 0) {
                refresh_rate = value;
            } else {
                fprintf(stderr, "Invalid refresh rate: %s\n", argv[i] + 15);
                fprintf(stderr, "Using default refresh rate: 200\n");
            }
        } else if (argv[i][0] != '-') {
            // Assume this is the device path
            if (local_device_override) {
                free(local_device_override);
            }
            local_device_override = strdup(argv[i]);
            device_override = local_device_override;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            fprintf(stderr, "Use --help for usage information\n");
            return 1;
        }
    }
    
    // Try to auto-detect scroll direction if enabled
    if (auto_detect_direction) {
        if (!detect_scroll_direction()) {
            debug_log("Could not auto-detect scroll direction, using traditional\n");
        }
    }
    
    
    debug_log("Configuration: multitouch=%s, grab=%s, scroll_direction=%s, scroll_axis=%s, debug=%s\n", 
           use_multitouch ? "enabled" : "disabled",
           grab_device ? "enabled" : "disabled",
           scroll_direction == SCROLL_DIRECTION_NATURAL ? "natural" : "traditional",
           scroll_axis == SCROLL_AXIS_HORIZONTAL ? "horizontal" : "vertical",
           debug_mode ? "enabled" : "disabled");
    debug_log("Sensitivity: %.2f, Multiplier: %.2f, Friction: %.2f, Divisor: %.2f\n", 
           scroll_sensitivity, scroll_multiplier, scroll_friction, sensitivity_divisor);
    
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
    
    debug_log("momentum mouse running. Scroll your mouse wheel!\n");
    
    while (1) {
        capture_input_event();
        // Process inertia:
        if (use_multitouch) {
            process_inertia_mt();
        } else {
            process_inertia();
        }
        usleep(1000000 / refresh_rate); // Convert Hz to microseconds
    }
    
    // Clean up resources
    cleanup_input_capture();
    if (use_multitouch) {
        destroy_virtual_multitouch_device();
    } else {
        destroy_virtual_device();
    }
    
    if (daemon_mode) {
        syslog(LOG_INFO, "momentum mouse daemon stopped");
        closelog();
    }
    
    return 0;
}
