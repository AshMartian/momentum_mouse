#ifndef INERTIA_SCROLLER_H
#define INERTIA_SCROLLER_H

// Original event emitter functions
int setup_virtual_device(void);
int emit_scroll_event(int value);
void destroy_virtual_device(void);

// New multitouch emitter functions
int setup_virtual_multitouch_device(void);
int emit_two_finger_scroll_event(int delta);
void destroy_virtual_multitouch_device(void);

// Inertia logic functions
void update_inertia(int delta);
void process_inertia(void);
void start_inertia(int initial_velocity);
void stop_inertia(void);

// Input capture functions
int initialize_input_capture(const char *device_override);
int capture_input_event(void);

#endif
