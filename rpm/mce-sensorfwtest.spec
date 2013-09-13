Name:       mce-sensorfwtest
Summary:    Mode Control Entity for Nokia mobile computers
Version:    1.14.1
Release:    1
Group:      System/System Control
License:    LGPLv2
URL:        https://github.com/nemomobile/mce
Source0:    %{name}-%{version}.tar.bz2
# Patches auto-generated by git-buildpackage:
Requires:   dsme
Requires:   systemd
Requires(preun): systemd
Requires(post): systemd
Requires(postun): systemd
BuildRequires:  pkgconfig(dbus-1) >= 1.0.2
BuildRequires:  pkgconfig(dbus-glib-1)
BuildRequires:  pkgconfig(dsme) >= 0.58
BuildRequires:  pkgconfig(gconf-2.0)
BuildRequires:  pkgconfig(glib-2.0) >= 2.18.0
BuildRequires:  pkgconfig(mce) >= 1.12.4
BuildRequires:  pkgconfig(libsystemd-daemon)
BuildRequires:  kernel-headers >= 2.6.32
BuildRequires:  systemd
# systemd has /etc/rpm/macros.systemd

%description
This package contains the Mode Control Entity which provides
mode management features.  This is a daemon that is the backend
for many features on Nokia's mobile computers.

%package tools
Summary:    Tools for interacting with mce
Group:      Development/Tools
Requires:   %{name} = %{version}-%{release}

%description tools
This package contains tools that can be used to interact with
the Mode Control Entity and to get mode information.

%prep
%setup -q -n %{name}-%{version}

%build
make %{?jobs:-j%jobs} mce-test

%install
rm -rf %{buildroot}
# FIXME: we need a configure script ... for now pass dirs in make install
mkdir -p %{buildroot}/%{_sbindir}
cp mce-test %{buildroot}/%{_sbindir}/%{name}

%files
%defattr(-,root,root,-)
%{_sbindir}/%{name}