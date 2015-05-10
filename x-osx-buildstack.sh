#!/bin/sh

# we keep a copy of the sources here:
: ${SRCDIR=$HOME/src/stack}
# actual build location
: ${BUILDD=$HOME/src/sbf_build}
# target install dir:
: ${PREFIX=$HOME/src/sbf_stack}
# concurrency
: ${MAKEFLAGS="-j4"}

case `sw_vers -productVersion | cut -d'.' -f1,2` in
	"10.4")
		echo "Tiger"
		SBFARCH="-arch i386 -arch ppc"
		OSXCOMPAT=""
		;;
	"10.5")
		echo "Leopard"
		SBFARCH="-arch i386 -arch ppc"
		OSXCOMPAT=""
		;;
	"10.6")
		echo "Snow Leopard"
		SBFARCH="-arch i386 -arch ppc -arch x86_64"
		OSXCOMPAT="-isysroot /Developer/SDKs/MacOSX10.5.sdk -mmacosx-version-min=10.5"
		;;
	*)
		echo "**UNTESTED OSX VERSION**"
		echo "if it works, please report back :)"
		SBFARCH="-arch i386 -arch x86_64"
		OSXCOMPAT="-mmacosx-version-min=10.5 -DMAC_OS_X_VERSION_MAX_ALLOWED=1090"
		;;
	esac

################################################################################
set -e

unset PKG_CONFIG_PATH
export PKG_CONFIG_PATH=${PREFIX}/lib/pkgconfig
export PREFIX
export SRCDIR

export PATH=${PREFIX}/bin:$HOME/bin:/usr/local/git/bin/:/usr/bin:/bin:/usr/sbin:/sbin:/usr/local/bin

if test ! -d "${PREFIX}/include" -o -z "$NOSTACK"; then

rm -rf ${BUILDD}
rm -rf ${PREFIX}

mkdir -p ${SRCDIR}
mkdir -p ${PREFIX}
mkdir -p ${BUILDD}

function autoconfbuild {
set -e
echo "======= $(pwd) ======="
	CPPFLAGS="-I${PREFIX}/include$CPPFLAGS" \
	CFLAGS="${SBFARCH}${OSXCOMPAT:+ $OSXCOMPAT}$CFLAGS -fdata-sections -ffunction-sections" \
	CXXFLAGS="${SBFARCH}${OSXCOMPAT:+ $OSXCOMPAT}$CXXFLAGS -fdata-sections -ffunction-sections" \
	LDFLAGS="${SBFARCH}${OSXCOMPAT:+ $OSXCOMPAT} -headerpad_max_install_names -fdata-sections -ffunction-sections -Wl,-dead_strip" \
	./configure --disable-dependency-tracking --prefix=$PREFIX --enable-shared $@
	make $MAKEFLAGS && make install
}


function wafbuild {
set -e
echo "======= $(pwd) ======="
	PATH=${PREFIX}/bin:$PATH \
	CPPFLAGS="-I${PREFIX}/include$CPPFLAGS" \
	CFLAGS="${SBFARCH}${OSXCOMPAT:+ $OSXCOMPAT}$CFLAGS" \
	CXXFLAGS="${SBFARCH}${OSXCOMPAT:+ $OSXCOMPAT}$CXXFLAGS" \
	LDFLAGS="${SBFARCH}${OSXCOMPAT:+ $OSXCOMPAT} -headerpad_max_install_names" \
	./waf configure --prefix=$PREFIX $@ \
	&& ./waf && ./waf install
}

function download {
echo "--- Downloading.. $2"
test -f ${SRCDIR}/$1 || curl -L -o ${SRCDIR}/$1 $2
}

function src {
download ${1}.${2} $3
cd ${BUILDD}
rm -rf $1
tar xf ${SRCDIR}/${1}.${2}
cd $1
}

################################################################################

src m4-1.4.17 tar.gz http://ftp.gnu.org/gnu/m4/m4-1.4.17.tar.gz
autoconfbuild

src pkg-config-0.28 tar.gz http://pkgconfig.freedesktop.org/releases/pkg-config-0.28.tar.gz
./configure --prefix=$PREFIX --with-internal-glib
make $MAKEFLAGS
make install

src autoconf-2.69 tar.xz http://ftp.gnu.org/gnu/autoconf/autoconf-2.69.tar.gz
autoconfbuild
hash autoconf
hash autoreconf

src automake-1.14 tar.gz http://ftp.gnu.org/gnu/automake/automake-1.14.tar.gz
autoconfbuild
hash automake

