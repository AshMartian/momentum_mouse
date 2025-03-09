#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <sys/time.h>
#include "inertia_scroller.h"

// Forward declaration for function used in this file
extern void end_multitouch_gesture(void);

// Use a double for finer precision.
static double current_velocity = 0.0;
static int inertia_active = 0;
static struct timeval last_time = {0};
static double current_position = 0.0; // Keep only this position variable

// Helper to get time difference in seconds between two timeval structs.
static double time_diff_in_seconds(struct timeval *start, struct timeval *end) {
    double seconds = (double)(end->tv_sec - start->tv_sec);
    double microseconds = (double)(end->tv_usec - start->tv_usec) / 1000000.0;
    return seconds + microseconds;
}

// Called when a new physical scroll event is captured.
// This updates the current velocity based on incoming scroll events.
void update_inertia(int delta) {
    // Invert delta for natural scrolling
    if (scroll_direction == SCROLL_DIRECTION_NATURAL) {
        delta = -delta;
    }
    
    // Get the current time for timing calculations
    struct timeval now;
    gettimeofday(&now, NULL);
    
    // Check if this is a new scroll sequence or continuing an existing one
    double dt = 0.0;
    if (last_time.tv_sec != 0) {
        dt = time_diff_in_seconds(&last_time, &now);
    }
    last_time = now;
    
    
    // Determine if this is a continuation of scrolling in the same direction
    // Use a smaller initial velocity for single scroll events
    double velocity_factor = 5.0;  // Reduced from 10.0 for smaller initial effect
    
    if (inertia_active) {
        // If scrolling in the same direction as current velocity and within a short time window
        if (((current_velocity > 0 && delta > 0) || (current_velocity < 0 && delta < 0)) && dt < 0.3) {
            // Enhance the effect for consecutive scrolls in the same direction
            // The multiplier increases with each scroll in the same direction
            velocity_factor = 8.0 + (fabs(current_velocity) / 5.0);  // Grows with existing velocity
            
            if (debug_mode) {
                printf("Consecutive scroll in same direction, velocity factor: %.2f\n", velocity_factor);
            }
        } else if ((current_velocity > 0 && delta < 0) || (current_velocity < 0 && delta > 0)) {
            // Opposite direction - reduce existing velocity by a larger factor
            // This makes it easier to cancel existing inertia
            current_velocity *= 0.5;  // Reduce existing velocity by half
            
            if (debug_mode) {
                printf("Direction change, reducing velocity by half\n");
            }
            
            // Then add the new input, with normal responsiveness
            velocity_factor = 10.0;  // Use standard factor for direction changes
        }
    }
    
    // Apply the velocity change
    current_velocity += (double)delta * velocity_factor;
    
    // Update position - use a smaller factor for smoother initial movement
    current_position += (double)delta * 15.0;  // Reduced from 20.0
    
    if (debug_mode) {
        printf("Updated velocity: %.2f, position: %.2f\n", current_velocity, current_position);
    }
    
    inertia_active = 1;
}

// Optionally, explicitly start inertia with an initial velocity.
void start_inertia(int initial_velocity) {
    current_velocity = (double)initial_velocity;
    inertia_active = 1;
    gettimeofday(&last_time, NULL);
}

// Call this to cancel any ongoing inertia fling.
void stop_inertia(void) {
    current_velocity = 0.0;
    inertia_active = 0;
    last_time.tv_sec = 0;
    last_time.tv_usec = 0;
    end_multitouch_gesture();  // End the touch gesture when inertia stops
}

// Check if inertia is currently active
int is_inertia_active(void) {
    return inertia_active;
}

