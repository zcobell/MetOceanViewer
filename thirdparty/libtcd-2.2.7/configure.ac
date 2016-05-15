# $Id: configure.ac 6245 2016-01-25 17:29:12Z flaterco $

# Int types macros require autoconf 2.60 or later
AC_PREREQ([2.60])

# autoconf 2.65 (Slackware 13.1) has a regression that causes the INT*_T
# tests to fail improperly.  See email from Eric Blake to the bug-autoconf
# mailing list on 2010-02-06.
m4_if(2.65, AC_AUTOCONF_VERSION, [m4_fatal([autoconf version 2.65 is broken and unusable])])

# FIXME: Update version info on each release
AC_INIT([libtcd], [2.2.7], [dave@flaterco.com])
AC_SUBST([libtcd_rev_date], ["2015-08-09"])
AC_SUBST([libtcd_major_rev], ["2"])
AC_SUBST([libtcd_minor_rev], ["2"])
# NOTE patchlevel must include leading period if it is provided
AC_SUBST([libtcd_patchlevel], [".7"])

# Added at the behest of libtoolize.
AC_CONFIG_MACRO_DIR([m4])

# The following is "bad" because "--enable-feature options should
# never make a feature behave differently or cause one feature to
# replace another.  They should only cause parts of the program to be
# built rather than left out."  COMPAT114 is bad for the same reason.
# Nobody should use it, but if they do, it may as well work like this.

AC_SUBST([def_COMPAT114], ["undef"])
AC_SUBST([ver_COMPAT114], [""])
AC_ARG_ENABLE([COMPAT114],
  [AS_HELP_STRING([--enable-COMPAT114],
                  [for emergency use only:  force API changes to enable old applications to build; break applications expecting the v2 API; disable build of shared library; see libtcd.html])],
  [
    AS_IF([test "$enable_COMPAT114" == yes], [
       AC_MSG_WARN([[COMPAT114 is enabled!]])
       AC_MSG_WARN([[This option is for emergency use ONLY!]])
       AC_MSG_WARN([[Forcing API changes to enable old applications to build!]])
       AC_MSG_WARN([[This will BREAK applications expecting the v2 API!]])
       AC_MSG_WARN([[Disabling build of shared library!]])
       AC_MSG_WARN([[Read libtcd.html!]])
       enable_shared=no
       AC_SUBST([def_COMPAT114], ["define"])
       AC_SUBST([ver_COMPAT114], ["-COMPAT114"])
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

AM_INIT_AUTOMAKE([-Wall -Werror])

AC_LANG([C])
AC_PROG_CC
m4_ifdef([AM_PROG_AR], [AM_PROG_AR])
AC_PROG_LIBTOOL

AC_TYPE_INT8_T
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_INT64_T

AC_TYPE_UINT8_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T

# Unfortunately, we need to take this to the next level and generate a
# header file that will work outside of autoconf.  Evil:  relying on
# internals of autoconf macro definitions.

AS_IF([test "$ac_cv_header_sys_types_h" == yes],
      [AC_SUBST([sub_systypes], ["#include <sys/types.h>"])],
      [AC_SUBST([sub_systypes], [""])])
AS_IF([test "$ac_cv_header_inttypes_h" == yes],
        [AC_SUBST([sub_inttypes], ["#include <inttypes.h>"])],
      [test "$ac_cv_header_stdint_h" == yes],
        [AC_SUBST([sub_inttypes], ["#include <stdint.h>"])],
        [AC_SUBST([sub_inttypes], [""])])

AS_IF([test "$ac_cv_c_int8_t" == yes],
      [AC_SUBST([sub_int8_t], ["int8_t"])],
      [AC_SUBST([sub_int8_t], ["$ac_cv_c_int8_t"])])
AS_IF([test "$ac_cv_c_int16_t" == yes],
      [AC_SUBST([sub_int16_t], ["int16_t"])],
      [AC_SUBST([sub_int16_t], ["$ac_cv_c_int16_t"])])
AS_IF([test "$ac_cv_c_int32_t" == yes],
      [AC_SUBST([sub_int32_t], ["int32_t"])],
      [AC_SUBST([sub_int32_t], ["$ac_cv_c_int32_t"])])
AS_IF([test "$ac_cv_c_int64_t" == yes],
      [AC_SUBST([sub_int64_t], ["int64_t"])],
      [AC_SUBST([sub_int64_t], ["$ac_cv_c_int64_t"])])

AS_IF([test "$ac_cv_c_uint8_t" == yes],
      [AC_SUBST([sub_uint8_t], ["uint8_t"])],
      [AC_SUBST([sub_uint8_t], ["$ac_cv_c_uint8_t"])])
AS_IF([test "$ac_cv_c_uint16_t" == yes],
      [AC_SUBST([sub_uint16_t], ["uint16_t"])],
      [AC_SUBST([sub_uint16_t], ["$ac_cv_c_uint16_t"])])
AS_IF([test "$ac_cv_c_uint32_t" == yes],
      [AC_SUBST([sub_uint32_t], ["uint32_t"])],
      [AC_SUBST([sub_uint32_t], ["$ac_cv_c_uint32_t"])])
AS_IF([test "$ac_cv_c_uint64_t" == yes],
      [AC_SUBST([sub_uint64_t], ["uint64_t"])],
      [AC_SUBST([sub_uint64_t], ["$ac_cv_c_uint64_t"])])

# Visual C++ 2008 Express Edition has io.h but not unistd.h.
AC_CHECK_HEADERS([io.h])

# Visual C++ 2008 Express Edition has _chsize but not ftruncate.
AC_MSG_CHECKING([for ftruncate])
AC_LINK_IFELSE(
 [AC_LANG_PROGRAM([[
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_IO_H
#include <io.h>
#endif
                  ]], [[ftruncate (0, 0)]])],
  AC_MSG_RESULT([yes]),
 [AC_MSG_RESULT([no])
  AC_MSG_CHECKING([for _chsize])
  AC_LINK_IFELSE(
   [AC_LANG_PROGRAM([[
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_IO_H
#include <io.h>
#endif
		    ]], [[_chsize (0, 0)]])],
   [
    AC_MSG_RESULT([yes])
    AC_DEFINE(ftruncate, _chsize)
   ],
   [AC_MSG_RESULT([no])
    AC_MSG_ERROR([found neither ftruncate nor _chsize.])
   ])
 ])

# Visual C++ 2008 Express Edition uses #pragma message not #warning.
AC_SUBST([def_PRAGMAMESSAGE], ["undef"])
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
    AC_SUBST([def_PRAGMAMESSAGE], ["define"])
   ],
   [AC_MSG_RESULT([no])
   ])
 ])

AC_CONFIG_FILES([Makefile tcd.h])
AC_OUTPUT
