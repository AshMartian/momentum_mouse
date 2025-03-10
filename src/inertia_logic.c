#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <sys/time.h>
#include "momentum_mouse.h"

// Forward declaration for function used in this file
extern void end_multitouch_gesture(void);
extern int screen_width;
extern int screen_height;
extern int post_boundary_frames;  // Flag from event_emitter_mt.c

// Use a double for finer precision.
double current_velocity = 0.0;  // Make accessible to other files
static int inertia_active = 0;
static struct timeval last_time = {0};
// Make current_position accessible to other files that need to reset it
double current_position = 0.0; // Keep only this position variable

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
    
    // Apply the initial scroll multiplier to the delta
    delta = (int)(delta * scroll_multiplier);
    
    // Get the current time for timing calculations
    struct timeval now;
    gettimeofday(&now, NULL);
    
    // Check if we're in a boundary reset
    if (boundary_reset_in_progress) {
        // Calculate time since last boundary reset
        double time_since_reset = time_diff_in_seconds(&last_boundary_reset_time, &now);
        
        // If we're in the early stages of a boundary reset (first 100ms)
        if (time_since_reset < 0.1) {
            if (debug_mode) {
                printf("BOUNDARY: Ignoring scroll input during boundary reset (%.3fs after reset)\n", 
                       time_since_reset);
            }
            
            // Don't update velocity during early boundary reset
            // Just update the last_time to prevent time gaps
            last_time = now;
            return;
        }
        
        // For inputs slightly after the reset (100-300ms), blend them in gradually
        if (time_since_reset < 0.3) {
            // Scale the delta based on how far we are into the reset
            double scale_factor = (time_since_reset - 0.1) / 0.2;  // 0.0 to 1.0
            delta = (int)(delta * scale_factor);
            
            if (debug_mode) {
                printf("BOUNDARY: Scaling scroll input during boundary reset by %.2f\n", scale_factor);
            }
            
            // If delta becomes zero after scaling, just update time and return
            if (delta == 0) {
                last_time = now;
                return;
            }
        }
    }
    
    // Check if this is a new scroll sequence or continuing an existing one
    double dt = 0.0;
    if (last_time.tv_sec != 0) {
        dt = time_diff_in_seconds(&last_time, &now);
    }
    last_time = now;
    
    
    // Store the old velocity for smoothing
    double old_velocity = current_velocity;
    
    // Determine if this is a continuation of scrolling in the same direction
    // Use a smaller initial velocity for single scroll events
    double velocity_factor = 15.0 * scroll_sensitivity;  // Increased for faster initial scrolling
    
    if (inertia_active) {
        // If scrolling in the same direction as current velocity and within a short time window
        if (((current_velocity > 0 && delta > 0) || (current_velocity < 0 && delta < 0)) && dt < 0.3) {
            // Enhance the effect for consecutive scrolls in the same direction
            // The multiplier increases with each scroll in the same direction
            // Use a more gradual acceleration curve
            velocity_factor = (15.0 + (fabs(current_velocity) / 5.0)) * scroll_sensitivity;
            
            if (debug_mode) {
                printf("Consecutive scroll in same direction, velocity factor: %.2f\n", velocity_factor);
            }
        } else if ((current_velocity > 0 && delta < 0) || (current_velocity < 0 && delta > 0)) {
            // Opposite direction - completely reset the gesture like we do at boundaries
            if (debug_mode) {
                printf("Direction change detected, ending touch gesture and resetting velocity\n");
            }
            
            // End the current touch gesture
            end_multitouch_gesture();
            
            // Reset velocity completely
            current_velocity = 0.0;
            
            // Set position to a small value in the new direction
            current_position = (delta > 0) ? 10.0 : -10.0;
            
            // Use standard factor for the new direction
            velocity_factor = 15.0 * scroll_sensitivity;
            
            // We've completed a direction change reset
            if (debug_mode) {
                printf("Direction change reset complete\n");
            }
            
            // Return early to skip the rest of the update
            // The next call will start a new gesture in the opposite direction
            return;
        }
    }
    
    // Apply the velocity change
    // Calculate target velocity
    double target_velocity = current_velocity + (double)delta * velocity_factor;
    
    // Smooth the velocity change - blend old and new velocities
    double blend_factor = 0.7;  // 70% new, 30% old
    current_velocity = (target_velocity * blend_factor) + (old_velocity * (1.0 - blend_factor));
    
    // Cap the velocity based on screen dimensions
    double max_velocity;
    if (scroll_axis == SCROLL_AXIS_VERTICAL) {
        max_velocity = screen_height * max_velocity_factor;
    } else {
        max_velocity = screen_width * max_velocity_factor;
    }
    
    // Apply the cap
    if (current_velocity > max_velocity) {
        current_velocity = max_velocity;
        if (debug_mode) {
            printf("Capped velocity to maximum: %.2f\n", max_velocity);
        }
    } else if (current_velocity < -max_velocity) {
        current_velocity = -max_velocity;
        if (debug_mode) {
            printf("Capped velocity to minimum: %.2f\n", -max_velocity);
        }
    }
    
    // Update position - use a smaller factor for smoother initial movement
    // Update position more gradually
    current_position += (double)delta * 10.0 * scroll_sensitivity;
    
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
    // Adjust friction based on sensitivity and scroll_friction
    double friction_factor = (0.03 + (movement_magnitude * 0.0001)) * scroll_friction / sqrt(scroll_sensitivity);
    
    // Cap the friction factor to a lower value
    // Adjust cap based on sensitivity and scroll_friction
    double max_friction = 0.05 * scroll_friction / sqrt(scroll_sensitivity);
    if (friction_factor > max_friction) friction_factor = max_friction;  // Reduced from 0.95
    
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
    static int frame_count = 0;
    
    if (!inertia_active) {
        // Reset frame count when inertia is not active
        frame_count = 0;
        return;
    }
    
    frame_count++;
    
    struct timeval now;
    gettimeofday(&now, NULL);
    double dt = time_diff_in_seconds(&last_time, &now);
    last_time = now;
    
    // Log inertia state every 10 frames
    if (frame_count % 10 == 0) {
        printf("INERTIA: frame=%d, velocity=%.2f, position=%.2f, dt=%.3fs\n", 
               frame_count, current_velocity, current_position, dt);
    }
    
    // If the velocity is negligible, stop the inertia.
    if (fabs(current_velocity) < 0.05) {
        stop_inertia();
        return;
    }
    
    // Emit a synthetic scroll event with the current velocity (rounded to the nearest integer).
    int event_val = (int)round(current_velocity);
    if (emit_scroll_event(event_val) < 0) {
        fprintf(stderr, "Failed to emit scroll event in inertia processing.\n");
    }
    
    // Apply an exponential decay to simulate friction.
    // Use the scroll_friction parameter to control deceleration
    const double friction = 2.0 * scroll_friction;
    current_velocity *= exp(-friction * dt);
    
    // Sleep a little to approximate a 60 FPS update (the dt measurement handles timing accuracy).
    usleep(16000);
}

