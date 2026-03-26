#!/bin/bash
if command -v apt-get &> /dev/null; then
    apt-get update
    apt-get install -y build-essential libudev-dev libevdev-dev clang-format pkg-config libx11-dev libgtk-3-dev debhelper devscripts libatspi-dev
elif command -v dnf &> /dev/null; then
    dnf install -y make gcc systemd-devel libevdev-devel clang-tools-extra pkgconf libX11-devel gtk3-devel rpm-build rpmdevtools tar which findutils at-spi2-core-devel
else
    echo "Neither apt-get nor dnf found. Please install dependencies manually."
    exit 1
fi
