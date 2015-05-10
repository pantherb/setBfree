#!/bin/bash

: ${SBFARCH=-arch x86_64 -arch i386}
: ${SBFSTACK=$HOME/src/sbf_stack}
: ${OUTDIR=/tmp/}
: ${OSXCOMPAT=-mmacosx-version-min=10.5 -DMAC_OS_X_VERSION_MAX_ALLOWED=1090}

VERSION=`git describe --tags | sed 's/-g[a-f0-9]*$//'`
if test -z "$VERSION"; then
	echo "*** Cannot query version information."
	exit 1
fi

set -e

TARGET=/tmp/bpkg
PRODUCTDIR=$TARGET/setBfree.app

PATH=$HOME/bin:${SBFSTACK}/bin/:/usr/bin:/bin:/usr/sbin:/sbin:/usr/local/bin
PKG_CONFIG_PATH=${SBFSTACK}/lib/pkgconfig

make clean
make \
	ENABLE_CONVOLUTION=no \
	CFLAGS="-msse -msse2 -ffast-math -fomit-frame-pointer -O3 -fno-finite-math-only ${OSXCOMPAT} ${SBFARCH} -I${SBFSTACK}/src -DUSE_WEAK_JACK -DBUILTINFONT -fdata-sections -ffunction-sections" \
	LDFLAGS="-fdata-sections -ffunction-sections -Wl,-dead_strip"
	IRPATH="../Resources/ir" \
	WEAKJACK="${SBFSTACK}/src/weakjack/weak_libjack.c" \
	FONTFILE=verabd.h \
	SUBDIRS="b_synth b_whirl ui" $@ \
	|| exit

# zip-up LV2
export LV2TMPDIR=`mktemp -d -t lv2tmp`
trap "rm -rf $LV2TMPDIR" EXIT

