#!/bin/bash
podman run --rm -v "$(pwd):/app:Z" -w /app ubuntu:latest bash -c "
  apt-get update &&
  ./setup.sh &&
  dpkg-buildpackage -us -uc -b
"