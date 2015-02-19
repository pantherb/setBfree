#!/bin/bash
# this script creates a static linux version of setbfree
# cross-compiled on GNU/Linux
#
# It is intended to run in a pristine chroot or VM of a minimal
# debian system. see http://wiki.debian.org/cowbuilder
#

: ${XARCH=i686} # or x86_64

: ${MAKEFLAGS=-j4}
: ${SRCDIR=/var/tmp/winsrc}
: ${SRC=/usr/src}

if [ "$(id -u)" != "0" -a -z "$SUDO" ]; then
	echo "This script must be run as root in pbuilder" 1>&2
	echo "e.g sudo DIST=jessie cowbuilder --bindmounts /var/tmp --execute $0"
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

###############################################################################

apt-get -y install build-essential \
	git autoconf automake libtool pkg-config \
	curl unzip ed yasm ca-certificates \
	zip python

if test "$DIST" != "wheezy"; then
  apt-get -y install \
    libx11-dev libxext-dev libxrender-dev \
    libglu-dev
else
  # use old gcc for libstdc++ compat
  apt-get -y install \
    gcc-4.4 g++-4.4 cpp-4.4
  # use old libX11 for backwards compat
	apt-get -y install -t squeeze \
		libx11-dev libxext-dev libxrender-dev \
		libglu-dev

	update-alternatives --remove-all gcc || true
	update-alternatives --remove-all g++ || true

	update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-4.4 60 --slave /usr/bin/g++ g++ /usr/bin/g++-4.4
	update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-4.7 40 --slave /usr/bin/g++ g++ /usr/bin/g++-4.7
fi

set -e

unset PKG_CONFIG_PATH
export PKG_CONFIG_PATH=${PREFIX}/lib/pkgconfig
export PREFIX
export SRCDIR

export PATH=${PREFIX}/bin:/usr/lib/ccache/:/usr/bin:/bin:/usr/sbin:/sbin

function download {
	echo "--- Downloading.. $2"
	test -f ${SRCDIR}/$1 || curl -L -o ${SRCDIR}/$1 $2
}
################################################################################
function src {
	download ${1}.${2} $3
	cd ${BUILDD}
	rm -rf $1
	tar xf ${SRCDIR}/${1}.${2}
	cd $1
}

function autoconfconf {
	set -e
	echo "======= $(pwd) ======="
	CPPFLAGS="-I${PREFIX}/include$CPPFLAGS" \
	CFLAGS="-O3 -fvisibility=hidden -fPIC" \
	CXXFLAGS="-O3 -fvisibility=hidden -fPIC" \
	LDFLAGS="-L${PREFIX}/lib -fvisibility=hidden" \
	./configure \
	--disable-shared --enable-static \
	--prefix=$PREFIX $@
}

function autoconfbuild {
	set -e
	autoconfconf $@
	make $MAKEFLAGS
	make install
}

################################################################################

mkdir -p ${SRCDIR}
mkdir -p ${PREFIX}
mkdir -p ${BUILDD}

### jack headers, and pkg-config file from jackd 1.9.10
### this is a 'convenient' re-zip from official jack releases:
### http://jackaudio.org

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

src liblo-0.28 tar.gz http://downloads.sourceforge.net/liblo/liblo-0.28.tar.gz
autoconfbuild

src freetype-2.5.3 tar.gz http://download.savannah.gnu.org/releases/freetype/freetype-2.5.3.tar.gz
autoconfbuild --with-harfbuzz=no --with-png=no --with-bzip2=no --with-zlib=no

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
./waf configure --prefix=$PREFIX --no-plugins
./waf
./waf install

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

./x-lnx-bundle.sh $@
