#!/bin/bash

: ${XARCH=i686} # or x86_64
: ${SRC=/usr/src}
: ${OUTDIR=/var/tmp}

if test "$XARCH" = "x86_64"; then
	echo "Target: 64bit Windows (x86_64)"
	XPREFIX=x86_64-w64-mingw32
	HPREFIX=x86_64
	WARCH=w64
else
	echo "Target: 32 Windows (i686)"
	XPREFIX=i686-w64-mingw32
	HPREFIX=i386
	WARCH=w32
fi

: ${PREFIX=$SRC/win-stack-$WARCH}

export PKG_CONFIG_PATH=${PREFIX}/lib/pkgconfig

if test -f /home/robtk/.sbfbuild.cfg; then
	. /home/robtk/.sbfbuild.cfg
fi

set -e

make \
	XWIN=${XPREFIX} \
	clean

make \
  XWIN=${XPREFIX} \
	CFLAGS="-I${PREFIX}/include -fvisibility=hidden -DNDEBUG -DPTW32_STATIC_LIB -msse -msse2 -mfpmath=sse -fomit-frame-pointer -O3 -mstackrealign -fno-finite-math-only -I${PREFIX}/src -DUSE_WEAK_JACK -DBUILTINFONT -fdata-sections -ffunction-sections" \
	LDFLAGS="-L${PREFIX}/lib -fvisibility=hidden -fdata-sections -ffunction-sections -Wl,--gc-sections -Wl,-O1 -Wl,--as-needed -Wl,--strip-all" \
	ENABLE_CONVOLUTION=no PKG_UI_FLAGS=--static \
	FONTFILE=verabd.h \
	SUBDIRS="b_synth b_whirl ui" \
	$@

### package
PRODUCT_NAME=setBfree
XREGKEY=setbfree

if [ "$(id -u)" = "0" ]; then
	apt-get -y install nsis curl
fi

GITVERSION=$(git describe --tags | sed 's/-g.*$//')
BUILDDATE=$(date -R)
BINVERSION=$(git describe --tags | sed 's/-g.*$//' | sed 's/-/./')

if test -z "$GITVERSION"; then
	echo "*** Cannot query version information."
	exit 1
fi

OUTFILE="${OUTDIR}/${PRODUCT_NAME}-${GITVERSION}-${WARCH}-Setup.exe"

################################################################################

DESTDIR=`mktemp -d`
trap 'rm -rf $DESTDIR' exit SIGINT SIGTERM

echo " === bundle to $DESTDIR"

mkdir -p $DESTDIR/share
mkdir -p ${DESTDIR}/b_synth.lv2
mkdir -p ${DESTDIR}/b_whirl.lv2

