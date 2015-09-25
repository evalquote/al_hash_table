#
# Process this file with autoconf to produce a configure script

AC_PREREQ([2.63])

AC_INIT([alhash], [1.0.0])
AM_INIT_AUTOMAKE
AM_MAINTAINER_MODE

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_EXEEXT
AM_PROG_LIBTOOL

# Library Version
LTVERSION="1:0:0"
AC_SUBST(LTVERSION)

# Checks for libraries.
AC_CHECK_FUNCS([strlcpy])

# Checks for header files.

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.

dnl
dnl C comipiler flags
dnl
AC_ARG_ENABLE(
	debug,
		[ --enable-debug=ARG debugging mode ],
		[
			if test "$enableval" = "yes" ; then
			   CFLAGS="-O0 -g -Wall -DDEBUG"
                        else
			   CFLAGS="-O0 -g -Wall -DDEBUG=$enableval"
			fi
		],
		[
			CFLAGS="-O3 -g -Wall -DNDEBUG"
dnl			CFLAGS="-static -pg -O3 -g -Wall -DNDEBUG"
dnl			CFLAGS="-fno-inline -static -pg -O3 -g -Wall -DNDEBUG"
		])

AC_OUTPUT([Makefile
	   lib/Makefile
	   sample/Makefile])
