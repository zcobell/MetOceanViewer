# $Id: configure.ac 6280 2016-02-23 00:33:39Z flaterco $

# Int types macros require autoconf 2.60 or later.
AC_PREREQ([2.60])

# autoconf 2.65 (Slackware 13.1) has a regression that causes the INT*_T
# tests to fail improperly.  See email from Eric Blake to the bug-autoconf
# mailing list on 2010-02-06.
m4_if(2.65, AC_AUTOCONF_VERSION, [m4_fatal([autoconf version 2.65 is broken and unusable])])

# FIXME: Update version info on each release
AC_INIT([XTide], [2.15.1], [dave@flaterco.com])

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([libxtide/autoconf-defines.h])
AM_INIT_AUTOMAKE([-Wall -Werror])

AC_LANG([C++])
AC_PROG_CXX
AM_PROG_LEX
AC_PROG_YACC
m4_ifdef([AM_PROG_AR], [AM_PROG_AR])
AC_PROG_LIBTOOL
AC_REQUIRE_CPP

AC_SUBST([ADDED_CPPFLAGS])
AC_SUBST([ADDED_CXXFLAGS])

# Extended initializer lists were added in 4.4.
# Clang fraudulently identifies itself as an old GCC.
AC_MSG_CHECKING([if using a version of GCC older than 4.4])
AC_PREPROC_IFELSE(
  [AC_LANG_SOURCE([[
#ifndef __clang__
#ifdef __GNUC__
#if (__GNUC__ * 100 + __GNUC_MINOR__) < 404
#FAIL
#endif
#endif
#endif
  ]])],
  [
    AC_MSG_RESULT(no)
  ],
  [
    AC_MSG_RESULT(yes)
    AC_MSG_NOTICE([BIG UGLY WARNING: GCC older than version 4.4 is UNSUPPORTED])
    AC_MSG_NOTICE([BIG UGLY WARNING: whatever problems you encounter, step 1 is UPGRADE GCC])
    AC_MSG_NOTICE([BIG UGLY WARNING: please do not submit bug reports with old GCC])
  ]
)

