#!/bin/bash
IS_DARK=0
if command -v gsettings &> /dev/null; then
    SCHEME=$(gsettings get org.gnome.desktop.interface color-scheme 2>/dev/null)
    if [[ "$SCHEME" == *"prefer-dark"* ]]; then IS_DARK=1; fi
fi
if [ "$(id -u)" -eq 0 ]; then
    # Already root, just run the GUI
    MOMENTUM_DARK_MODE=$IS_DARK ./momentum_mouse_gui 
else
    # Not root, use pkexec to get elevated privileges
    pkexec env DISPLAY=$DISPLAY XAUTHORITY=$XAUTHORITY MOMENTUM_DARK_MODE=$IS_DARK $(dirname "$0")/momentum_mouse_gui 
fi
