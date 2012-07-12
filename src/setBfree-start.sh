#!/bin/bash

pidof setBfree && exit 1

SBF=setBfree
VKB=vb3kb
VKDIR=
JACK_LSP=`which jack_lsp`

progname="$0"
curdir=`dirname "$progname"`

test -x "$curdir/setBfree" && SBF="$curdir/setBfree"

# Allow to run from src folder:
if test -x "$curdir/../vb3kb/vb3kb";then
	VKB="$curdir/../vb3kb/vb3kb"
	VKDIR="$curdir/../vb3kb/"
fi

if test -d "$curdir/../b_conv/ir";then
	export BXIRFILE="$curdir/../b_conv/ir/ir_leslie-%04d.wav"
fi

$SBF $@ &

RV=$?
PID=$!

if test $PID -lt 1 -o $RV != 0; then
	# TODO use zenity of similar x-dialog
	echo "Failed to start setBfree. Check the \"console\" for error messages."
	exit 1
fi

if test -z "$JACK_LSP"; then
	sleep 3
else
	TIMEOUT=15
	while test -z "`$JACK_LSP 2>/dev/null | grep setBfree:midi_in`" -a $TIMEOUT -gt 0 ; do sleep 1; TIMEOUT=$[ $TIMEOUT - 1 ]; done
	if test $TIMEOUT -eq 0; then
		kill -HUP $PID &>/dev/null
		# TODO use zenity of similar x-dialog
		echo "Failed to query JACK ports of setBfree. Check the \"console\" for error messages."
		exit 1
	fi
fi

if test -n "$VKDIR"; then
	export VB3KBTCL="$VKDIR/vb3kb.tcl"
fi

$VKB

kill -HUP $PID
