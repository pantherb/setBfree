#!/bin/bash
# this script creates a windows version of setbfree
# cross-compiled on GNU/Linux
#
# It is intended to run in a pristine chroot or VM of a minimal
# debian system. see http://wiki.debian.org/cowbuilder
#

: ${XARCH=i686} # or x86_64

: ${MAKEFLAGS=-j4}
: ${STACKCFLAGS="-O3"}

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
	echo "Target: 64bit Windows (x86_64)"
	XPREFIX=x86_64-w64-mingw32
	HPREFIX=x86_64
	WARCH=w64
	DEBIANPKGS="mingw-w64"
else
	echo "Target: 32 Windows (i686)"
	XPREFIX=i686-w64-mingw32
	HPREFIX=i386
	WARCH=w32
	DEBIANPKGS="gcc-mingw-w64-i686 g++-mingw-w64-i686 mingw-w64-tools mingw32"
fi

: ${PREFIX=$SRC/win-stack-$WARCH}
: ${BUILDD=$SRC/win-build-$WARCH}

###############################################################################

apt-get -y install build-essential \
	${DEBIANPKGS} \
	git autoconf automake libtool pkg-config \
	curl unzip ed yasm ca-certificates \
	zip python

#fixup mingw64 ccache for now
if test -d /usr/lib/ccache -a -f /usr/bin/ccache; then
	export PATH="/usr/lib/ccache:${PATH}"
	cd /usr/lib/ccache
	test -L ${XPREFIX}-gcc || ln -s ../../bin/ccache ${XPREFIX}-gcc
	test -L ${XPREFIX}-g++ || ln -s ../../bin/ccache ${XPREFIX}-g++
fi

###############################################################################

mkdir -p ${SRCDIR}
mkdir -p ${PREFIX}
mkdir -p ${BUILDD}

unset PKG_CONFIG_PATH
export XPREFIX
export PKG_CONFIG_PATH=${PREFIX}/lib/pkgconfig
export PREFIX
export SRCDIR
export XARCH

if test -n "$(which ${XPREFIX}-pkg-config)"; then
	export PKG_CONFIG=`which ${XPREFIX}-pkg-config`
fi

function download {
	echo "--- Downloading.. $2"
	test -f ${SRCDIR}/$1 || curl -k -L -o ${SRCDIR}/$1 $2
}

set -e

###############################################################################

function src {
	download ${1}.${2} $3
	cd ${BUILDD}
	rm -rf $1
	tar xf ${SRCDIR}/${1}.${2}
	cd $1
}

function autoconfbuild {
set -e
echo "======= $(pwd) ======="
	CPPFLAGS="-I${PREFIX}/include -DPTW32_STATIC_LIB$CPPFLAGS" \
	CFLAGS="-I${PREFIX}/include ${STACKCFLAGS} -fvisibility=hidden -DPTW32_STATIC_LIB -mstackrealign" \
	CXXFLAGS="-I${PREFIX}/include ${STACKCFLAGS} -fvisibility=hidden -DPTW32_STATIC_LIB -mstackrealign" \
	LDFLAGS="-L${PREFIX}/lib -fvisibility=hidden" \
	./configure --host=${XPREFIX} --build=${HPREFIX}-linux \
	--disable-shared --enable-static \
	--prefix=$PREFIX $@
	make $MAKEFLAGS && make install
}


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


download pthreads-w32-2-9-1-release.tar.gz ftp://sourceware.org/pub/pthreads-win32/pthreads-w32-2-9-1-release.tar.gz
cd ${BUILDD}
rm -rf pthreads-w32-2-9-1-release
tar xzf ${SRCDIR}/pthreads-w32-2-9-1-release.tar.gz
cd pthreads-w32-2-9-1-release
make clean GC-static CROSS=${XPREFIX}-
mkdir -p ${PREFIX}/bin
mkdir -p ${PREFIX}/lib
mkdir -p ${PREFIX}/include
# fix some compiler warnings
ed sched.h << EOF
/define PTW32_SCHED_LEVEL PTW32_SCHED_LEVEL_MAX
i
#undef PTW32_SCHED_LEVEL
.
wq
EOF
ed pthread.h << EOF
/define PTW32_LEVEL PTW32_LEVEL_MAX
i
#undef PTW32_LEVEL
.
wq
EOF
cp -vf libpthreadGC2.a ${PREFIX}/lib/libpthread.a
cp -vf pthread.h sched.h ${PREFIX}/include

################################################################################

src liblo-0.28 tar.gz http://downloads.sourceforge.net/liblo/liblo-0.28.tar.gz
autoconfbuild

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
patch -p1 << EOF
--- a/configure	2008-06-12 14:32:56.000000000 +0000
+++ b/configure	2015-02-12 06:02:48.111065082 +0000
@@ -22529,7 +22529,7 @@
         LIBS="\$with_gl_lib"
     fi
 else
-    LIBS="-lGL"
+    LIBS="-lopengl32 -lglu32"
 fi
 cat >conftest.\$ac_ext <<_ACEOF
 /* confdefs.h.  */
@@ -22544,11 +22544,11 @@
 #ifdef __cplusplus
 extern "C"
 #endif
-char glBegin ();
+#include <GL/glu.h>
 int
 main ()
 {
-return glBegin ();
+glBegin(GL_POINTS)
   ;
   return 0;
 }
@@ -23000,7 +23000,7 @@
 
 { echo "\$as_me:\$LINENO: checking for GLU library" >&5
 echo \$ECHO_N "checking for GLU library... \$ECHO_C" >&6; }
-LIBS="-lGLU \$GL_LIBS"
+LIBS="-lopengl32 -lglu32"
 cat >conftest.\$ac_ext <<_ACEOF
 /* confdefs.h.  */
 _ACEOF
@@ -23014,7 +23014,7 @@
 #ifdef __cplusplus
 extern "C"
 #endif
-char gluNewTess ();
+#include <GL/glu.h>
 int
 main ()
 {
EOF
MAKEFLAGS=ECHO="/bin/echo" PATH=$PREFIX/bin:$PATH \
autoconfbuild --enable-static

src lv2-1.10.0 tar.bz2 http://lv2plug.in/spec/lv2-1.10.0.tar.bz2
CC=${XPREFIX}-gcc \
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

./x-win-bundle.sh $@
