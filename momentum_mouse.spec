Name:           momentum_mouse
Version:        1.0.0
Release:        1%{?dist}
Summary:        Smooth scrolling daemon for Linux

License:        MIT
URL:            https://github.com/AshMartian/momentum_mouse
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  systemd-devel
BuildRequires:  libevdev-devel
BuildRequires:  libX11-devel
BuildRequires:  gtk3-devel
BuildRequires:  systemd-rpm-macros

Requires:       libevdev
Requires:       libudev
Requires:       gtk3
Requires:       polkit

%description
Brings smooth, natural scrolling to Linux desktop environments.
It transforms the abrupt, jerky scrolling experience typical of mouse wheels
into a fluid, momentum-based scrolling experience.

%prep
%autosetup

%build
make %{?_smp_mflags}
make -C gui %{?_smp_mflags}

%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT prefix=/usr sysconfdir=/etc systemddir=%{_unitdir}

%files
%{_bindir}/momentum_mouse
%{_bindir}/momentum_mouse_gui
%{_bindir}/momentum_mouse_gui_launcher
%config(noreplace) /etc/momentum_mouse.conf
%{_unitdir}/momentum_mouse.service
/usr/share/polkit-1/actions/org.momentum_mouse.gui.policy
/usr/share/applications/momentum_mouse_gui.desktop
/usr/share/icons/hicolor/256x256/apps/momentum_mouse.png

%post
%systemd_post momentum_mouse.service

%preun
%systemd_preun momentum_mouse.service

%postun
%systemd_postun_with_restart momentum_mouse.service

%changelog
* Wed Mar 25 2026 Ash Martian <ash@example.com> - 1.0.0-1
- Initial packaging for Fedora
