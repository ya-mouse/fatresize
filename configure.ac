AC_PREREQ(2.50)
AC_INIT(fatresize, 1.0.2, [mouse@altlinux.org])

AM_INIT_AUTOMAKE
AM_CONFIG_HEADER(config.h)

AM_MAINTAINER_MODE

dnl set some basic variables
BUILD_DATE="`date '+%D'`"
AC_DEFINE_UNQUOTED(BUILD_DATE, "$BUILD_DATE", [The date of the build.])

# Check for programs.
AC_LANG_C
AC_PROG_CC
AC_PROG_INSTALL

# Check headers.
AC_CHECK_HEADERS([ctype.h errno.h stdio.h getopt.h stdarg.h string.h \
		 stdlib.h unistd.h parted/parted.h])

# Check for library functions.
AC_CHECK_FUNCS([memset strtoll])

# Check for LFS
AC_SYS_LARGEFILE
CFLAGS="$CFLAGS -D_FILE_OFFSET_BITS=${ac_cv_sys_file_offset_bits}"

dnl libparted
# hack
LIBS="$LIBS -ldl"
PKG_CHECK_MODULES([PARTED], [libparted >= 1.6.0])
PKG_CHECK_EXISTS([libparted >= 2.4],
		 [AC_DEFINE([LIBPARTED_GT_2_4], [1],
			    [Define if you have at least libparted 2.4.])])
PKG_CHECK_EXISTS([libparted >= 3.1],
		 [PARTED_LIBS="-lparted-fs-resize $PARTED_LIBS"])

AC_SUBST(PARTED_LIBS)

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
