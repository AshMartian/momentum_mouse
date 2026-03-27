#!/bin/bash
podman run --rm -v "$(pwd):/app:Z" -w /app fedora:latest bash -c "
  dnf install -y git tar findutils &&
  ./setup.sh &&
  rpmdev-setuptree &&
  VERSION=\$(grep 'Version:' momentum_mouse.spec | awk '{print \$2}') &&
  mkdir -p ~/momentum_mouse-\$VERSION &&
  cp -r . ~/momentum_mouse-\$VERSION/ &&
  tar -czvf ~/rpmbuild/SOURCES/momentum_mouse-\$VERSION.tar.gz -C ~ momentum_mouse-\$VERSION &&
  cp momentum_mouse.spec ~/rpmbuild/SPECS/ &&
  rpmbuild -ba ~/rpmbuild/SPECS/momentum_mouse.spec &&
  cp ~/rpmbuild/RPMS/x86_64/momentum_mouse-*.rpm /app/
"
