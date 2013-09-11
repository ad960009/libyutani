Name:       libyutani
Summary:	Input device helper library for wayland compositor
Version:    1.2.0
Release:    1
Group:      System/Libraries
License:    MIT
Source0:    %{name}.tar.gz
ExclusiveArch:  %arm

# Requirements
BuildRequires:  autoconf
BuildRequires:  libtool

BuildRequires:  wayland-devel
BuildRequires:  mtdev-devel
BuildRequires:  libudev-devel

%description
Input device helper library for wayland compositor

%prep
%setup -q -n %{name}

%build
%reconfigure --prefix=/usr
make

%install
mkdir -p %{buildroot}/root
%make_install
cp src/.libs/yt_evdev_example %{buildroot}/root

%files
%defattr(-,root,root,-)
%{_includedir}/*
%{_libdir}/*
/root/yt_evdev_example

