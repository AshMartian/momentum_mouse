#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/limits.h>
#include "momentum_mouse.h"

// Function to check if a device is likely a mouse
static int is_mouse_device(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    
    unsigned long evbit = 0;
    if (ioctl(fd, EVIOCGBIT(0, sizeof(evbit)), &evbit) < 0) {
        close(fd);
        return 0;
    }
    
    // Check if device supports relative events (mice do)
    int has_rel = (evbit & (1 << EV_REL)) != 0;
    
    // Check if device has mouse buttons
    unsigned long keybit[KEY_MAX / (sizeof(unsigned long) * 8) + 1] = {0};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybit)), keybit) < 0) {
        close(fd);
        return has_rel; // If it has relative events, it's probably a mouse
    }
    
    int has_btn_left = (keybit[BTN_LEFT / (sizeof(unsigned long) * 8)] & (1 << (BTN_LEFT % (sizeof(unsigned long) * 8)))) != 0;
    
    close(fd);
    return has_rel && has_btn_left;
}

// Function to get device name from its path
static int get_device_name(const char *path, char *name, size_t name_size) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    
    memset(name, 0, name_size); // Ensure the buffer is zeroed
    
    if (ioctl(fd, EVIOCGNAME(name_size - 1), name) < 0) {
        close(fd);
        return -1;
    }
    
    // Ensure null termination
    name[name_size - 1] = '\0';
    
    close(fd);
    return 0;
}

// Function to list all input devices
int list_input_devices(InputDevice **devices) {
    DIR *dir;
    struct dirent *entry;
    int count = 0;
    int capacity = 10;
    
    *devices = malloc(capacity * sizeof(InputDevice));
    if (!*devices) return -1;
    
    // Open the /dev/input directory
    dir = opendir("/dev/input");
    if (!dir) {
        free(*devices);
        *devices = NULL;
        return -1;
    }
    
    // Iterate through all event* devices
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "event", 5) == 0) {
            char path[PATH_MAX];
            snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);
            
            // Get device name
            char name[256] = {0};
            if (get_device_name(path, name, sizeof(name)) < 0) {
                strcpy(name, "Unknown Device");
            }
            
            // Make sure the name is not empty
            if (name[0] == '\0') {
                strcpy(name, "Unknown Device");
            }
            
            // Check if it's a mouse
            int mouse = is_mouse_device(path);
            
            // Add to our list
            if (count >= capacity) {
                capacity *= 2;
                InputDevice *new_devices = realloc(*devices, capacity * sizeof(InputDevice));
                if (!new_devices) {
                    free(*devices);
                    *devices = NULL;
                    closedir(dir);
                    return -1;
                }
                *devices = new_devices;
            }
            
            strcpy((*devices)[count].path, path);
            strcpy((*devices)[count].name, name);
            (*devices)[count].is_mouse = mouse;
            count++;
        }
    }
    
    closedir(dir);
    return count;
}

// Function to free the devices array
void free_input_devices(InputDevice *devices, int count) {
    (void)count; // Unused parameter
    free(devices);
}

// Function to find a device by name
char* find_device_by_name(const char *device_name) {
    InputDevice *devices;
    int count = list_input_devices(&devices);
    
    if (count <= 0) {
        return NULL;
    }
    
    char *result = NULL;
    
    // First try to find an exact match
    for (int i = 0; i < count; i++) {
        // Skip the momentum mouse Trackpad
        if (strstr(devices[i].name, "momentum mouse Trackpad") != NULL) {
            continue;
        }
        
        // Check for exact match
        if (strcmp(devices[i].name, device_name) == 0) {
            result = strdup(devices[i].path);
            debug_log("Found exact match for device '%s' at %s\n", device_name, devices[i].path);
            break;
        }
    }
    
    // If no exact match, try to find a device that contains the full device_name as a substring
    if (result == NULL) {
        for (int i = 0; i < count; i++) {
            // Skip the momentum mouse Trackpad
            if (strstr(devices[i].name, "momentum mouse Trackpad") != NULL) {
                continue;
            }
            
            // Check if the device name contains the full configured name
            if (strstr(devices[i].name, device_name) != NULL) {
                result = strdup(devices[i].path);
                debug_log("Found substring match for device '%s' in '%s' at %s\n", 
                          device_name, devices[i].name, devices[i].path);
                break;
            }
        }
    }
    
    // If still no match, try a more lenient approach - match the first part
    if (result == NULL) {
        size_t best_match_len = 0;
        int best_match_idx = -1;
        
        for (int i = 0; i < count; i++) {
            // Skip the momentum mouse Trackpad
            if (strstr(devices[i].name, "momentum mouse Trackpad") != NULL) {
                continue;
            }
            
            // Find the longest common prefix
            size_t min_len = strlen(device_name) < strlen(devices[i].name) ? 
                             strlen(device_name) : strlen(devices[i].name);
            size_t match_len = 0;
            
            while (match_len < min_len && 
                   device_name[match_len] == devices[i].name[match_len]) {
                match_len++;
            }
            
            // If this is a better match than what we've found so far
            if (match_len > best_match_len) {
                best_match_len = match_len;
                best_match_idx = i;
            }
        }
        
        // If we found a reasonable match (at least 50% of the name)
        if (best_match_idx >= 0 && best_match_len >= strlen(device_name) / 2) {
            result = strdup(devices[best_match_idx].path);
            debug_log("Found partial match for device '%s' in '%s' at %s (matched %zu/%zu chars)\n", 
                      device_name, devices[best_match_idx].name, devices[best_match_idx].path,
                      best_match_len, strlen(device_name));
        }
    }
    
    if (result == NULL) {
        debug_log("No matching device found for '%s'\n", device_name);
    }
    
    free_input_devices(devices, count);
    return result;
}
