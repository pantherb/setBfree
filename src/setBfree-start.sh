#!/bin/bash

if test -n "`pidof setBfree > /dev/null`"; then
	echo "setBfree is already running"
	exit 1
fi

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

$SBF "$@" &

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
	JM=`$JACK_LSP 2>/dev/null | grep setBfree:midi_in`
	AM=`aconnect -o -l 2>/dev/null | grep setBfree | sed 's/^client \([0-9]*\):.*$/\1/'`
	while test -z "$AM" -a -z "$JM" -a $TIMEOUT -gt 0 ; do
		JM=`$JACK_LSP 2>/dev/null | grep setBfree:midi_in`
		AM=`aconnect -o -l 2>/dev/null | grep setBfree | sed 's/^client \([0-9]*\):.*$/\1/'`
		sleep 1; TIMEOUT=$[ $TIMEOUT - 1 ];
	done
	if test $TIMEOUT -eq 0; then
		kill -HUP $PID &>/dev/null
		# TODO use zenity of similar x-dialog
		echo "Failed to query MIDI port of setBfree. Check the \"console\" for error messages."
		exit 1
	fi
fi

if test -n "$AM"; then
	VKOPTS="$VKOPTS --driver=alsa --port=$AM"
fi

if test -n "$VKDIR"; then
	export VB3KBTCL="$VKDIR/vb3kb.tcl"
fi

$VKB $VKOPTS

kill -HUP $PID
