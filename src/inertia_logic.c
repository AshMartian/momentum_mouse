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
    
    // If we're already scrolling and the new input is in the opposite direction,
    // we need to fight against the existing inertia
    if (inertia_active) {
        if ((current_velocity > 0 && delta < 0) || (current_velocity < 0 && delta > 0)) {
            // Opposite direction - reduce existing velocity by a larger factor
            // This makes it easier to cancel existing inertia
            current_velocity *= 0.5;  // Reduce existing velocity by half
            
            // Then add the new input, amplified to make it more responsive
            current_velocity += (double)delta * 15.0;  // Amplify more than usual
        } else {
            // Same direction, just add to current velocity
            current_velocity += (double)delta * 10.0;
        }
    } else {
        // Not active yet, just set the initial velocity
        current_velocity = (double)delta * 10.0;
    }
    
    // Update position
    current_position += (double)delta * 20.0;
    
    inertia_active = 1;
    // Reset the timer on new input
    gettimeofday(&last_time, NULL);
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
    current_velocity *= exp(-friction * dt);
    
    // Update position based on velocity
    double position_delta = current_velocity * dt * 40.0;  // Reduced from 60.0 for smoother scrolling
    current_position += position_delta;
    
    // If velocity is negligible, stop inertia
    if (fabs(current_velocity) < 0.5) {
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