src libtool-2.4 tar.gz http://ftp.gnu.org/gnu/libtool/libtool-2.4.tar.gz
autoconfbuild
hash libtoolize

src make-4.1 tar.gz http://ftp.gnu.org/gnu/make/make-4.1.tar.gz
autoconfbuild
hash make

################################################################################

download jack_headers.tar.gz http://robin.linuxaudio.org/jack_headers.tar.gz
cd "$PREFIX"
tar xzf ${SRCDIR}/jack_headers.tar.gz
"$PREFIX"/update_pc_prefix.sh

if test ! -d ${SRCDIR}/weakjack.git.reference; then
	git clone --mirror git://github.com/x42/weakjack.git ${SRCDIR}/weakjack.git.reference
fi

cd "$PREFIX"
mkdir src/ && cd src
git clone git://github.com/x42/weakjack.git

################################################################################

src liblo-0.28 tar.gz http://downloads.sourceforge.net/liblo/liblo-0.28.tar.gz
autoconfbuild

#src freetype-2.5.3 tar.gz http://download.savannah.gnu.org/releases/freetype/freetype-2.5.3.tar.gz
#autoconfbuild -with-harfbuzz=no --with-png=no --with-bzip2=no --with-zlib=no

src freetype-2.5.3 tar.gz http://download.savannah.gnu.org/releases/freetype/freetype-2.5.3.tar.gz
autoconfbuild --with-harfbuzz=no --with-png=no --enable-static --with-bzip2=no --with-zlib=no

download ftgl-2.1.3-rc5.tar.gz http://downloads.sourceforge.net/project/ftgl/FTGL%20Source/2.1.3~rc5/ftgl-2.1.3-rc5.tar.gz
cd ${BUILDD}
rm -rf ftgl-2.1.3~rc5
tar xf ${SRCDIR}/ftgl-2.1.3-rc5.tar.gz
cd ftgl-2.1.3~rc5
patch -p1 << EOF
diff --git a/src/FTFont/FTFontGlue.cpp b/src/FTFont/FTFontGlue.cpp
index b23e787..03ee840 100644
--- a/src/FTFont/FTFontGlue.cpp
+++ b/src/FTFont/FTFontGlue.cpp
@@ -57,6 +57,9 @@ C_TOR(ftglCreateBitmapFont, (const char *fontname),
 C_TOR(ftglCreateBufferFont, (const char *fontname),
       FTBufferFont, (fontname), FONT_BUFFER);
 
+C_TOR(ftglCreateBufferFontMem, (const unsigned char *pBufferBytes, size_t bufferSizeInBytes),
+      FTBufferFont, (pBufferBytes,bufferSizeInBytes), FONT_BUFFER);
+
 // FTExtrudeFont::FTExtrudeFont();
 C_TOR(ftglCreateExtrudeFont, (const char *fontname),
       FTExtrudeFont, (fontname), FONT_EXTRUDE);
diff --git a/src/FTGL/FTBufferFont.h b/src/FTGL/FTBufferFont.h
index 15d358d..b3d40ab 100644
--- a/src/FTGL/FTBufferFont.h
+++ b/src/FTGL/FTBufferFont.h
@@ -92,6 +92,7 @@ FTGL_BEGIN_C_DECLS
  * @see  FTGLfont
  */
 FTGL_EXPORT FTGLfont *ftglCreateBufferFont(const char *file);
+FTGL_EXPORT FTGLfont *ftglCreateBufferFontMem(const unsigned char *pBufferBytes, size_t bufferSizeInBytes);
 
 FTGL_END_C_DECLS
EOF
MAKEFLAGS=ECHO="/bin/echo" \
autoconfbuild --enable-static

src lv2-1.10.0 tar.bz2 http://lv2plug.in/spec/lv2-1.10.0.tar.bz2
wafbuild --no-plugins

################################################################################
fi  ## NOSTACK
################################################################################

if test ! -d ${SRCDIR}/setBfree.git.reference; then
	git clone --mirror git://github.com/pantherb/setBfree.git ${SRCDIR}/setBfree.git.reference
fi

cd ${BUILDD}
git clone -b master --single-branch --reference ${SRCDIR}/setBfree.git.reference git://github.com/pantherb/setBfree.git || true
cd setBfree

if git diff-files --quiet --ignore-submodules -- && git diff-index --cached --quiet HEAD --ignore-submodules --; then
	git pull || true
fi

export SBFARCH
export SBFSTACK="$PREFIX"
export OSXCOMPAT
./x-osx-bundle.sh $@
