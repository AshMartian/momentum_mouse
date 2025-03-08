#ifndef INERTIA_SCROLLER_H
#define INERTIA_SCROLLER_H

// Include the input_event struct definition
#include <linux/input.h>

// Scroll direction enum
typedef enum {
    SCROLL_DIRECTION_TRADITIONAL = 0,  // Wheel up = content up
    SCROLL_DIRECTION_NATURAL = 1       // Wheel up = content down (like touchpads)
} ScrollDirection;

// Global configuration variables
extern int grab_device;  // Whether to grab the device exclusively
extern ScrollDirection scroll_direction;

// Original event emitter functions
int setup_virtual_device(void);
int emit_scroll_event(int value);
int emit_passthrough_event(struct input_event *ev);
void destroy_virtual_device(void);

// New multitouch emitter functions
int setup_virtual_multitouch_device(void);
int emit_two_finger_scroll_event(int delta);
void end_multitouch_gesture(void);
void destroy_virtual_multitouch_device(void);

// Inertia logic functions
void update_inertia(int delta);
void process_inertia(void);
void process_inertia_mt(void);
void start_inertia(int initial_velocity);
void stop_inertia(void);

// Input capture functions
int initialize_input_capture(const char *device_override);
int capture_input_event(void);
void cleanup_input_capture(void);

// System settings detection
int detect_scroll_direction(void);

#endif
