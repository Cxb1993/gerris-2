%define	alphatag %(date +%Y%m%d)
%define current %(gerris2D -V 2>&1 | head -1 | cut -d' ' -f6)

Summary: The Gerris Flow Solver
Name: gerris
%if "%{current}" == ""
Version: 1.2.0
%else
Version: %{current}
%endif
Release: 7.%{alphatag}cvs%{?dist}
License: GPLv2
# SuSE should have this macro set. If doubt specify in ~/.rpmmacros
%if 0%{?suse_version}
Group: Productivity/Scientific/Other
%endif
# For Fedora you must specify fedora_version in your ~/.rpmmacros file
%if 0%{?fedora_version}
Group: Applications/Engineering
%endif
URL: http://gfs.sourceforge.net
Packager: Ivan Adam Vari <i.vari@niwa.co.nz>
Source0: %{name}-stable.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
%if 0%{?fedora_version}
Requires: proj gsl netcdf
BuildRequires: netcdf-devel proj-devel
%endif
%if 0%{?suse_version}
Requires: libproj0 gsl libnetcdf-4
BuildRequires: libnetcdf-devel libproj-devel
%endif
# For both distros
Requires: gts-snapshot-devel >= 0.7.6 pkgconfig gcc sed gawk m4
BuildRequires: glibc-devel automake libtool gsl-devel gts-snapshot-devel >= 0.7.6


%description
Gerris is an Open Source Free Software library for the solution of the 
partial differential equations describing fluid flow. The source code 
is available free of charge under the Free Software GPL license.
Gerris is supported by NIWA (National Institute of Water and Atmospheric
research) and by the Marsden Fund of the Royal Society of New Zealand.
The code is written entirely in C and uses both the GLib Library and the
GTS Library for geometrical functions and object-oriented programming. 


%prep
%setup -q -n %{name}-stable


%build
RPM_OPT_FLAGS="$RPM_OPT_FLAGS -fPIC -DPIC"
%if 0%{?suse_version}
if [ -x ./configure ]; then
    CFLAGS="$RPM_OPT_FLAGS" ./configure \
	--prefix=%{_prefix} \
	--libdir=%{_prefix}/%_lib \
	--disable-mpi \
	--disable-static
else
    CFLAGS="$RPM_OPT_FLAGS" sh autogen.sh \
	--prefix=%{_prefix} \
	--libdir=%{_prefix}/%_lib \
	--disable-mpi \
	--disable-static
fi
%endif
%if 0%{?fedora_version}
if [ -x ./configure ]; then
    CFLAGS="$RPM_OPT_FLAGS" \
    CPPFLAGS="-I%{_includedir}/netcdf-3" ./configure \
	--prefix=%{_prefix} \
	--libdir=%{_prefix}/%_lib \
	--disable-mpi \
	--disable-static
else
    CFLAGS="$RPM_OPT_FLAGS" \
    CPPFLAGS="-I%{_includedir}/netcdf-3" sh autogen.sh \
	--prefix=%{_prefix} \
	--libdir=%{_prefix}/%_lib \
	--disable-mpi \
	--disable-static
fi
%endif

%{__make}


%install
rm -rf $RPM_BUILD_ROOT
mkdir $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT

# Comply shared library policy
find $RPM_BUILD_ROOT -name *.la -exec rm -f {} \;

# Comply static build policy
find $RPM_BUILD_ROOT -name *.a -exec rm -f {} \;


%clean
rm -rf $RPM_BUILD_ROOT


%post
/sbin/ldconfig
touch --no-create %{_datadir}/icons/hicolor
if [ -x %{_bindir}/gtk-update-icon-cache ]; then
 %{_bindir}/gtk-update-icon-cache --quiet %{_datadir}/icons/hicolor || :
elif [ -x /opt/gnome/bin/gtk-update-icon-cache ]; then
 /opt/gnome/bin/gtk-update-icon-cache -t --quiet %{_datadir}/icons/hicolor || :
fi
update-mime-database %{_datadir}/mime &> /dev/null || :


%postun
/sbin/ldconfig
touch --no-create %{_datadir}/icons/hicolor
if [ -x %{_bindir}/gtk-update-icon-cache ]; then
 %{_bindir}/gtk-update-icon-cache --quiet %{_datadir}/icons/hicolor || :
elif [ -x /opt/gnome/bin/gtk-update-icon-cache ]; then
 /opt/gnome/bin/gtk-update-icon-cache -t --quiet %{_datadir}/icons/hicolor || :
fi
update-mime-database %{_datadir}/mime &> /dev/null || :


%files
%defattr(-,root,root)
%doc NEWS README TODO COPYING
%{_bindir}/*
%{_includedir}/*.h
%{_includedir}/gerris/*.h
%{_libdir}/*.so.*
%{_libdir}/*.so
%{_libdir}/gerris/*
%{_libdir}/pkgconfig/*.pc
%{_datadir}/gerris/gfs.lang
%{_datadir}/mime/packages/*.xml
%{_datadir}/icons/hicolor/48x48/mimetypes/*.png


%changelog
* Thu Jul 3 2008 Ivan Adam Vari <i.vari@niwa.co.nz> - 7
- Fixed typo in %files section (attr)
- Added new file gfs.lang to %files section
- Disabled MPI according to debian build rules

* Thu May 15 2008 Ivan Adam Vari <i.vari@niwa.co.nz> - 6
- Added fedora 8 support for x86 (32bit only)
- Removed libtool config files to comply with shared
  library policy
- Removed static build bits to comply with shared
  library policy
- Fixed dependencies

* Mon May 12 2008 Ivan Adam Vari <i.vari@niwa.co.nz> - 5
- Added new package dependencies, minor fixes

* Mon Jan 7 2008 Ivan Adam Vari <i.vari@niwa.co.nz> - 4
- Removed %{?_smp_mflags} from make due to intermittent
  build errors on some SMP systems

* Mon Nov 12 2007 Ivan Adam Vari <i.vari@niwa.co.nz> - 3
- Fixed package (install) dependencies

* Mon Oct 1 2007 Ivan Adam Vari <i.vari@niwa.co.nz> - 2
- Removed unnecessary version specifications for some
  build requirements
- Added SLEx/SuSE compatibilty
- Added 64bit compatibility
- Updated %post, %postun scriptlets

* Tue May 1 2007 Ivan Adam Vari <i.vari@niwa.co.nz> - 1
- Initial rpm release based on Fedora/Redhat Linux
