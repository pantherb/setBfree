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

# start with a clean slate:
if test -z "$NOCLEAN"; then
	rm -rf ${BUILDD}
	rm -rf ${PREFIX}
fi

mkdir -p ${SRCDIR}
mkdir -p ${PREFIX}
mkdir -p ${BUILDD}

unset PKG_CONFIG_PATH
export PKG_CONFIG_PATH=${PREFIX}/lib/pkgconfig
export PREFIX
export SRCDIR

export PATH=$HOME/bin:/usr/bin:/bin:/usr/sbin:/sbin:/usr/local/bin

function autoconfbuild {
set -e
echo "======= $(pwd) ======="
	PATH=${PREFIX}/bin:$PATH \
	CPPFLAGS="-I${PREFIX}/include$CPPFLAGS" \
	CFLAGS="${SBFARCH}${OSXCOMPAT:+ $OSXCOMPAT}$CFLAGS" \
	CXXFLAGS="${SBFARCH}${OSXCOMPAT:+ $OSXCOMPAT}$CXXFLAGS" \
	LDFLAGS="${SBFARCH}${OSXCOMPAT:+ $OSXCOMPAT} -headerpad_max_install_names$LDLAGS" \
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
	LDFLAGS="${SBFARCH}${OSXCOMPAT:+ $OSXCOMPAT} -headerpad_max_install_names$LDLAGS" \
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
#src zlib-1.2.7 tar.gz ftp://ftp.simplesystems.org/pub/libpng/png/src/history/zlib/zlib-1.2.7.tar.gz

src liblo-0.28 tar.gz http://downloads.sourceforge.net/liblo/liblo-0.28.tar.gz
autoconfbuild

src freetype-2.5.3 tar.gz http://download.savannah.gnu.org/releases/freetype/freetype-2.5.3.tar.gz
autoconfbuild -with-harfbuzz=no --with-png=no

src freetype-2.5.3 tar.gz http://download.savannah.gnu.org/releases/freetype/freetype-2.5.3.tar.gz
autoconfbuild --with-harfbuzz=no --with-png=no --enable-static

download ftgl-2.1.3-rc5.tar.gz http://downloads.sourceforge.net/project/ftgl/FTGL%20Source/2.1.3~rc5/ftgl-2.1.3-rc5.tar.gz
cd ${BUILDD}
rm -rf ftgl-2.1.3~rc5
tar xf ${SRCDIR}/ftgl-2.1.3-rc5.tar.gz
cd ftgl-2.1.3~rc5
MAKEFLAGS=ECHO="/bin/echo" \
autoconfbuild --enable-static

src lv2-1.10.0 tar.bz2 http://lv2plug.in/spec/lv2-1.10.0.tar.bz2
wafbuild --no-plugins

################################################################################
cd ${BUILDD}
#rm -rf setBfree
git clone -b master git://github.com/pantherb/setBfree.git || true
cd setBfree

export SBFARCH
export SBFSTACK="$PREFIX"
export OSXCOMPAT
./build-osx.sh
