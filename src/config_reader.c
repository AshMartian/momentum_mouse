#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "momentum_mouse.h"

// Load configuration from the specified file
void load_config_file(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        if (debug_mode) {
            printf("Could not open config file: %s\n", filename);
        }
        return;
    }

    if (debug_mode) {
        printf("Reading configuration from: %s\n", filename);
    }

    // Add this to log the entire file content for debugging
    if (debug_mode) {
        printf("Config file contents:\n");
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), fp) != NULL) {
            printf("  %s", buffer);
        }
        printf("End of config file\n");
        rewind(fp);
    }

    char line[256];
    int in_smooth_scroll_section = 0;  // Flag to track if we're in the [smooth_scroll] section
    
    while (fgets(line, sizeof(line), fp)) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }

        // Remove trailing newline
        size_t len = strlen(line);
        if (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[len-1] = '\0';
        }
        
        // Check for section header
        if (line[0] == '[') {
            // If this is the [smooth_scroll] section, set the flag
            if (strncmp(line, "[smooth_scroll]", 15) == 0) {
                in_smooth_scroll_section = 1;
            } else {
                // Any other section, clear the flag
                in_smooth_scroll_section = 0;
            }
            continue;  // Skip processing this line further
        }

        // If we're not in the smooth_scroll section and we found a section header, skip this line
        if (!in_smooth_scroll_section && strchr(line, '[') != NULL) {
            continue;
        }

        char key[128] = {0};
        char value[128] = {0};
        
        // Parse key=value format
        char *equals_pos = strchr(line, '=');
        if (equals_pos) {
            // Extract the key (everything before the equals sign)
            size_t key_len = equals_pos - line;
            if (key_len > sizeof(key) - 1) key_len = sizeof(key) - 1;
            strncpy(key, line, key_len);
            key[key_len] = '\0';
            
            // Trim whitespace from key
            char *k = key;
            while (*k == ' ' || *k == '\t') k++;
            
            // Extract the value (everything after the equals sign)
            equals_pos++; // Move past the equals sign
            strncpy(value, equals_pos, sizeof(value) - 1);
            value[sizeof(value) - 1] = '\0';
            
            // Trim trailing whitespace from value
            size_t val_len = strlen(value);
            while (val_len > 0 && (value[val_len-1] == ' ' || value[val_len-1] == '\t' || 
                                   value[val_len-1] == '\n' || value[val_len-1] == '\r')) {
                value[--val_len] = '\0';
            }
            
            if (strcmp(k, "sensitivity") == 0) {
                double val = atof(value);
                if (val > 0.0) {
                    scroll_sensitivity = val;
                    if (debug_mode) {
                        printf("Config: sensitivity=%.2f\n", scroll_sensitivity);
                    }
                }
            } else if (strcmp(k, "multiplier") == 0) {
                double val = atof(value);
                if (val > 0.0) {
                    scroll_multiplier = val;
                    if (debug_mode) {
                        printf("Config: multiplier=%.2f\n", scroll_multiplier);
                    }
                }
            } else if (strcmp(k, "friction") == 0) {
                double val = atof(value);
                if (val > 0.0) {
                    scroll_friction = val;
                    if (debug_mode) {
                        printf("Config: friction=%.2f\n", scroll_friction);
                    }
                }
            } else if (strcmp(k, "grab") == 0) {
                if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
                    grab_device = 1;
                    if (debug_mode) {
                        printf("Config: grab=true\n");
                    }
                } else if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0) {
                    grab_device = 0;
                    if (debug_mode) {
                        printf("Config: grab=false\n");
                    }
                }
            } else if (strcmp(k, "natural") == 0) {
                if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
                    scroll_direction = SCROLL_DIRECTION_NATURAL;
                    auto_detect_direction = 0;
                    if (debug_mode) {
                        printf("Config: natural=true\n");
                    }
                } else if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0) {
                    scroll_direction = SCROLL_DIRECTION_TRADITIONAL;
                    auto_detect_direction = 0;
                    if (debug_mode) {
                        printf("Config: natural=false\n");
                    }
                }
            } else if (strcmp(k, "multitouch") == 0) {
                if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
                    use_multitouch = 1;
                    if (debug_mode) {
                        printf("Config: multitouch=true\n");
                    }
                } else if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0) {
                    use_multitouch = 0;
                    if (debug_mode) {
                        printf("Config: multitouch=false\n");
                    }
                }
            } else if (strcmp(k, "horizontal") == 0) {
                if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
                    scroll_axis = SCROLL_AXIS_HORIZONTAL;
                    if (debug_mode) {
                        printf("Config: horizontal=true\n");
                    }
                } else if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0) {
                    scroll_axis = SCROLL_AXIS_VERTICAL;
                    if (debug_mode) {
                        printf("Config: horizontal=false\n");
                    }
                }
            } else if (strcmp(k, "debug") == 0) {
                if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
                    debug_mode = 1;
                    printf("Config: debug=true\n");
                } else if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0) {
                    debug_mode = 0;
                }
            } else if (strcmp(k, "max_velocity") == 0) {
                double val = atof(value);
                if (val > 0.0) {
                    max_velocity_factor = val;
                    if (debug_mode) {
                        printf("Config: max_velocity=%.2f\n", max_velocity_factor);
                    }
                }
            } else if (strcmp(k, "sensitivity_divisor") == 0) {
                double val = atof(value);
                if (val > 0.0) {
                    sensitivity_divisor = val;
                    if (debug_mode) {
                        printf("Config: sensitivity_divisor=%.2f\n", sensitivity_divisor);
                    }
                }
            } else if (strcmp(k, "resolution_multiplier") == 0) {
                double val = atof(value);
                if (val > 0.0) {
                    resolution_multiplier = val;
                    if (debug_mode) {
                        printf("Config: resolution_multiplier=%.2f\n", resolution_multiplier);
                    }
                }
            } else if (strcmp(k, "refresh_rate") == 0) {
                int val = atoi(value);
                if (val > 0) {
                    refresh_rate = val;
                    if (debug_mode) {
                        printf("Config: refresh_rate=%d\n", refresh_rate);
                    }
                }
            } else if (strcmp(k, "device_name") == 0) {
                // Store the device name
                if (strlen(value) > 0) {
                    // Find the device path by name
                    char *path = find_device_by_name(value);
                    if (path) {
                        if (debug_mode) {
                            printf("Config: device_name=%s (path=%s)\n", value, path);
                        }
                        // Store the device path in the global variable
                        if (device_override == NULL) {
                            device_override = strdup(path);
                        }
                        free(path);
                    } else if (debug_mode) {
                        printf("Config: device_name=%s (not found)\n", value);
                    }
                }
            }
        }
    }
    
    fclose(fp);
}