// Apply friction based on mouse movement
void apply_mouse_friction(int movement_magnitude) {
    if (!inertia_active) {
        return;
    }
    
    // Calculate friction factor based on movement magnitude
    // Make it much gentler - small movements = very small friction
    double friction_factor = 0.03 + (movement_magnitude * 0.0001);
    
    // Cap the friction factor to a much lower value
    if (friction_factor > 0.05) friction_factor = 0.05;  // Reduced from 0.95
    
    double old_velocity = current_velocity;
    
    // Apply the friction by reducing velocity
    current_velocity *= (1.0 - friction_factor);
    
    if (debug_mode && movement_magnitude > 10) {
        printf("Mouse friction: movement=%d, factor=%.3f, velocity: %.2f -> %.2f\n", 
               movement_magnitude, friction_factor, old_velocity, current_velocity);
    }
    
    // Only stop inertia if velocity becomes extremely small
    if (fabs(current_velocity) < 0.05) {
        if (debug_mode) {
            printf("Velocity too low (%.2f), stopping inertia\n", current_velocity);
        }
        stop_inertia();
    }
    
    // Update the last time to prevent time-based friction from being applied immediately
    gettimeofday(&last_time, NULL);
}

// This function should be called periodically in the main loop.
// It emits synthetic scroll events based on the current velocity and decays that velocity over time.
void process_inertia(void) {
    if (!inertia_active) {
        return;
    }
    
    struct timeval now;
    gettimeofday(&now, NULL);
    double dt = time_diff_in_seconds(&last_time, &now);
    last_time = now;
    
    // If the velocity is negligible, stop the inertia.
    if (fabs(current_velocity) < 0.5) {
        stop_inertia();
        return;
    }
    
    // Emit a synthetic scroll event with the current velocity (rounded to the nearest integer).
    int event_val = (int)round(current_velocity);
    if (emit_scroll_event(event_val) < 0) {
        fprintf(stderr, "Failed to emit scroll event in inertia processing.\n");
    }
    
    // Apply an exponential decay to simulate friction.
    // Adjust the friction coefficient (here set to 2.0) to tune deceleration.
    const double friction = 2.0;
    current_velocity *= exp(-friction * dt);
    
    // Sleep a little to approximate a 60 FPS update (the dt measurement handles timing accuracy).
    usleep(16000);
}

// This function is similar to process_inertia() but emits multitouch events instead
void process_inertia_mt(void) {
    static int was_active = 0;
    
    if (!inertia_active) {
        // If we were previously active but now we're not, end the touch gesture
        if (was_active) {
            end_multitouch_gesture();
            was_active = 0;
        }
        return;
    }
    was_active = 1;
    
    
    struct timeval now;
    gettimeofday(&now, NULL);
    double dt = time_diff_in_seconds(&last_time, &now);
    last_time = now;
    
    // Apply friction to velocity - use a gentler friction for smoother scrolling
    const double friction = 1.2;  // Reduced from 1.5
    double old_velocity = current_velocity;
    
    current_velocity *= exp(-friction * dt);
    
    if (debug_mode && fabs(old_velocity - current_velocity) > 1.0) {
        printf("Time-based friction: dt=%.3fs, velocity: %.2f -> %.2f\n", 
               dt, old_velocity, current_velocity);
    }
    
    // Update position based on velocity
    double position_delta = current_velocity * dt * 40.0;  // Reduced from 60.0 for smoother scrolling
    current_position += position_delta;
    
    // If velocity is negligible, stop inertia
    if (fabs(current_velocity) < 0.5) {
        if (debug_mode) {
            printf("Velocity too low (%.2f), stopping inertia\n", current_velocity);
        }
        stop_inertia();
        return;
    }
    
    // Emit a synthetic multitouch scroll event with the position delta
    int event_val = (int)round(position_delta);
    
    // Only emit if there's a change
    if (event_val != 0) {
        if (emit_two_finger_scroll_event(event_val) < 0) {
            fprintf(stderr, "Failed to emit multitouch scroll event in inertia processing.\n");
            // If we failed to emit, stop inertia to prevent further errors
            stop_inertia();
            return;
        }
    }
    
    // Sleep a little to approximate a 60 FPS update (the dt measurement handles timing accuracy).
    usleep(16000);
}
