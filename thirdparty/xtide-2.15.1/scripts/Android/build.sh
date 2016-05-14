#!/bin/bash
# $Id: build.sh 6280 2016-02-23 00:33:39Z flaterco $
# Sample script for cross-compiling libpng, libtcd, libxtide, tide, and xttpd
# for Android on x86 Linux.  Review and edit before use.

# Usage:  build.sh build-arch target-arch
#   build-arch must be either x86 or x86_64.
#   target-arch must be one of arm, x86, or mips.

# As written, if arm is specified, the script builds a binary with hard
# floating point that runs from a shell prompt on a 1st gen Kindle Fire.
# mips and x86 targets are verified to build but are otherwise untested.

set -e

NUMCPUS=8

# Validate command line.
ERRFLAG=0
case "$1" in
"x86")
  ;;
"x86_64")
  ;;
*)
  echo Please specify x86 or x86_64 for build-arch.
  ERRFLAG=1
  ;;
esac
case "$2" in
"arm")
  ;;
"x86")
  ;;
"mips")
  ;;
*)
  echo Please specify arm, mips, or x86 for target-arch.
  ERRFLAG=1
  ;;
esac
if [ ${ERRFLAG} = 1 ]; then
  echo Usage:  build.sh build-arch target-arch
  exit
fi

# The tarballs for these packages should be in the current dir.
# Android already has libz.
LIBPNG=libpng-1.6.21
LIBTCD=libtcd-2.2.7-r2
LIBTCDDIR=libtcd-2.2.7
XTIDE=xtide-2.15.1

# Edit as needed.
NDKPATH=/usr/local/android-ndk-r10e
COMPFLAGS="-Wall -Wextra -no-canonical-prefixes -O2"
API=android-9
SYSROOT=${NDKPATH}/platforms/${API}/arch-$2
# Path for installing.
DROIDTMP=/tmp/droidstuff-$2

# And furthermore...
COMMON_CONFIG_OPTS="--disable-dependency-tracking --build=$1-pc-linux-gnu --with-sysroot=${SYSROOT} --disable-shared --enable-static --prefix=${DROIDTMP}"
COMMON_CPPFLAGS="--sysroot=${SYSROOT}"
COMMON_LDFLAGS="--sysroot=${SYSROOT}"

# And then do everything different depending on arch.
case "$2" in
"arm")
  # Kindle Fire 1st gen:  OMAP4430 = Cortex-A9 = ARMv7-A.
  # FireOS 6.3.3 = Android 2.3.3 = API level 10.  This NDK has 9 but not 10.
  # -mhard-float went away in GCC 4.9, is superseded by -mfloat-abi=hard.
  COMPFLAGS+=" -march=armv7-a -mtune=cortex-a9 -marm -mfpu=neon -mfloat-abi=hard -Wl,--no-warn-mismatch"
  COMMON_CPPFLAGS+=" -D_NDK_MATH_NO_SOFTFP=1"
  COMMON_CONFIG_OPTS+=" --host=arm-linux-androideabi"
  LIBARCH=armeabi-v7a-hard
  ALSO_WITH="--enable-lm_hard"
  export PATH=${PATH}:${NDKPATH}/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-$1/bin
  ;;
"x86")
  # Cannot test so use defaults
  COMMON_CONFIG_OPTS+=" --host=i686-linux-android"
  LIBARCH=x86
  ALSO_WITH=""
  export PATH=${PATH}:${NDKPATH}/toolchains/x86-4.9/prebuilt/linux-$1/bin
  ;;
"mips")
  # Cannot test so use defaults
  COMMON_CONFIG_OPTS+=" --host=mipsel-linux-android"
  LIBARCH=mips
  ALSO_WITH=""
  export PATH=${PATH}:${NDKPATH}/toolchains/mipsel-linux-android-4.9/prebuilt/linux-$1/bin
  ;;
*)
  echo Input validation failed.
  exit
  ;;
esac

# libstdc++ on Android is another entire can of worms.
STL_EXTRA_CPPFLAGS="-I${NDKPATH}/sources/cxx-stl/gnu-libstdc++/4.9/include -I${NDKPATH}/sources/cxx-stl/gnu-libstdc++/4.9/libs/${LIBARCH}/include"
STL_EXTRA_LDFLAGS="-L${NDKPATH}/sources/cxx-stl/gnu-libstdc++/4.9/libs/${LIBARCH}"
STL_EXTRA_LIBS="-lgnustl_static"

# ----------- libpng -----------

tar xvf ${LIBPNG}.tar.*
cd ${LIBPNG}

if [ $2 = arm ]; then
  # configure has -lm hardcoded but we want -lm_hard instead.
  echo Patching configure script.
  sed s/-lm/-lm_hard/g configure > temp
  mv temp configure
  chmod 755 configure
fi

./configure \
  ${COMMON_CONFIG_OPTS} \
  CFLAGS="${COMPFLAGS}" \
  CPPFLAGS="${COMMON_CPPFLAGS}" \
  LDFLAGS="${COMMON_LDFLAGS}"

make -j ${NUMCPUS}
make install
cd ..
rm -rf ${LIBPNG}

# ----------- libtcd -----------

tar xvf ${LIBTCD}.tar.*
cd ${LIBTCDDIR}

./configure \
  ${COMMON_CONFIG_OPTS} \
  ${ALSO_WITH} \
  CFLAGS="${COMPFLAGS}" \
  CPPFLAGS="${COMMON_CPPFLAGS}" \
  LDFLAGS="${COMMON_LDFLAGS}"

make
make install
cd ..
rm -rf ${LIBTCDDIR}

# ----------- libxtide, tide and xttpd -----------

tar xvf ${XTIDE}.tar.*
cd ${XTIDE}

./configure \
  ${COMMON_CONFIG_OPTS} \
  --enable-gnu-attributes \
  --enable-local-files \
  ${ALSO_WITH} \
  --without-x \
  CFLAGS="${COMPFLAGS}" CXXFLAGS="${COMPFLAGS}" \
  CPPFLAGS="${COMMON_CPPFLAGS} ${STL_EXTRA_CPPFLAGS} -I${DROIDTMP}/include -DXTTPD_NO_DAEMON" \
  LDFLAGS="${COMMON_LDFLAGS} ${STL_EXTRA_LDFLAGS} -L${DROIDTMP}/lib" \
  LIBS="${STL_EXTRA_LIBS}"

make -j ${NUMCPUS}
make install
cd ..
rm -rf ${XTIDE}

# ------------------------------------

echo ""
echo Everything has been installed to ${DROIDTMP}.
echo The tide binary is in ${DROIDTMP}/bin.