cp -v b_synth/*.ttl b_synth/*.dll "$DESTDIR/b_synth.lv2"
cp -v b_whirl/*.ttl b_whirl/*.dll "$DESTDIR/b_whirl.lv2"

# zip-up LV2
if test -n "$ZIPUP" ; then # build a standalone lv2 zip
	cd ${DESTDIR}
	rm -f ${OUTDIR}/${PRODUCT_NAME}-lv2-${WARCH}-${GITVERSION}.zip
	zip -r ${OUTDIR}/${PRODUCT_NAME}-lv2-${WARCH}-${GITVERSION}.zip b_synth.lv2/
	ls -l ${OUTDIR}/${PRODUCT_NAME}-lv2-${WARCH}-${GITVERSION}.zip
	cd -
fi

cp -v COPYING "$DESTDIR/share/"
cp -v ui/setBfreeUI.exe "$DESTDIR/"
cp -v img/setbfree.ico "$DESTDIR/share/"
#cp -v doc/win_readme.txt "${DESTDIR}/README.txt"

echo " === complete"
du -sh $DESTDIR

################################################################################
echo " === Preparing Windows Installer"
NSISFILE="$DESTDIR/sbf.nsis"

if test "$WARCH" = "w64"; then
	PGF=PROGRAMFILES64
	CMF=COMMONFILES64
else
	PGF=PROGRAMFILES
	CMF=COMMONFILES
fi

if test -n "$QUICKZIP" ; then
	cat > "$NSISFILE" << EOF
SetCompressor zlib
EOF
else
	cat > "$NSISFILE" << EOF
SetCompressor /SOLID lzma
SetCompressorDictSize 32
EOF
fi

cat >> "$NSISFILE" << EOF
!include MUI2.nsh
Name "${PRODUCT_NAME}"
OutFile "${OUTFILE}"
RequestExecutionLevel admin
InstallDir "\$${PGF}\\${PRODUCT_NAME}"
InstallDirRegKey HKLM "Software\\RSS\\${XREGKEY}\\$WARCH" "Install_Dir"

!define MUI_ICON "share\\setbfree.ico"

!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_LICENSE "share\\COPYING"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "LV2 Plugin (required)" SecLV2
  SectionIn RO
  SetOutPath "\$${CMF}\\LV2"
  File /r b_synth.lv2
  File /r b_whirl.lv2
  SetOutPath "\$INSTDIR"
  File /nonfatal README.txt
  WriteRegStr HKLM SOFTWARE\\RSS\\${XREGKEY}\\$WARCH "Install_Dir" "\$INSTDIR"
  WriteRegStr HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\${XREGKEY}" "DisplayName" "${PRODUCT_NAME}"
  WriteRegStr HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\${XREGKEY}" "UninstallString" '"\$INSTDIR\\uninstall.exe"'
  WriteRegDWORD HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\${XREGKEY}" "NoModify" 1
  WriteRegDWORD HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\${XREGKEY}" "NoRepair" 1
  WriteUninstaller "\$INSTDIR\uninstall.exe"
  SetShellVarContext all
  CreateDirectory "\$SMPROGRAMS\\${PRODUCT_NAME}"
  CreateShortCut "\$SMPROGRAMS\\${PRODUCT_NAME}\\Uninstall.lnk" "\$INSTDIR\\uninstall.exe" "" "\$INSTDIR\\uninstall.exe" 0
SectionEnd

Section "JACK Application" SecJACK
  SetOutPath \$INSTDIR
  File setBfreeUI.exe
  SetShellVarContext all
  CreateDirectory "\$SMPROGRAMS\\${PRODUCT_NAME}"
  CreateShortCut "\$SMPROGRAMS\\${PRODUCT_NAME}\\setBfree.lnk" "\$INSTDIR\\setBfreeUI.exe" "0" "\$INSTDIR\\setBfreeUI.exe" 0
SectionEnd

LangString DESC_SecLV2 \${LANG_ENGLISH} "${PRODUCT_NAME}.lv2 ${GITVERSION}\$\\r\$\\nLV2 Plugins.\$\\r\$\\n${BUILDDATE}"
LangString DESC_SecJACK \${LANG_ENGLISH} "Standalone JACK clients and start-menu shortcuts"
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
!insertmacro MUI_DESCRIPTION_TEXT \${SecLV2} \$(DESC_SecLV2)
!insertmacro MUI_DESCRIPTION_TEXT \${SecJACK} \$(DESC_SecJACK)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  SetShellVarContext all
  DeleteRegKey HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\${XREGKEY}"
  DeleteRegKey HKLM SOFTWARE\\RSS\\${XREGKEY}\\$WARCH
  RMDir /r "\$${CMF}\\LV2\\b_synth.lv2"
  RMDir /r "\$${CMF}\\LV2\\b_whirl.lv2"
  Delete "\$INSTDIR\\setBfreeUI.exe"
  Delete "\$INSTDIR\\README.txt"
  Delete "\$INSTDIR\uninstall.exe"
  RMDir "\$INSTDIR"
  Delete "\$SMPROGRAMS\\${PRODUCT_NAME}\\*.*"
  RMDir "\$SMPROGRAMS\\${PRODUCT_NAME}"
SectionEnd
EOF

#cat -n "$NSISFILE"

rm -f ${OUTFILE}
echo " === OutFile: $OUTFILE"

if test -n "$QUICKZIP" ; then
echo " === Building Windows Installer (fast zip)"
else
echo " === Building Windows Installer (lzma compression takes ages)"
fi
time makensis -V2 "$NSISFILE"
rm -rf $DESTDIR
ls -lh "$OUTFILE"
