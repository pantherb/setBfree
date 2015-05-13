#!/bin/bash

: ${XARCH=i686} # or x86_64
: ${SRC=/usr/src}
: ${OUTDIR=/var/tmp/}

VERSION=`git describe --tags | sed 's/-g[a-f0-9]*$//'`
if test -z "$VERSION"; then
	echo "*** Cannot query version information."
	exit 1
fi

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

make \
	CFLAGS="-I${PREFIX}/include -fvisibility=hidden -DNDEBUG -msse -msse2 -mfpmath=sse -fomit-frame-pointer -O3 -mstackrealign -fno-finite-math-only -I${PREFIX}/src -DUSE_WEAK_JACK -DBUILTINFONT -fdata-sections -ffunction-sections" \
	LDFLAGS="-L${PREFIX}/lib -fvisibility=hidden -fdata-sections -ffunction-sections -Wl,--gc-sections -Wl,-O1 -Wl,--strip-all" \
	ENABLE_CONVOLUTION=no \
	WEAKJACK="${PREFIX}/src/weakjack/weak_libjack.c" \
	FONTFILE=verabd.h \
	STATICBUILD=yes \
	SUBDIRS="b_synth b_whirl ui src" $@


##############################################################################

PRODUCT_NAME=setBfree

if test -z "$OUTDIR"; then
	OUTDIR=/tmp/
fi

BUNDLEDIR=`mktemp -d`
trap "rm -rf ${BUNDLEDIR}" EXIT

mkdir -p ${BUNDLEDIR}/${PRODUCT_NAME}/bin
mkdir -p ${BUNDLEDIR}/${PRODUCT_NAME}/b_synth.lv2
mkdir -p ${BUNDLEDIR}/${PRODUCT_NAME}/b_whirl.lv2

cp -v b_synth/*.ttl b_synth/*.so "${BUNDLEDIR}/${PRODUCT_NAME}/b_synth.lv2"
cp -v b_whirl/*.ttl b_whirl/*.so "${BUNDLEDIR}/${PRODUCT_NAME}/b_whirl.lv2"
cp -v ui/setBfreeUI "${BUNDLEDIR}/${PRODUCT_NAME}/bin"
cp -v src/setBfree "${BUNDLEDIR}/${PRODUCT_NAME}/bin"

echo "$VERSION" > ${OUTDIR}/${PRODUCT_NAME}.latest.txt

if test -n "$ZIPUP" ; then # build a standalone lv2 zip
	cd ${BUNDLEDIR}/${PRODUCT_NAME}
	rm -f ${OUTDIR}${PRODUCT_NAME}-lv2-linux-${WARCH}-${VERSION}.zip
	zip -r ${OUTDIR}${PRODUCT_NAME}-lv2-linux-${WARCH}-${VERSION}.zip b_synth.lv2/
	ls -l ${OUTDIR}${PRODUCT_NAME}-lv2-linux-${WARCH}-${VERSION}.zip
	cd -
fi

# TODO add README, man-pages, default cfg,..
# desktop file
# makeself installer?!

cd ${BUNDLEDIR}
rm -f ${OUTDIR}${PRODUCT_NAME}-${VERSION}ar.gz
tar czf ${OUTDIR}${PRODUCT_NAME}-${VERSION}-${WARCH}.tar.gz ${PRODUCT_NAME}
ls -l ${OUTDIR}${PRODUCT_NAME}-${VERSION}-${WARCH}.tar.gz

rm -rf ${BUNDLEDIR}