# /tmp/ccB6kF2d.s: Assembler messages:
# /tmp/ccB6kF2d.s:1325: Error: symbol `_ZZN10SafeVectorIdEixEPdS1_jE19__PRETTY_FUNCTION__' is already defined
# /tmp/ccB6kF2d.s:1330: Error: symbol `_ZZN10SafeVectorI5AngleEixEPS0_S2_jE19__PRETTY_FUNCTION__' is already defined
# __GNUC_PATCHLEVEL__ is available since GCC 3.0
AC_MSG_CHECKING([if using GCC 4.5.0])
AC_PREPROC_IFELSE(
  [AC_LANG_SOURCE([[
#ifdef __GNUC__
#if __GNUC__ == 4 && __GNUC_MINOR__ == 5 && __GNUC_PATCHLEVEL__ == 0
#FAIL
#endif
#endif
  ]])],
  [
    AC_MSG_RESULT(no)
  ],
  [
    AC_MSG_RESULT(yes)
    AC_MSG_NOTICE([oh woe])
    AC_MSG_NOTICE([disabling ipa-sra optimization to work around GCC Bug @%:@43905])
    AC_MSG_NOTICE([(see http://gcc.gnu.org/bugzilla/show_bug.cgi?id=43905)])
    ADDED_CXXFLAGS="${ADDED_CXXFLAGS} -fno-ipa-sra"
  ]
)

# This tests for the presence of a fault that prevents GCC 3.3.6 from
# compiling HarmonicsFile.cc.
AC_MSG_CHECKING([if C++ compiler has an old GCC fault])
AC_COMPILE_IFELSE(
  [AC_LANG_SOURCE([[
    class ChuckingRate {public: ChuckingRate (const char *rate)           {}};
    class Woodchuck    {public: Woodchuck    (const ChuckingRate &rate)   {}};
    class Groundhog    {public: Groundhog    (const Woodchuck &woodchuck) {}};
    Groundhog phil (Woodchuck (ChuckingRate ("About a cord a day")));
  ]])],
  [
    AC_MSG_RESULT(no)
  ],
  [
    AC_MSG_RESULT(yes)
    AC_MSG_NOTICE([this compiler will never work; try a recent GCC instead])
    AC_MSG_ERROR([compiler found unworthy])
  ]
)

# Ensure that STL is present.  (Reportedly not found without CPPFLAGS
# under Mac OS X 10.4.9)
AC_CHECK_HEADER([vector], [], [
  AC_MSG_NOTICE([[Not finding the Standard Template Library.]])
  AC_MSG_NOTICE([[Find a directory like /usr/include/c++/[...numbers...],]])
  AC_MSG_NOTICE([[then do ./configure CPPFLAGS="-I/usr/include/...".]])
  AC_MSG_ERROR([cannot find Standard Template Library; try setting CPPFLAGS.])
])

SAVED_CXXFLAGS="${CXXFLAGS}"
AC_MSG_CHECKING([if C++ compiler can handle simple extended initializer lists])
AC_COMPILE_IFELSE(
  [AC_LANG_PROGRAM([[#include <vector>]],
                   [[std::vector<int> test = {1}]])],
  [
    AC_MSG_RESULT(yes)
  ],
  [
    AC_MSG_RESULT(no)
    AC_MSG_CHECKING([if it helps to add -std=gnu++0x to CXXFLAGS])
    CXXFLAGS="-std=gnu++0x ${SAVED_CXXFLAGS}"
    AC_COMPILE_IFELSE(
      [AC_LANG_PROGRAM([[#include <vector>]],
		       [[std::vector<int> test = {1}]])],
      [
	AC_MSG_RESULT(yes)
        ADDED_CXXFLAGS="${ADDED_CXXFLAGS} -std=gnu++0x"
      ],
      [
	AC_MSG_RESULT(no)
        # Clang needs -std=c++11 instead
	AC_MSG_CHECKING([if it helps to add -std=c++11 to CXXFLAGS])
	CXXFLAGS="-std=c++11 ${SAVED_CXXFLAGS}"
	AC_COMPILE_IFELSE(
	  [AC_LANG_PROGRAM([[#include <vector>]],
			   [[std::vector<int> test = {1}]])],
	  [
	    AC_MSG_RESULT(yes)
	    ADDED_CXXFLAGS="${ADDED_CXXFLAGS} -std=c++11"
	  ],
	  [
	    AC_MSG_RESULT(no)
	    AC_DEFINE([NO_INITIALIZER_LISTS],1,[Define to use run-time initialization workaround for completely missing initializer list support])
	    SKIP_COMPLICATED=1
            CXXFLAGS="${SAVED_CXXFLAGS}"
	  ]
	)
      ]
    )
  ]
)
if test -z "${SKIP_COMPLICATED}"; then
  # Workaround for gcc 4.4 series.
  AC_MSG_CHECKING([if C++ compiler can handle complicated extended initializer lists])
  AC_COMPILE_IFELSE(
    [AC_LANG_PROGRAM([[#include <vector>
		       struct ChuckDay  { unsigned chuckingRate; };
		       struct Woodchuck { std::vector<ChuckDay> days; };]],
		     [[Woodchuck phil ({{{1}}});]])],
    [
      AC_MSG_RESULT(yes)
    ],
    [
      AC_MSG_RESULT(no)
      AC_MSG_CHECKING([if it helps to use compound literals])
      AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM([[#include <vector>
			   struct ChuckDay  { unsigned chuckingRate; };
			   struct Woodchuck { std::vector<ChuckDay> days; };]],
			 [[Woodchuck phil ({{(ChuckDay){1}}});]])],
	[
	  AC_MSG_RESULT(yes)
	  AC_DEFINE([BROKEN_INITIALIZER_LISTS],1,[Define to use compound literals workaround for broken initializer list support])
	],
	[
	  AC_MSG_RESULT(no)
	  AC_DEFINE([NO_INITIALIZER_LISTS],1,[Define to use run-time initialization workaround for completely missing initializer list support])
	]
      )
    ]
  )
fi

# SafeVector.hh needs an initializer_list constructor if and only if
# std::vector has such a constructor.  This is almost redundant with the
# previous initializer list tests but they might succeed using some data type
# that is not called initializer_list.
AC_MSG_CHECKING([if std::vector has an initializer_list constructor])
AC_COMPILE_IFELSE(
  [AC_LANG_PROGRAM([[#include <vector>
                    template <class T> class VectorSubclass: public std::vector<T> {
                    public:
                      VectorSubclass (std::initializer_list<T> l): std::vector<T>(l) {}
                    };]])],
  [
    AC_MSG_RESULT(yes)
  ],
  [
    AC_MSG_RESULT(no)
    AC_DEFINE([NO_VECTOR_INITLISTS],1,[Define if std::vector has no initializer_list constructor])
  ]
)
CXXFLAGS="${SAVED_CXXFLAGS}"

# xttpd configs
AC_ARG_WITH([xttpd-user],
            [AS_HELP_STRING([--with-xttpd-user=user],
              [user that xttpd should run as])],
            [xttpd_user=$withval],
            [xttpd_user=nobody])
AC_ARG_WITH([xttpd-group],
            [AS_HELP_STRING([--with-xttpd-group=group],
              [group that xttpd should run as])],
            [xttpd_group=$withval],
            [xttpd_group=nobody])
AC_ARG_WITH([webmaster],
            [AS_HELP_STRING([--with-webmaster=addr],
              [email address for xttpd feedback])],
            [webmasteraddr=$withval])
AC_DEFINE_UNQUOTED([xttpd_user], ["$xttpd_user"], [User for xttpd])
AC_DEFINE_UNQUOTED([xttpd_group], ["$xttpd_group"], [Group for xttpd])
AC_SUBST([SYSTEMD_USER], ["$xttpd_user"])
AC_SUBST([SYSTEMD_GROUP], ["$xttpd_group"])
if test ! -z "$webmasteraddr"; then
  AC_DEFINE_UNQUOTED([webmasteraddr], ["$webmasteraddr"], [Webmaster email address for xttpd])
fi

# with options.
AC_ARG_ENABLE([time-workaround],
  [AS_HELP_STRING([--enable-time-workaround],
                  [work around Y2038 problem; disable time zones])],
  [
    AS_IF([test "$enable_time_workaround" == yes], [
       AC_MSG_WARN([[Y2038 workaround is enabled; time zones are disabled.]])
       AC_DEFINE([TIME_WORKAROUND],1,[Define to work around Y2038 problem but disable time zones])
    ])
  ])
AC_ARG_ENABLE([systemd],
  [AS_HELP_STRING([--enable-systemd],
                  [configure xttpd to work with systemd instead of init])],
  [
    AS_IF([test "$enable_systemd" == yes], [
       AC_MSG_NOTICE([[Configuring xttpd for systemd.]])
       AC_DEFINE([systemd],1,[Define to configure xttpd to work with systemd instead of init])
       AC_SUBST([SYSTEMD_LIBS], ["-lsystemd"])
    ])
  ])
AC_ARG_ENABLE([gnu-attributes],
  [AS_HELP_STRING([--enable-gnu-attributes],
                  [use with g++ -Wall -Wextra to make warnings smarter])],
  [
    AS_IF([test "$enable_gnu_attributes" == yes], [
       AC_MSG_NOTICE([[GNU attributes are enabled.]])
       AC_DEFINE([UseGnuAttributes],1,[Define to enable GNU attributes])
    ])
  ])
AC_ARG_ENABLE([semicolon-pathsep],
  [AS_HELP_STRING([--enable-semicolon-pathsep],
                  [use ; instead of : to separate names in HFILE_PATH (good idea if they begin with C:\)])],
  [
    AS_IF([test "$enable_semicolon_pathsep" == yes], [
       AC_MSG_NOTICE([[Using semicolon as HFILE_PATH separator.]])
       AC_DEFINE([UseSemicolonPathsep],1,[Define to use semicolon as HFILE_PATH separator])
    ])
  ])
AC_ARG_ENABLE([local-files],
  [AS_HELP_STRING([--enable-local-files],
                  [locate xtide.conf, .xtide.xml, and .disableXTidedisclaimer files in current working directory])],
  [
    AS_IF([test "$enable_local_files" == yes], [
       AC_MSG_NOTICE([[Using local files.]])
       AC_DEFINE([UseLocalFiles],1,[Define to locate files in current working directory])
    ])
  ])
AC_ARG_ENABLE([3d],
  [AS_HELP_STRING([--enable-3d],
                  [use Xaw3d or Xaw3dxft if available (default yes)])],
  [
    AS_IF([test "$enable_3d" == no], [
       AC_MSG_NOTICE([[Forcing plain Athena Widgets.]])
       Genuine_Xaw=yes
    ])
  ])
AC_ARG_ENABLE([moon-age],
  [AS_HELP_STRING([--enable-moon-age],
                  [(experimental) replace calendar mode moon phase column with moon age])],
  [
    AS_IF([test "$enable_moon_age" == yes], [
       AC_MSG_NOTICE([[Enabling experimental moon age code.]])
       AC_DEFINE([EXPERIMENTAL_MOON_AGE_NOT_PHASE],1,[Define to enable experimental moon age code])
    ])
  ])
AC_ARG_ENABLE([lm_hard],
  [AS_HELP_STRING([--enable-lm_hard],
                  [link with libm_hard instead of libm (for ARM Android)])],
  [
    AS_IF([test "$enable_lm_hard" == yes], [
       AC_MSG_NOTICE([[Linking with libm_hard instead of libm.]])
       LIBS="$LIBS -lm_hard"
    ], [
       LIBS="$LIBS -lm"
    ])
  ],
  [
    LIBS="$LIBS -lm"
  ])

# For Solaris, this should put -lsocket -lnsl in X_EXTRA_LIBS.
AC_PATH_XTRA

# X, Xaw, Xaw3D, ...
AM_CONDITIONAL([HAVE_X], [test "$no_x" != "yes"])
if test "$no_x" = "yes"; then
  AC_MSG_WARN([Building tide and xttpd without X11; not building xtide.])
else
  if test "$Genuine_Xaw" = "yes"; then
    AC_CHECK_HEADER([X11/Xaw/Box.h],
      [AC_CHECK_LIB([Xaw], [XawInitializeWidgetSet],
        [AC_MSG_NOTICE([[Using libXaw for Athena Widgets.]])
         AC_SUBST([XAW], ["Xaw"])
         AC_DEFINE([XAWPATH], X11/Xaw, [Include path for widgets])],
        [AC_MSG_ERROR([found Xaw headers but not libXaw; try setting LDFLAGS.])],
        [-lXmu -lXt -lSM -lICE -lXext -lX11 -lXpm $X_EXTRA_LIBS])],
      [AC_MSG_ERROR([cannot find Athena Widgets Box.h; try setting CPPFLAGS.])])
  else
    # Choose one of Xaw3dxft, Xaw3d, or good old Xaw.
    AC_MSG_CHECKING([location of freetype2 headers])
    SAVED_CPPFLAGS="${CPPFLAGS}"
    AC_PREPROC_IFELSE(
      [AC_LANG_PROGRAM([[#include <ft2build.h>]])],
      [AC_MSG_RESULT([already in path])],
      [CPPFLAGS="-I/usr/include/freetype2 ${CPPFLAGS}"
       AC_PREPROC_IFELSE(
	[AC_LANG_PROGRAM([[#include <ft2build.h>]])],
	[AC_MSG_RESULT([/usr/include/freetype2])],
	[AC_MSG_RESULT([not found])
         CPPFLAGS="${SAVED_CPPFLAGS}"
         AC_MSG_NOTICE([[freetype2 is a prerequisite for libXaw3dXft to work.]])
        ]
      )]
    )
    AC_MSG_CHECKING([for libXaw3dXft 1.6.2d])

    # XTide 2.15 cannot build with any older version.  Every update tends to
    # break the build, but there are no version defines to use defensively.
    # We check for GET_XAW3DXFT_DATA because it did not exist before version
    # 1.6.2d.

    AC_PREPROC_IFELSE(
      [AC_LANG_SOURCE([[
	#include <X11/Xaw3dxft/Xaw3dXft.h>
        #ifndef GET_XAW3DXFT_DATA
        #FAIL
        #endif
      ]])],
      [	AC_MSG_RESULT(yes)
        AC_CHECK_LIB([Xaw3dxft], [Xaw3dXftDrawString],
        [AC_MSG_NOTICE([[Using libXaw3dXft 1.6.2d for Athena Widgets.]])
         if test "${CPPFLAGS}" != "${SAVED_CPPFLAGS}"; then
           ADDED_CPPFLAGS="-I/usr/include/freetype2 ${ADDED_CPPFLAGS}"
           CPPFLAGS="${SAVED_CPPFLAGS}"
         fi
         AC_SUBST([XAW], ["Xaw3dxft -lXft"])
         AC_DEFINE([HAVE_XAW3DXFT],1,[Define if have Xaw3dXft])
         AC_DEFINE([HAVE_XAW3D],1,[Define if have Xaw3dXft and/or Xaw3d])
         AC_DEFINE([XAWPATH], X11/Xaw3dxft, [Include path for widgets])],
        [AC_MSG_ERROR([found Xaw3dXft headers but not libXaw3dXft; try setting LDFLAGS.])],
        [-lXft -lXmu -lXt -lSM -lICE -lXext -lX11 -lXpm $X_EXTRA_LIBS])],
      [ AC_MSG_RESULT(no)
	CPPFLAGS="${SAVED_CPPFLAGS}"
	AC_CHECK_HEADER([X11/Xaw3d/Box.h],
	[AC_CHECK_LIB([Xaw3d], [XawInitializeWidgetSet],
	  [AC_MSG_NOTICE([[Using libXaw3d for Athena Widgets.]])
	   AC_SUBST([XAW], ["Xaw3d"])
	   AC_DEFINE([HAVE_XAW3D],1,[Define if have Xaw3dXft and/or Xaw3d])
	   AC_DEFINE([XAWPATH], X11/Xaw3d, [Include path for widgets])],
	  [AC_MSG_ERROR([found Xaw3d headers but not libXaw3d; try setting LDFLAGS.])],
	  [-lXmu -lXt -lSM -lICE -lXext -lX11 -lXpm $X_EXTRA_LIBS])],
	[AC_CHECK_HEADER([X11/Xaw/Box.h],
	  [AC_CHECK_LIB([Xaw], [XawInitializeWidgetSet],
	    [AC_MSG_NOTICE([[Using libXaw for Athena Widgets.]])
	     AC_SUBST([XAW], ["Xaw"])
	     AC_DEFINE([XAWPATH], X11/Xaw, [Include path for widgets])],
	    [AC_MSG_ERROR([found Xaw headers but not libXaw; try setting LDFLAGS.])],
	    [-lXmu -lXt -lSM -lICE -lXext -lX11 -lXpm $X_EXTRA_LIBS])],
	  [AC_MSG_ERROR([cannot find Athena Widgets Box.h; try setting CPPFLAGS.])]
      )])]
    )
  fi
fi

# Since adding support for IPv6, xttpd is unlikely to build anymore on any
# system where the answer is not socklen_t, but the check remains harmless.
AC_MSG_CHECKING([type of third argument to accept()])
AC_COMPILE_IFELSE(
  [AC_LANG_SOURCE([[
#include <sys/types.h>
#include <sys/socket.h>
int main (int argc, char **argv) {
  socklen_t length;
  struct sockaddr addr;
  int listen_socket;
  accept (listen_socket, &addr, &length);
  return 0;
}
  ]])],
  [AC_DEFINE([acceptarg3_t], [socklen_t], [Type of third argument to accept()])
   AC_MSG_RESULT(socklen_t)
  ],
  [
    AC_COMPILE_IFELSE(
      [AC_LANG_SOURCE([[
#include <sys/types.h>
#include <sys/socket.h>
int main (int argc, char **argv) {
  int length;
  struct sockaddr addr;
  int listen_socket;
  accept (listen_socket, &addr, &length);
  return 0;
}
      ]])],
      [AC_DEFINE([acceptarg3_t], [int], [Type of third argument to accept()])
       AC_MSG_RESULT(int)
      ],
      [AC_DEFINE([acceptarg3_t], [size_t], [Type of third argument to accept()])
       AC_MSG_RESULT(size_t)
      ]
    )
])

# libz is required by libpng.
AC_CHECK_HEADER([zlib.h], [],
                [AC_MSG_ERROR([cannot find zlib.h; try setting CPPFLAGS.])])
AC_CHECK_LIB([z], [zlibVersion], [],
             [AC_MSG_ERROR([cannot find libz; try setting LDFLAGS.])])

# libpng 0.96 or compatible newer version is required.
AC_CHECK_HEADER([png.h], [],
                [AC_MSG_ERROR([cannot find png.h; try setting CPPFLAGS.])])
AC_CHECK_LIB([png], [png_sig_cmp], [],
             [AC_MSG_ERROR([cannot find libpng; try setting LDFLAGS.])])

# libtcd 2.2 or compatible newer version is required.
AC_CHECK_HEADER([tcd.h], [],
                [AC_MSG_ERROR([cannot find tcd.h; try setting CPPFLAGS.])])
AC_CHECK_LIB([tcd], [get_tide_db_header], [],
             [AC_MSG_ERROR([cannot find libtcd; try setting LDFLAGS.])])

# libdstr is optional; use it if you have a compatible version.
AC_CHECK_HEADER([Dstr], [AC_CHECK_LIB([dstr], [DstrCompat20070215Check])])
AM_CONDITIONAL(NO_LIBDSTR, test "$ac_cv_lib_dstr_DstrCompat20070215Check" != "yes")

# libgps is optional; use it if you have a compatible version.
AC_CHECK_HEADER([gps.h], [AC_CHECK_LIB([gps], [timestamp], [
  AC_MSG_CHECKING([if libgps is 3.x or newer, compatible version])
  AC_COMPILE_IFELSE(
    [AC_LANG_SOURCE([[
#include <gps.h>
#if (GPSD_API_MAJOR_VERSION < 5)
#FAIL
#endif
int main (int argc, char **argv) {
  struct gps_data_t gpsdata;
  gps_open ("127.0.0.1", "2947", &gpsdata);
  gps_stream (&gpsdata, WATCH_ENABLE|WATCH_NEWSTYLE, NULL);
  gps_read (&gpsdata);
  return (gpsdata.fix.mode >= MODE_2D);
}
    ]])],
    [
      AC_MSG_RESULT(yes)
      AC_DEFINE([GPS_GOOD],1,[Define if libgps is usable])
      AC_SUBST([GPSD_LIBS], ["-lgps"])
    ],
    [
      AC_MSG_RESULT(no)
    ]
  )
])])

AC_TYPE_INT8_T
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_INT64_T

AC_TYPE_UINT8_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T

# Visual C++ 2008 Express Edition lacks some standard headers and requires
# some nonstandard ones.
AC_CHECK_HEADERS([syslog.h langinfo.h dirent.h sys/resource.h io.h process.h])

# Visual C++ 2008 Express Edition uses #pragma message not #warning.
AC_MSG_CHECKING([[if #warning works]])
AC_COMPILE_IFELSE(
 [AC_LANG_PROGRAM([[]], [[#warning warning]])],
  AC_MSG_RESULT([yes]),
 [AC_MSG_RESULT([no])
  AC_MSG_CHECKING([[if #pragma message works]])
  AC_COMPILE_IFELSE(
   [AC_LANG_PROGRAM([[]], [[#pragma message("warning")]])],
   [
    AC_MSG_RESULT([yes])
    AC_DEFINE([USE_PRAGMA_MESSAGE],1,[Define to use pragma message instead of warning])
   ],
   [AC_MSG_RESULT([no])
   ])
 ])

# Choices are dirent or WIN32-specific code; there is no third option.
AC_MSG_CHECKING([for either dirent or _finddata_t])
AC_COMPILE_IFELSE(
 [AC_LANG_PROGRAM([[
#ifndef HAVE_DIRENT_H
#ifdef HAVE_IO_H
#include <io.h>
#endif
_finddata_t fileinfo;
#endif
]], [[]])],
  AC_MSG_RESULT([yes]),
 [AC_MSG_RESULT([no])
  AC_MSG_ERROR([neither SUSv3 nor WIN32 dirent facility found])
 ])

# Cygwin has lround but not llround.
# Visual C++ 2008 Express Edition has neither.
AC_MSG_CHECKING([for llround])
AC_LINK_IFELSE(
 [AC_LANG_PROGRAM([[#include <math.h>]], [[llround (0)]])],
 [AC_MSG_RESULT([yes])
  AC_DEFINE([HAVE_LLROUND],1,[Define if llround is in math.h])
 ],
 AC_MSG_RESULT([no]))

# Visual C++ 2008 Express Edition does not support strftime %l format.
# The final parameter to AC_RUN_IFELSE is action-if-cross-compiling.
AC_MSG_CHECKING([for strftime %l format])
AC_RUN_IFELSE(
 [AC_LANG_PROGRAM([[#include <time.h>]], [[
  char s[3];
  struct tm t1;
  size_t stret;
  t1.tm_hour = 5;
  s[0] = s[1] = 'X';
  stret = strftime (s, 3, "%l", &t1);
  return (stret == 0 || s[0] != ' ' || s[1] != '5');
]])],
 [AC_MSG_RESULT([yes])
  AC_DEFINE([HAVE_GOOD_STRFTIME],1,[Define if strftime supports %l format])
 ],
 AC_MSG_RESULT([no]),
 [AC_MSG_RESULT([cannot test in cross-compile; assume yes])
  AC_DEFINE([HAVE_GOOD_STRFTIME],1,[Define if strftime supports %l format])
 ])

# adl_RECURSIVE_EVAL:  patch for badly needed, missing autoconf feature.
# Copied 2016-01-03 from http://ac-archive.sourceforge.net/adl/relpaths.html
# License: GPLWithACException
#   This program is free software; you can redistribute it and/or modify it
#   under the terms of the GNU General Public License as published by the
#   Free Software Foundation. As a special exception, the respective Autoconf
#   Macro's copyright owner gives unlimited permission to copy, distribute
#   and modify the configure scripts that are the output of Autoconf when
#   processing the Macro. You need not follow the terms of the GNU General
#   Public License when using or distributing such scripts
# Author: Alexandre Duret-Lutz <duret_g@epita.fr>
# Version 2001-05-25
dnl adl_RECURSIVE_EVAL(VALUE, RESULT)
dnl =================================
dnl Interpolate the VALUE in loop until it doesn't change,
dnl and set the result to $RESULT.
dnl WARNING: It's easy to get an infinite loop with some unsane input.
AC_DEFUN([adl_RECURSIVE_EVAL],
[_lcl_receval="$1"
$2=`(test "x$prefix" = xNONE && prefix="$ac_default_prefix"
     test "x$exec_prefix" = xNONE && exec_prefix="${prefix}"
     _lcl_receval_old=''
     while test "[$]_lcl_receval_old" != "[$]_lcl_receval"; do
       _lcl_receval_old="[$]_lcl_receval"
       eval _lcl_receval="\"[$]_lcl_receval\""
     done
     echo "[$]_lcl_receval")`])

# We need sbindir fully expanded to patch xttpd.service correctly.
adl_RECURSIVE_EVAL(${sbindir},SBINDIR)
AC_SUBST([SBINDIR])
AC_OUTPUT(Makefile scripts/systemd/xttpd.service)
