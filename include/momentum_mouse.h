#ifndef momentum_mouse_H
#define momentum_mouse_H

// Include the input_event struct definition
#include <linux/input.h>

// Scroll direction enum
typedef enum {
    SCROLL_DIRECTION_TRADITIONAL = 0,  // Wheel up = content up
    SCROLL_DIRECTION_NATURAL = 1       // Wheel up = content down (like touchpads)
} ScrollDirection;

// Scroll axis enum
typedef enum {
    SCROLL_AXIS_VERTICAL = 0,   // Standard vertical scrolling
    SCROLL_AXIS_HORIZONTAL = 1  // Horizontal scrolling
} ScrollAxis;

// Global configuration variables
extern int grab_device;  // Whether to grab the device exclusively
extern ScrollDirection scroll_direction;
extern ScrollAxis scroll_axis;     // Whether to scroll vertically or horizontally
extern int debug_mode;  // Whether to output debug messages
extern int daemon_mode; // Whether to run as a daemon
extern double scroll_sensitivity;  // How much inertia is applied per scroll
extern double scroll_multiplier;   // How much the initial scroll is multiplied
extern double scroll_friction;     // How quickly scrolling slows down (higher = faster stop)
extern int auto_detect_direction;
extern int use_multitouch;
extern double current_velocity;    // Current scrolling velocity
extern double current_position;    // Current scrolling position
extern double max_velocity_factor; // Maximum velocity as a factor of screen dimensions
// Structure to track boundary reset information
typedef struct {
    struct timeval reset_time;
    double reset_velocity;
    double reset_position;
    int reset_direction;
} BoundaryResetInfo;

extern BoundaryResetInfo boundary_reset_info;
extern int boundary_reset_in_progress;
extern struct timeval last_boundary_reset_time;

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
int is_inertia_active(void);
void apply_mouse_friction(int movement_magnitude);

// Input capture functions
int initialize_input_capture(const char *device_override);
int capture_input_event(void);
void cleanup_input_capture(void);

// System settings detection
int detect_scroll_direction(void);

// Configuration file handling
void load_config_file(const char *filename);

// Debug logging
void debug_log(const char *format, ...);

#endif
