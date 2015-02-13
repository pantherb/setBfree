#!/bin/bash

: ${XARCH=i686} # or x86_64
: ${SRC=/usr/src}
: ${OUTDIR=/var/tmp/}


###############################################################################
set -e

if test "$XARCH" = "x86_64"; then
	echo "Target: 64bit GNU/Linux (x86_64)"
	HPREFIX=x86_64
	WARCH=x86_64
else
	echo "Target: 32bit GNU/Linux (i686)"
	HPREFIX=i386
	WARCH=i386
fi

: ${PREFIX=${SRC}/lnx-stack-$WARCH}
: ${BUILDD=${SRC}/lnx-build-$WARCH}

export PKG_CONFIG_PATH=${PREFIX}/lib/pkgconfig

make clean

# TODO BUILTINFONT
make \
	CFLAGS="-I${PREFIX}/include -fvisibility=hidden -DNDEBUG -msse -msse2 -mfpmath=sse -fomit-frame-pointer -O3 -mstackrealign -fno-finite-math-only -I${PREFIX}/src -DUSE_WEAK_JACK -DBUILTINFONT" \
	LDFLAGS="-L${PREFIX}/lib -fvisibility=hidden" \
	ENABLE_CONVOLUTION=no \
	WEAKJACK="${PREFIX}/src/weakjack/weak_libjack.c" \
	FONTFILE=verabd.h \
	STATICBUILD=yes \
	SUBDIRS="b_synth ui src"


##############################################################################

VERSION=`git describe --tags`
PRODUCT_NAME=setbfree

if test -z "$OUTDIR"; then
	OUTDIR=/tmp/
fi

BUNDLEDIR=`mktemp -d`
trap "rm -rf ${BUNDLEDIR}" EXIT

mkdir -p ${BUNDLEDIR}/${PRODUCT_NAME}/bin
mkdir -p ${BUNDLEDIR}/${PRODUCT_NAME}/b_synth.lv2

cp -v b_synth/*.ttl b_synth/*.so "${BUNDLEDIR}/${PRODUCT_NAME}/b_synth.lv2"
cp -v ui/setBfreeUI "${BUNDLEDIR}/${PRODUCT_NAME}/bin"
cp -v src/setBfree "${BUNDLEDIR}/${PRODUCT_NAME}/bin"
# TODO add README, man-pages, default cfg,..
# desktop file
# makeself installer?!

cd ${BUNDLEDIR}
rm -f ${OUTDIR}${PRODUCT_NAME}-${WARCH}-${VERSION}.tar.gz
tar czf ${OUTDIR}${PRODUCT_NAME}-${WARCH}-${VERSION}.tar.gz ${PRODUCT_NAME}
ls -l ${OUTDIR}${PRODUCT_NAME}-${WARCH}-${VERSION}.tar.gz

rm -rf ${BUNDLEDIR}
