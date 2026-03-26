#!/bin/bash
IS_DARK=0
if command -v gsettings &> /dev/null; then
    SCHEME=$(gsettings get org.gnome.desktop.interface color-scheme 2>/dev/null)
    if [[ "$SCHEME" == *"prefer-dark"* ]]; then IS_DARK=1; fi
fi

DIR="$(cd "$(dirname "$(readlink -f "$0" || echo "$0")")" && pwd)"
GUI_BIN="$DIR/momentum_mouse_gui"

if [ ! -x "$GUI_BIN" ] && [ -x "/usr/bin/momentum_mouse_gui" ]; then
    GUI_BIN="/usr/bin/momentum_mouse_gui"
elif [ ! -x "$GUI_BIN" ]; then
    GUI_BIN="momentum_mouse_gui"
fi

if [ "$(id -u)" -eq 0 ]; then
    MOMENTUM_DARK_MODE=$IS_DARK "$GUI_BIN"
else
    pkexec env DISPLAY="$DISPLAY" XAUTHORITY="$XAUTHORITY" MOMENTUM_DARK_MODE=$IS_DARK "$GUI_BIN"
fi
