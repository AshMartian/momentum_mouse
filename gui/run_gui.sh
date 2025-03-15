#!/bin/bash
if [ "$(id -u)" -eq 0 ]; then
    # Already root, just run the GUI
    ./momentum_mouse_gui 
else
    # Not root, use pkexec to get elevated privileges
    pkexec env DISPLAY=$DISPLAY XAUTHORITY=$XAUTHORITY $(dirname "$0")/momentum_mouse_gui 
fi
