Name:           aperi
Version:        0.11.0~pre
Release:        1%{?dist}
Summary:        Simple resource opener based on file extensions and URI schemas

License:        MIT
URL:            https://codeberg.org/mbeniamino/aperi
# Local builds: create the tarball from the git repository with
#   git archive --prefix=aperi-%%{version}/ -o aperi-%%{version}.tar.gz HEAD
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  meson
BuildRequires:  pkgconfig(dbus-1)
BuildRequires:  systemd-rpm-macros
BuildRequires:  desktop-file-utils

Recommends:     xdg-desktop-portal
Suggests:       python3
Suggests:       wl-clipboard

%description
aperi opens a file, directory or URI with the program associated to its
extension, URI schema or type by a simple configuration file. It can be
used in place of xdg-open and as the default application of the desktop.

The package also contains app-chooser, to open a file with the desktop
application chooser; aperi-session, a session helper providing a minimal
org.freedesktop.FileManager1 D-Bus service and removing the file
associations created by wine; and aperi-setup, to configure a user account
to use aperi.

%prep
%autosetup -n %{name}-%{version}

%build
# The unit directory is passed explicitly, so that systemd.pc is not needed
%meson -Ddbus=enabled -Dsystemduserunitdir=%{_userunitdir}
%meson_build

%install
%meson_install

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/aperi.desktop
%meson_test

# Applies the user presets: aperi-session is not enabled by default, users
# opt in with systemctl --user enable or aperi-setup
%post
%systemd_user_post aperi-session.service

%preun
%systemd_user_preun aperi-session.service

%files
%license LICENSE
%{_bindir}/aperi
%{_bindir}/aperi-session
%{_bindir}/aperi-setup
%{_bindir}/app-chooser
%{_userunitdir}/aperi-session.service
%{_datadir}/applications/aperi.desktop
%{_mandir}/man1/aperi.1*
%{_mandir}/man1/aperi-session.1*
%{_mandir}/man1/aperi-setup.1*
%{_mandir}/man1/app-chooser.1*
# README.md, Changelog and the examples read by aperi-setup
%{_docdir}/%{name}/

%changelog
* Sun Nov 15 2026 Matteo Beniamino <m.beniamino@tautologica.org> - 0.11.0~pre-1
- aperi-session replaces aperi_fm1 and wipewine
- Added aperi-setup, man pages and RPM packaging
