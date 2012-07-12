#!/bin/bash

eval `grep "VERSION=" Makefile`

TARGET=/tmp/bpkg
PRODUCTDIR=$TARGET/setBfree.app

make clean
make \
	ENABLE_CONVOLUTION=yes \
	OPTIMIZATIONS="-O3 -arch ppc -arch i386 -arch x86_64" \
	TCLFILE="../Resources/vb3kb.tcl" \
	IRPATH="../Resources/ir" \
	|| exit


mkdir -p $PRODUCTDIR/Contents
mkdir -p $PRODUCTDIR/Contents/MacOS
mkdir -p $PRODUCTDIR/Contents/Resources
mkdir -p $PRODUCTDIR/Contents/Frameworks

cp -v src/setBfree $PRODUCTDIR/Contents/MacOS/setBfree-bin
cp -v vb3kb/vb3kb $PRODUCTDIR/Contents/MacOS/vb3kb

cp -v vb3kb/vb3kb.tcl $PRODUCTDIR/Contents/Resources

cp -v pgm/default.pgm $PRODUCTDIR/Contents/Resources
cp -v doc/setBfree.icns $PRODUCTDIR/Contents/Resources
cp -v doc/dmgbg.png $TARGET/dmgbg.png

cp -av b_conv/ir $PRODUCTDIR/Contents/Resources/


# install shared libraries

follow_dependencies () {
	dependencies=`otool -arch all -L "$1"  | egrep '\/((opt|usr)\/local\/lib|gtk\/inst\/lib)'| awk '{print $1}'`
	for l in $dependencies; do
		libname=`basename $l`
		libpath=`dirname $l`
		echo "$libname" | grep "libjack" >/dev/null && continue 
		if [ ! -f "$PRODUCTDIR/Contents/Frameworks/$libname" ]; then
			deploy_lib $libname $libpath
		fi
		install_name_tool \
			-change $libpath/$libname \
			@executable_path/../Frameworks/$libname \
			"$1"
	done
}

deploy_lib () {
	libname=$1
	libpath=$2
	if [ ! -f "$PRODUCTDIR/Contents/Frameworks/$libname" ]; then
		cp -f "$libpath/$libname" "$PRODUCTDIR/Contents/Frameworks/$libname"
		install_name_tool \
			-id @executable_path/../Frameworks/$libname \
			"$PRODUCTDIR/Contents/Frameworks/$libname"
		follow_dependencies "$PRODUCTDIR/Contents/Frameworks/$libname"
	fi
}

follow_dependencies "$PRODUCTDIR/Contents/MacOS/setBfree-bin"

cat > $PRODUCTDIR/Contents/MacOS/setBfree << EOF
#!/bin/sh

if test ! -x /usr/local/bin/jackd -a ! -x /usr/bin/jackd ; then
  /usr/bin/osascript -e '
    tell application "Finder"
    display dialog "You do not have JACK installed. setBfree will not run without it. See http://jackaudio.org/ for info." buttons["OK"]
    end tell'
  exit 1
fi

progname="\$0"
curdir=\`dirname "\$progname"\`
cd "\${curdir}"
./setBfree-bin -p ../Resources/default.pgm &
RV=\$?
PID=\$!

if test \$PID -lt 1 -o \$RV != 0; then
  /usr/bin/osascript -e '
    tell application "Finder"
    display dialog "Failed to start setBfree. Check the \"console\" for error messages." buttons["OK"]
    end tell'
	exit 1
fi

JACK_LSP=
if test -x /usr/local/bin/jack_lsp; then
	JACK_LSP=/usr/local/bin/jack_lsp
elif test -x /usr/bin/jack_lsp; then
	JACK_LSP=/usr/local/bin/jack_lsp
fi
if test -z "\$JACK_LSP"; then
  sleep 3 # give setBfree time to start and create jack-ports
else
	TIMEOUT=15
	while test -z "\`\$JACK_LSP 2>/dev/null | grep setBfree\`" -a \$TIMEOUT -gt 0 ; do sleep 1; TIMEOUT=\$[ \$TIMEOUT - 1 ]; done
fi

./vb3kb

kill -HUP \$PID
EOF
chmod +x $PRODUCTDIR/Contents/MacOS/setBfree


cat > $PRODUCTDIR/Contents/PkgInfo << EOF
APPL~~~~
EOF

cat > $PRODUCTDIR/Contents/Info.plist << EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple Computer//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleExecutable</key>
	<string>setBfree</string>
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

##############################################################################
#roll a DMG

TMPFILE=/tmp/bxtmp.dmg
DMGFILE=/tmp/setBfree-${VERSION}.dmg
MNTPATH=/tmp/mnt/
VOLNAME=setBfree
APPNAME="setBfree.app"

BGPIC=$TARGET/dmgbg.png

mkdir -p $MNTPATH
if [ -e $TMPFILE -o -e $DMGFILE -o ! -d $MNTPATH ]; then
  echo
  echo "could not make DMG. tmp-file or destination file exists."
  exit;
fi

hdiutil create -megabytes 100 $TMPFILE
DiskDevice=$(hdid -nomount "${TMPFILE}" | grep Apple_HFS | cut -f 1 -d ' ')
newfs_hfs -v "${VOLNAME}" "${DiskDevice}"
mount -t hfs "${DiskDevice}" "${MNTPATH}"

cp -r ${TARGET}/${APPNAME} ${MNTPATH}/

# TODO: remove .svn files..

mkdir ${MNTPATH}/.background
BGFILE=$(basename $BGPIC)
cp -vi ${BGPIC} ${MNTPATH}/.background/${BGFILE}

echo '
   tell application "Finder"
     tell disk "'${VOLNAME}'"
	   open
	   set current view of container window to icon view
	   set toolbar visible of container window to false
	   set statusbar visible of container window to false
	   set the bounds of container window to {400, 200, 800, 440}
	   set theViewOptions to the icon view options of container window
	   set arrangement of theViewOptions to not arranged
	   set icon size of theViewOptions to 64
	   set background picture of theViewOptions to file ".background:'${BGFILE}'"
	   make new alias file at container window to POSIX file "/Applications" with properties {name:"Applications"}
	   set position of item "'${APPNAME}'" of container window to {100, 100}
	   set position of item "Applications" of container window to {310, 100}
	   close
	   open
	   update without registering applications
	   delay 5
	   eject
     end tell
   end tell
' | osascript

sync

# umount the image
umount "${DiskDevice}"
hdiutil eject "${DiskDevice}"

# Create a read-only version, use zlib compression
hdiutil convert -format UDZO "${TMPFILE}" -imagekey zlib-level=9 -o "${DMGFILE}"

# Delete the temporary files
rm $TMPFILE
rmdir $MNTPATH

echo
echo "packaging succeeded."
ls -l $DMGFILE
