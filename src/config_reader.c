#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "inertia_scroller.h"

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

    char line[256];
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

        char key[128] = {0};
        char value[128] = {0};
        
        // Parse key=value format
        if (sscanf(line, "%127[^=]=%127s", key, value) == 2) {
            // Trim whitespace from key
            char *k = key;
            while (*k == ' ' || *k == '\t') k++;
            
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
            }
        }
    }
    
    fclose(fp);
}
