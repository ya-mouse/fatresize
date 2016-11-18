Name: fatresize
Version: 1.0.2
Release: alt4

License: GPLv3
Group: File tools
Summary: The FAT16/FAT32 non-destructive resizer.
URL: http://sourceforge.net/projects/fatresize/

Source: %name-%version.tar.bz2

BuildRequires: libparted-devel >= 1.6.24 libe2fs-devel

%description
The FAT16/FAT32 non-destructive resizer.

%prep
%setup -q

%build
autoreconf -fisv
%configure
%make_build

%install
%makeinstall install

%files
%_sbindir/%name

%changelog
* Tue Sep 20 2005 Kachalov Anton <mouse@altlinux.ru> 1.0.2-alt4
- LFS support

* Mon Sep 19 2005 Kachalov Anton <mouse@altlinux.ru> 1.0.2-alt3
- restore original partition geometry while resizing EVMS partition

* Thu Sep 15 2005 Kachalov Anton <mouse@altlinux.ru> 1.0.2-alt2
- added default name translation of EVMS partitions
- synced resize code with new cmd-line parted utility 1.6.24

* Wed Sep 07 2005 Kachalov Anton <mouse@altlinux.ru> 1.0.2-alt1
- tell k|M|G and ki|Mi|Gi suffixes
- proper filesystem information
- resize partition only for non-EVMS partitions

* Wed Apr 13 2005 Kachalov Anton <mouse@altlinux.ru> 1.0.1-alt1
- removed translation option: fix new geometry boundary

* Mon Apr 11 2005 Anton D. Kachalov <mouse@altlinux.org> 1.0-alt2
- added translating option

* Fri Apr 08 2005 Anton D. Kachalov <mouse@altlinux.org> 1.0-alt1
- first build