// This function is similar to process_inertia() but emits multitouch events instead
void process_inertia_mt(void) {
    static int frame_count = 0;
    static int was_active = 0;
    
    if (!inertia_active) {
        // End the touch gesture if we were previously active
        if (was_active) {
            end_multitouch_gesture();
            was_active = 0;
            frame_count = 0;
        }
        return;
    }
    
    // Set active flag for next time
    was_active = 1;
    frame_count++;
    
    // Calculate time delta
    struct timeval now;
    gettimeofday(&now, NULL);
    double dt = time_diff_in_seconds(&last_time, &now);
    last_time = now;
    
    // Log inertia state periodically
    if (debug_mode && frame_count % 20 == 0) {
        printf("INERTIA: frame=%d, velocity=%.2f, position=%.2f, dt=%.3fs\n", 
               frame_count, current_velocity, current_position, dt);
    }
    
    // Apply friction to velocity
    const double friction = 0.8 * scroll_friction / sqrt(scroll_sensitivity);
    double old_velocity = current_velocity;
    current_velocity *= exp(-friction * dt);
    
    if (debug_mode && fabs(old_velocity - current_velocity) > 5.0) {
        printf("Time-based friction: dt=%.3fs, friction=%.2f, velocity: %.2f -> %.2f\n", 
               dt, friction, old_velocity, current_velocity);
    }
    
    // Calculate position delta based on velocity
    double position_delta = current_velocity * dt * 80.0 * scroll_sensitivity;
    
    // Fix direction after boundary reset if needed
    if (boundary_reset_in_progress && post_boundary_frames > 15) {
        int expected_direction = boundary_reset_info.reset_direction;
        int actual_direction = (position_delta > 0) ? 1 : -1;
        
        if (expected_direction != actual_direction && position_delta != 0) {
            if (debug_mode) {
                printf("DIRECTION-FIX: Correcting position_delta from %.2f to %.2f\n",
                       position_delta, -position_delta);
            }
            position_delta = -position_delta;
            current_velocity = -current_velocity;
        }
    }
    
    // Handle post-boundary transition - simplified for faster response
    if (post_boundary_frames > 0) {
        post_boundary_frames = 0;
        boundary_reset_in_progress = 0;
        
        if (debug_mode) {
            printf("POST-BOUNDARY: Skipping transition frames for faster response\n");
        }
    }
    
    // Cap position delta to prevent large jumps
    double screen_size = (scroll_axis == SCROLL_AXIS_VERTICAL) ? screen_height : screen_width;
    double abs_velocity = fabs(current_velocity);
    double max_delta_factor = 0.3;  // Default 30% of screen size
    
    if (abs_velocity > 500) {
        max_delta_factor = 0.15;
    } else if (abs_velocity > 300) {
        max_delta_factor = 0.2;
    }
    
    double max_delta = screen_size * max_delta_factor;
    if (fabs(position_delta) > max_delta) {
        double direction = (position_delta > 0) ? 1.0 : -1.0;
        position_delta = direction * max_delta;
        if (debug_mode) {
            printf("POSITION: Capped delta to %.2f\n", position_delta);
        }
    }
    
    // Update position
    current_position += position_delta;
    
    // Stop inertia if velocity is too low
    if (fabs(current_velocity) < 0.025) {
        if (debug_mode) {
            printf("Velocity too low (%.2f), stopping inertia\n", current_velocity);
        }
        stop_inertia();
        return;
    }
    
    // Emit scroll event
    int event_val = (int)round(position_delta);
    if (event_val != 0) {
        if (debug_mode && (abs(event_val) > 10 || frame_count % 20 == 0)) {
            printf("EMIT: Sending event with delta=%d (from position_delta=%.2f)\n", 
                   event_val, position_delta);
        }
        
        if (emit_two_finger_scroll_event(event_val) < 0) {
            fprintf(stderr, "Failed to emit multitouch scroll event in inertia processing.\n");
            stop_inertia();
            return;
        }
    }
    
    // Maintain ~60 FPS
    usleep(16000);
}