mkdir -p ${LV2TMPDIR}/b_synth.lv2/
cp -v b_synth/*.ttl ${LV2TMPDIR}/b_synth.lv2/
cp -v b_synth/*.dylib ${LV2TMPDIR}/b_synth.lv2/

mkdir -p ${LV2TMPDIR}/b_whirl.lv2/
cp -v b_whirl/*.ttl ${LV2TMPDIR}/b_whirl.lv2/
cp -v b_whirl/*.dylib ${LV2TMPDIR}/b_whirl.lv2/

#############################################################################
# Create LOCAL APP DIR
export PRODUCT_NAME="setBfree"
export RSRC_DIR="$(pwd)/doc/"
export APPNAME="${PRODUCT_NAME}.app"

export BUNDLEDIR=`mktemp -d -t bundle`
trap "rm -rf $BUNDLEDIR $LV2TMPDIR" EXIT

export TARGET_BUILD_DIR="${BUNDLEDIR}/${APPNAME}/"
export TARGET_CONTENTS="${TARGET_BUILD_DIR}Contents/"

mkdir ${TARGET_BUILD_DIR}
mkdir ${TARGET_BUILD_DIR}Contents
mkdir ${TARGET_BUILD_DIR}Contents/MacOS
mkdir ${TARGET_BUILD_DIR}Contents/Resources

if test -n "$ZIPUP" ; then # build a standalone lv2 zip
	cd ${LV2TMPDIR}
	rm -f  ${OUTDIR}${PRODUCT_NAME}-lv2-osx-${VERSION}.zip
	zip -r ${OUTDIR}${PRODUCT_NAME}-lv2-osx-${VERSION}.zip b_synth.lv2/
	ls -l ${OUTDIR}${PRODUCT_NAME}-lv2-osx-${VERSION}.zip
	cd -
fi

#############################################################################
# DEPLOY TO LOCAL APP DIR

cp -v ui/setBfreeUI ${TARGET_CONTENTS}MacOS/setBfreeUI
cp -v pgm/default.pgm ${TARGET_CONTENTS}Resources/
cp -v cfg/default.cfg ${TARGET_CONTENTS}Resources/
cp -v cfg/bcf2000.cfg ${TARGET_CONTENTS}Resources/
cp -v cfg/oxy61.cfg ${TARGET_CONTENTS}Resources/
cp -v cfg/K2500.cfg ${TARGET_CONTENTS}Resources/
cp -v cfg/KB3X42_1.K25 ${TARGET_CONTENTS}Resources/
cp -v ${RSRC_DIR}/${PRODUCT_NAME}.icns ${TARGET_CONTENTS}Resources/
#cp -av b_conv/ir ${TARGET_CONTENTS}Resources/

echo "APPL~~~~" > ${TARGET_CONTENTS}PkgInfo

cat > ${TARGET_CONTENTS}Info.plist << EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple Computer//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleExecutable</key>
	<string>setBfreeUI</string>
	<key>CFBundleName</key>
	<string>setBfree</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>CFBundleSignature</key>
	<string>~~~~</string>
	<key>CFBundleVersion</key>
	<string>1.0</string>
	<key>CFBundleIconFile</key>
	<string>setBfree</string>
	<key>CSResourcesFileMapped</key>
	<true/>
</dict>
</plist>
EOF

# TODO update installer to handle multiple bundles.
if test -n "$LV2INSTALLER"; then
	$LV2INSTALLER
	DMGPOSA="set position of item \"LV2Installer.app\" of container window to {100, 260}"
	DMGPOSB="set position of item \"b_synth.lv2\" of container window to {205, 300}"
	DMGPOSC="set position of item \"b_whirl.lv2\" of container window to {205, 210}"
else
	DMGPOSA="set position of item \"b_synth.lv2\" of container window to {100, 260}"
	DMGPOSB="set position of item \"b_whirl.lv2\" of container window to {205, 260}"
	DMGPOSC=""
fi

##############################################################################
#roll a DMG

UC_DMG="${OUTDIR}${PRODUCT_NAME}-${VERSION}.dmg"

DMGBACKGROUND=${RSRC_DIR}dmgbg.png
VOLNAME=$PRODUCT_NAME-${VERSION}
EXTRA_SPACE_MB=5


DMGMEGABYTES=$[ `du -sck "${TARGET_BUILD_DIR}" "${LV2TMPDIR}" | tail -n 1 | cut -f 1` * 1024 / 1048576 + $EXTRA_SPACE_MB ]
echo "DMG MB = " $DMGMEGABYTES

MNTPATH=`mktemp -d -t mntpath`
TMPDMG=`mktemp -t tmpdmg`
ICNSTMP=`mktemp -t appicon`

trap "rm -rf $MNTPATH $TMPDMG ${TMPDMG}.dmg $ICNSTMP $BUNDLEDIR $LV2TMPDIR" EXIT

rm -f $UC_DMG "$TMPDMG" "${TMPDMG}.dmg" "$ICNSTMP ${ICNSTMP}.icns ${ICNSTMP}.rsrc"
rm -rf "$MNTPATH"
mkdir -p "$MNTPATH"

TMPDMG="${TMPDMG}.dmg"

hdiutil create -megabytes $DMGMEGABYTES "$TMPDMG"
DiskDevice=$(hdid -nomount "$TMPDMG" | grep Apple_HFS | cut -f 1 -d ' ')
newfs_hfs -v "${VOLNAME}" "${DiskDevice}"
mount -t hfs -o nobrowse "${DiskDevice}" "${MNTPATH}"

cp -a ${TARGET_BUILD_DIR} "${MNTPATH}/${APPNAME}"
cp -a ${BUNDLEDIR}/* "${MNTPATH}/"
cp -a ${LV2TMPDIR}/b_synth.lv2 "${MNTPATH}/b_synth.lv2"
cp -a ${LV2TMPDIR}/b_whirl.lv2 "${MNTPATH}/b_whirl.lv2"
cp ${RSRC_DIR}/osx_readme.txt "${MNTPATH}/README.txt"

mkdir "${MNTPATH}/.background"
cp -vi ${DMGBACKGROUND} "${MNTPATH}/.background/dmgbg.png"

echo "setting DMG background ..."

if test $(sw_vers -productVersion | cut -d '.' -f 2) -lt 9; then
	# OSX ..10.8.X
	DISKNAME=${VOLNAME}
else
	# OSX 10.9.X and later
	DISKNAME=`basename "${MNTPATH}"`
fi

echo '
   tell application "Finder"
     tell disk "'${DISKNAME}'"
	   open
	   delay 1
	   set current view of container window to icon view
	   set toolbar visible of container window to false
	   set statusbar visible of container window to false
	   set the bounds of container window to {400, 200, 800, 580}
	   set theViewOptions to the icon view options of container window
	   set arrangement of theViewOptions to not arranged
	   set icon size of theViewOptions to 64
	   set background picture of theViewOptions to file ".background:dmgbg.png"
	   make new alias file at container window to POSIX file "/Applications" with properties {name:"Applications"}
	   set position of item "'${APPNAME}'" of container window to {100, 100}
	   set position of item "Applications" of container window to {310, 100}
	   '${DMGPOSA}'
	   '${DMGPOSB}'
	   '${DMGPOSC}'
	   set position of item "README.txt" of container window to {310, 260}
	   close
	   open
	   update without registering applications
	   delay 5
	   eject
     end tell
   end tell
' | osascript || {
	echo "Failed to set background/arrange icons"
	umount "${DiskDevice}" || true
	hdiutil eject "${DiskDevice}"
	exit 1
}

set +e
chmod -Rf go-w "${MNTPATH}"
set -e
sync
sleep 2
sync

echo "unmounting the disk image ..."
# Umount the image ('eject' above may already have done that)
umount "${DiskDevice}" || true
hdiutil eject "${DiskDevice}" || true

# Create a read-only version, use zlib compression
echo "compressing Image ..."
hdiutil convert -format UDZO "${TMPDMG}" -imagekey zlib-level=9 -o "${UC_DMG}"
# Delete the temporary files
rm "$TMPDMG"
rm -rf "$MNTPATH"

echo "setting file icon ..."

cp ${RSRC_DIR}/${PRODUCT_NAME}.icns ${ICNSTMP}.icns
sips -i ${ICNSTMP}.icns
DeRez -only icns ${ICNSTMP}.icns > ${ICNSTMP}.rsrc
Rez -append ${ICNSTMP}.rsrc -o "$UC_DMG"
SetFile -a C "$UC_DMG"

rm ${ICNSTMP}.icns ${ICNSTMP}.rsrc
rm -rf $BUNDLEDIR

echo
echo "packaging suceeded:"
ls -l "$UC_DMG"
echo "Done."

echo "$VERSION" > ${OUTDIR}/${PRODUCT_NAME}.latest.txt
