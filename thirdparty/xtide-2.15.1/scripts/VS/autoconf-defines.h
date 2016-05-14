/* libxtide/autoconf-defines.h.  Generated from autoconf-defines.h.in by configure.  */
/* libxtide/autoconf-defines.h.in.  Generated from configure.ac by autoheader.  */
/* With some manual deletions */

/* Define to use compound literals workaround for broken initializer list
   support */
/* #undef BROKEN_INITIALIZER_LISTS */

/* Define to enable experimental moon age code */
/* #undef EXPERIMENTAL_MOON_AGE_NOT_PHASE */

/* Define if libgps is usable */
/* #undef GPS_GOOD */

/* Define to 1 if you have the <dirent.h> header file. */
/* #undef HAVE_DIRENT_H */

/* Define to 1 if you have the <dlfcn.h> header file. */
/* #undef HAVE_DLFCN_H */

/* Define if strftime supports %l format */
/* #undef HAVE_GOOD_STRFTIME */

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define to 1 if you have the <io.h> header file. */
#define HAVE_IO_H 1

/* Define to 1 if you have the <langinfo.h> header file. */
/* #undef HAVE_LANGINFO_H */

/* Define to 1 if you have the `dstr' library (-ldstr). */
/* #undef HAVE_LIBDSTR */

/* Define to 1 if you have the `png' library (-lpng). */
#define HAVE_LIBPNG 1

/* Define to 1 if you have the `tcd' library (-ltcd). */
#define HAVE_LIBTCD 1

/* Define to 1 if you have the `z' library (-lz). */
#define HAVE_LIBZ 1

/* Define if llround is in math.h */
#define HAVE_LLROUND 1

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the <process.h> header file. */
#define HAVE_PROCESS_H 1

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
/* #undef HAVE_STRINGS_H */

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the <syslog.h> header file. */
/* #undef HAVE_SYSLOG_H */

/* Define to 1 if you have the <sys/resource.h> header file. */
/* #undef HAVE_SYS_RESOURCE_H */

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <unistd.h> header file. */
/* #undef HAVE_UNISTD_H */

/* Define if have Xaw3dXft and/or Xaw3d */
/* #undef HAVE_XAW3D */

/* Define if have Xaw3dXft */
/* #undef HAVE_XAW3DXFT */

/* Define to the sub-directory in which libtool stores uninstalled libraries.
   */
#define LT_OBJDIR ".libs/"

/* Define to use run-time initialization workaround for completely missing
   initializer list support */
/* #undef NO_INITIALIZER_LISTS */

/* Define if std::vector has no initializer_list constructor */
/* #undef NO_VECTOR_INITLISTS */

/* Define to the full name and version of this package. */
/* Moved up to build.bat because it keeps changing. */
/* #define PACKAGE_STRING "XTide 2.14-dev-20141021" */

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Define to work around Y2038 problem but disable time zones */
/* #undef TIME_WORKAROUND */

/* Define to use pragma message instead of warning */
#define USE_PRAGMA_MESSAGE 1

/* Define to enable GNU attributes */
/* #undef UseGnuAttributes */

/* Define to locate files in current working directory */
#define UseLocalFiles 1

/* Define to use semicolon as HFILE_PATH separator */
#define UseSemicolonPathsep 1

/* Include path for widgets */
/* #undef XAWPATH */

/* Define to 1 if the X Window System is missing or not being used. */
#define X_DISPLAY_MISSING 1

/* Define to 1 if `lex' declares `yytext' as a `char *' by default, not a
   `char[]'. */
/* #undef YYTEXT_POINTER */

/* Define for Solaris 2.5.1 so the uint32_t typedef from <sys/synch.h>,
   <pthread.h>, or <semaphore.h> is not used. If the typedef were allowed, the
   #define below would cause a syntax error. */
/* #undef _UINT32_T */

/* Define for Solaris 2.5.1 so the uint64_t typedef from <sys/synch.h>,
   <pthread.h>, or <semaphore.h> is not used. If the typedef were allowed, the
   #define below would cause a syntax error. */
/* #undef _UINT64_T */

/* Define for Solaris 2.5.1 so the uint8_t typedef from <sys/synch.h>,
   <pthread.h>, or <semaphore.h> is not used. If the typedef were allowed, the
   #define below would cause a syntax error. */
/* #undef _UINT8_T */

/* Type of third argument to accept() */
#define acceptarg3_t size_t

/* Define to the type of a signed integer type of width exactly 16 bits if
   such a type exists and the standard includes do not define it. */
/* #undef int16_t */

/* Define to the type of a signed integer type of width exactly 32 bits if
   such a type exists and the standard includes do not define it. */
/* #undef int32_t */

/* Define to the type of a signed integer type of width exactly 64 bits if
   such a type exists and the standard includes do not define it. */
/* #undef int64_t */

/* Define to the type of a signed integer type of width exactly 8 bits if such
   a type exists and the standard includes do not define it. */
/* #undef int8_t */

/* Define to configure xttpd to work with systemd instead of init */
/* #undef systemd */

/* Define to the type of an unsigned integer type of width exactly 16 bits if
   such a type exists and the standard includes do not define it. */
/* #undef uint16_t */

/* Define to the type of an unsigned integer type of width exactly 32 bits if
   such a type exists and the standard includes do not define it. */
/* #undef uint32_t */

/* Define to the type of an unsigned integer type of width exactly 64 bits if
   such a type exists and the standard includes do not define it. */
/* #undef uint64_t */

/* Define to the type of an unsigned integer type of width exactly 8 bits if
   such a type exists and the standard includes do not define it. */
/* #undef uint8_t */

/* Webmaster email address for xttpd */
/* #undef webmasteraddr */

/* Group for xttpd */
#define xttpd_group "nobody"

/* User for xttpd */
#define xttpd_user "nobody"
