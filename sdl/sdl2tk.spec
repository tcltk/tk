# This file is the basis for a binary SDL-Tk Linux RPM.

%define version 8.6.6
%define directory /opt/sdltk86

Name:          sdl2tk
Summary:       SDL-Tk graphical toolkit for the Tcl scripting language.
Version:       %{version}
Release:       1
License:       BSD
Group:         Development/Languages
Source:        sdl2tk-%{version}.tar.gz
URL:           http://www.tcl.tk/
Buildroot:     /var/tmp/%{name}%{version}
Requires:      sdltcl86 >= %version, SDL2

%description
The Tcl (Tool Command Language) provides a powerful platform for
creating integration applications that tie together diverse
applications, protocols, devices, and frameworks.  When paired with
the Tk toolkit, Tcl provides the fastest and most powerful way to
create GUI applications that run on PCs, Unix, and Mac OS X.  Tcl
can also be used for a variety of web-related tasks and for creating
powerful command languages for applications.

%prep
%setup -q -n sdl2tk-%{version}

%build
cd sdl
CFLAGS="${RPM_OPT_FLAGS} -DZIPFS_IN_TCL=1" ./configure \
	--prefix=%{directory} \
	--exec-prefix=%{directory} \
	--with-tcl=%{directory}/lib \
	--libdir=%{directory}/lib \
	--mandir=%{directory}/man
make

%install
rm -rf ${RPM_BUILD_ROOT}
cd sdl
make INSTALL_ROOT=${RPM_BUILD_ROOT} install
make INSTALL_ROOT=${RPM_BUILD_ROOT} install-private-headers

%clean
rm -rf ${RPM_BUILD_ROOT}

%files -n sdl2tk86
%defattr(-,root,root)
%{directory}/lib
%{directory}/bin
%{directory}/include
%{directory}/man
