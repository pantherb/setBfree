#!/bin/bash

export ROBTK=`mktemp -d -t robtkgit.XXXXXX`
trap "rm -rf ${BUNDLEDIR}" EXIT

set -e
pushd $ROBTK
set -e
git clone git://github.com/x42/robtk.git || true
cd robtk
git pull
RTKVERSION=`git describe --tags`
popd

mkdir -p robtk
rsync -va \
	$ROBTK/robtk/ui_gl.c $ROBTK/robtk/robtk.mk $ROBTK/robtk/widgets $ROBTK/robtk/gl \
	$ROBTK/robtk/lv2uisyms $ROBTK/robtk/lv2syms $ROBTK/robtk/robtk.h $ROBTK/robtk/rtk \
	$ROBTK/robtk/jackwrap.c $ROBTK/robtk/jackwrap.mm $ROBTK/robtk/weakjack $ROBTK/robtk/win_icon.rc \
	robtk/
rsync -va $ROBTK/robtk/pugl/ pugl/
echo "exported git://github.com/x42/robtk.git $RTKVERSION" > robtk/README
git add robtk pugl
