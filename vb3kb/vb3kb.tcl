# Virtual Organ
# based on
# Virtual Tiny Keyboard
#
# Copyright (c) 2012 by Robin Gareus <robin@gareus.org>
# Copyright (c) 1997-2007 by Takashi Iwai
#
# turn off auto-repeat on your X display by "xset -r"
#

#----------------------------------------------------------------
# default files
#----------------------------------------------------------------

#----------------------------------------------------------------
# keyboard size (width & height)

set keywid 18
set keyhgt 72

set barwid 17
set barhgt 77

#----------------------------------------------------------------
# Drawbar defaults

set preset(0) { 8 8 8  0 0 0 0  0 0 }
set preset(1) { 8 3 8  0 0 0 0  0 0 }
set preset(2) { 8 0 6  0 0 0 0  0 0 }

#----------------------------------------------------------------
# keymap en-us
set keymap(0) {
    {1 20} {q 21} {2 22} {w 23}
    {e 24} {4 25} {r 26} {5 27} {t 28} {y 29} {7 30} {u 31} {8 32} {i 33} {9 34} {o 35} {p 36}
}
set keymap(1) {
    {a 20} {z 21} {s 22} {x 23}
    {c 24} {f 25} {v 26} {g 27} {b 28} {n 29} {j 30} {m 31} {k 32} {comma 33} {l 34} {period 35} {slash 36}
}
set keymap(2) { }

#----------------------------------------------------------------
# preset names copies from built-in pgm/default.pgm
set programpresets {
  {1 "Jazz 1 all"}
  {2 "Jazz 2"}
  {3 "Jazz 3"}
  {61 "Brassy pedals"}
  {62 "Hollow pedals"}
  {63 "Cute pedals"}
  {82 "U:Stopped Flute"}
  {83 "U:Dulciana"}
  {84 "U:French Horn"}
  {85 "U:Salicional"}
  {86 "U:Flutes 8'&4'"}
  {87 "U:Oboe Horn"}
  {88 "U:Swell Diapaso"}
  {89 "U:Trumpet"}
  {90 "U:Full Swell"}
  {91 "U:French Horn 8"}
  {92 "U:Tibias 8'&4'"}
  {93 "U:Clarinet 8'"}
  {94 "U:Novel Solo 8'"}
  {95 "U:Theatre Solo"}
  {64 "L:Cello"}
  {65 "L:Flute & Strin"}
  {66 "L:Clarinet"}
  {67 "L:Salicional"}
  {68 "L:Great no reed"}
  {69 "L:Open Diaposon"}
  {70 "L:Full Great"}
  {71 "L:Tibia Clausa"}
  {72 "L:Full Great wi"}
  {73 "L:Cello 8'"}
  {74 "L:Dulciana 8'"}
  {75 "L:Vibraharp 8'"}
  {76 "L:Vox 8' & Tibi"}
  {77 "L:String Accomp"}
  {78 "L:Open Diapason"}
  {79 "L:Full Accomp."}
  {80 "L:Tibia 8'"}
  {81 "L:Bombarde 16'"}
  {57 "CTRL: Lowr/Upr Split"}
  {58 "CTRL: Pd/Lw/Up Split"}
  {59 "CTRL: Pdal/Upr Split"}
  {60 "CTRL: No Split"}
}

#----------------------------------------------------------------

proc NewControl {w ctrl} {
    global curctrl ctrlval
    set type [lindex $ctrl 0]
    #set curctrl $type
    #$w configure -text [lindex $ctrl 1]
    set curctrl -1
    $w configure -text "PGM"
    SeqProgram 0 0 $type
}

#----------------------------------------------------------------
# create virtual keyboard

proc DrawBarDrag {w y channel basecc i} {
  set y [$w canvasy $y]
  if {$y < 10} {set y 10}
  if {$y > 74} {set y 74}

  if       {$y < 14 } {set y 10
  } elseif {$y < 22 } {set y 19
  } elseif {$y < 30 } {set y 27
  } elseif {$y < 38 } {set y 35
  } elseif {$y < 46 } {set y 43
  } elseif {$y < 54 } {set y 51
  } elseif {$y < 62 } {set y 59
  } elseif {$y < 70 } {set y 67
  } else              {set y 74 }

  set val [expr (74 - $y) *2]
  if {$val > 127} {set val 127}

  DrawBar $w $channel $basecc $i $val
}

proc DrawBar {w channel basecc i val} {

  set ID [$w find withtag "db$channel,$i"]
  set coords [$w coords $ID]
  set b [expr 8 * (127 - $val ) / 127 ]
  set y [expr 10 + $b * 8]

  $w coords $ID [lset coords 3 [expr $y + 3]]

  set ID [$w find withtag "dx$channel,$i"]
  set coords [$w coords $ID]
  $w coords $ID [lset coords 1 [expr $y - 4]]
  $w coords $ID [lset coords 3 [expr $y + 4]]

  SeqControl $channel [expr $basecc + $i ] $val
  BCtrlBarText $w $channel $i $b
}

proc BCtrlBarText {w c i l} {
  global barwid
  set colormap(0) { white white black black white black white white black }
  set colormap(1) { white white black black white black white white black }
  set colormap(2) { white white white black white black white white black }

  set x1 [expr 6 + $barwid * ($i + 21 - $c * 10)]

  for {set y 1} {$y < 9} {incr y} {
    set ID [$w find withtag "dbn$c,$i,$y"]
      $w delete $ID
  }
  for {set y 1} {$y <= $l} {incr y} {
    set y1 [expr 78 - 8*( $y + 8 - $l ) ]
    $w create text $x1 $y1 -anchor s -font {courier 8} -fill [lindex $colormap($c) $i] -text "$y" -tags "dbn$c,$i,$y"
  }
}

proc BCtrlCreate {w channel basecc} {
    global barwid barhgt

    set colormap(0) { SaddleBrown SaddleBrown white white black white black black white }
    set colormap(1) { SaddleBrown SaddleBrown white white black white black black white }
    set colormap(2) { SaddleBrown IndianRed4 SaddleBrown gray80 gray30 gray80 gray30 gray30 gray80 }

    set barname { "16" "5\u2153" "8" "4" "2\u2154" "2" "1\u2157" "1\u2153" "1" }
    set manname { "Upper" "Lower" "Pedals" }

    $w create text [expr $barwid * (25.5 - $channel *10)] [expr 10 + $barhgt / 2 ] -font {courier 14} -fill gray -text [lindex $manname	 $channel]

    for {set i 0} {$i < 9} {incr i} {
      set x1 [expr $barwid *( $i + 21 - $channel * 10 )]
      set x2 [expr $x1 + $barwid - 5 ]
      set y1 0
      set y2 $barhgt
      set id1 [$w create rectangle $x1 $y1 $x2 $y2 -width 1\
	      -fill [lindex $colormap($channel) $i] -outline lightgray -tags "db$channel,$i" ]

      set xx1 [expr $x1 - 1 ]
      set xx2 [expr $x2 + 2 ]
      set yy1 [expr $y2 - 4 ]
      set yy2 [expr $y2 + 4 ]
      set id2 [$w create rectangle $xx1 $yy1 $xx2 $yy2 -width 0\
	      -fill [lindex $colormap($channel) $i] -outline lightgray -tags "dx$channel,$i" ]

      BCtrlBarText $w $channel $i 8
      $w create text [expr $x1 + 5 ] [expr $y2 + 17 + ($i%2)*8 ] -anchor s -font {courier 9} -fill white -text [lindex $barname $i]
      SeqControl $channel [expr $basecc + $i ] 0
      $w bind $id1 <B1-Motion> [list DrawBarDrag %W %y $channel $basecc $i ]
      $w bind $id2 <B1-Motion> [list DrawBarDrag %W %y $channel $basecc $i ]
    }
}

proc KeybdCreate {w c numkeys} {
    global keycolor keywid keyitem keyindex keyhgt keymap keywin

    set keywin($c) $w

    canvas $w -width [expr $keywid * $numkeys * 7 / 12] -height $keyhgt -bd 2 -bg black -highlightthickness 0 -relief sunken

    pack $w -side top
    for {set i 0} {$i < $numkeys} {incr i} {
	set octave [expr ($i / 12) * 7]
	set j [expr $i % 12]
	if {$j >= 5} {incr j}
	if {$j % 2 == 0} {
	    set x1 [expr ($octave + $j / 2) * $keywid]
	    set x2 [expr $x1 + $keywid]
	    set y1 0
	    set y2 $keyhgt
	    set id [$w create rectangle $x1 $y1 $x2 $y2 -width 1\
		    -fill white -outline black -tags ebony]
	    set keycolor($c,$i) white
	} else {
	    set x1 [expr ($octave + $j / 2) * $keywid + $keywid * 6/10]
	    set x2 [expr $x1 + $keywid * 8/10 - 1]
	    set y1 0
	    set y2 [expr $keyhgt * 6 / 10]
	    set id [$w create rectangle $x1 $y1 $x2 $y2 -width 1\
		    -fill black -outline white -tags ivory]
	    set keycolor($c,$i) black
	}
	set keyitem($c,$i) $id
	set keyindex($c,$id) $i
	$w bind $id <Button-1> [list KeyStart $c $i 1]
	$w bind $id <ButtonRelease-1> [list KeyStop $c $i 1]
	$w bind $id <Motion> [list KeyMotion $c $i %x %y %s]
    }
    $w lower ebony

    foreach i $keymap($c) {
	set key [lindex $i 0]
	set note [lindex $i 1]
	bind . <KeyPress-$key> [list KeyQueue $c 1 $note 0]
	bind . <KeyRelease-$key> [list KeyQueue $c 0 $note 0]
	set upperkey [string toupper $key]
	if {[string length $key] == 1 && $upperkey != $key} {
	    bind . <KeyPress-$upperkey> [list KeyQueue $c 1 $note 0]
	    bind . <KeyRelease-$upperkey> [list KeyQueue $c 0 $note 0]
	}
    }
}

#----------------------------------------------------------------
# key press/release, filter autorepeats
#
# a nice hack by Roger E Critchlow Jr <rec@elf.org>
#

set KeyEventQueue {}

proc KeyQueue {channel type note time} {
    global KeyEventQueue
    lappend KeyEventQueue $channel $type $note $time
    after idle KeyProcessQueue
}

proc KeyProcessQueue {} {
    global KeyEventQueue
    while {1} {
	switch [llength $KeyEventQueue] {
	    0 return
	    4 {
		foreach {channel type note time} $KeyEventQueue break
		KeyProcess $channel $type $note $time
		set KeyEventQueue [lrange $KeyEventQueue 4 end]
	    }
	    default {
		foreach {channel type1 note1 time1 channel type2 note2 time2} $KeyEventQueue break
		if {$note1 == $note2 && $time1 == $time2 && $type1 == 0 && $type2 == 1} {
		    set KeyEventQueue [lrange $KeyEventQueue 8 end]
		    continue;
		} else {
		    KeyProcess $channel $type1 $note1 $time1
		    set KeyEventQueue [lrange $KeyEventQueue 4 end]
		}
	    }
	}
    }
}

proc KeyProcess {channel type note time} {
    if {$type} {
	KeyStart $channel $note 0
    } else {
	KeyStop $channel $note 0
    }
}

#----------------------------------------------------------------
# note on/off

# base key note and default velocity
set keybase(0) 36
set keybase(1) 36
set keybase(2) 24

set keyvel 127

proc KeyStart {channel key button} {
    global keybase keywin keyitem keyvel activekey
    SeqOn
    catch {
	if {$button == 1} {
	    set activekey($channel) $keyitem($channel,$key)
	}
	$keywin($channel) itemconfigure $keyitem($channel,$key) -fill blue
    }
    set key [expr $key + $keybase($channel)]
    SeqStartNote $channel $key $keyvel
}

proc KeyStop {channel key button} {
    global keybase keywin keyitem keyindex keycolor activekey
    SeqOn
    catch {
	if {$button == 1 && [info exists activekey($channel)] } {
	    set key $keyindex($channel,$activekey($channel))
	    unset activekey($channel)
	}
	$keywin($channel) itemconfigure $keyitem($channel,$key) -fill $keycolor($channel,$key)
    }
    set key [expr $key + $keybase($channel)]
    SeqStopNote $channel $key 0
}

proc KeyMotion {channel key x y s} {
    global activekey keywin keyitem keyindex
    if {($s & 256) == 0} {
	if {[info exists activekey($channel)]} {
	    KeyStop $channel $keyindex($channel,$activekey($channel)) 1
	}
	return
    }
    set new [lindex [$keywin($channel) find overlapping $x $y $x $y] end]

    set old " "
    if {[info exists activekey($channel)]} {
      set old $activekey($channel)
    }

    if {$new != $old} {
	if {[info exists activekey($channel)]} {
	    KeyStop $channel $keyindex($channel,$activekey($channel)) 1
	}
	if {$new != ""} {
	    KeyStart $channel $keyindex($channel,$new) 1
	}
    }
}

#----------------------------------------------------------------
# create windows

# create the main menu
proc MenuCreate {{pw ""}} {
    set w $pw.menubar
    menu $w -tearoff 0

    $w add cascade -menu $w.file -label "File" -underline 0
    menu $w.file -tearoff 0
    $w.file add command -label "Quit" -command {exit 0} -underline 0
}

proc toggleperc {} { global percussion; SeqControl 0 80 [expr $percussion * 64 ] }

proc togglevibl {} {
  global viblonoff;
  if {$viblonoff == 1} { SeqProgram 0 0 48
  } else               { SeqProgram 0 0 49
  }
}

proc togglevibu {} {
  global vibuonoff;
  if {$vibuonoff == 1} { SeqProgram 0 0 50
  } else               { SeqProgram 0 0 51
  }
}

proc togglexov {} {
  global overdrive;
  if {$overdrive == 1} { SeqProgram 0 0 41
  } else               { SeqProgram 0 0 40
  }
}

proc toggleconv {} {
  global convolution;
  if {$convolution == 1} { SeqControl 0 94 127 ;#  SeqControl 0 30 64; # SeqControl 0 27 127
  } else                 { SeqControl 0 94 0 ;  #  SeqControl 0 30 24; # SeqControl 0 27 0
  }
}

proc setVolume {v} {
  SeqControl 0 1 $v
}

proc setDistChacater {v} {
  SeqControl 0 93 $v
}

proc AboutWindow {} {
  if {[winfo exists .t]} {
    raise .t
    return
  }
  tk::toplevel .t
  wm title .t "About setBfree"
  wm iconname .t "About setBfree"
  wm transient .t .

  panedwindow .t.pnd -orient v -opaqueresize 0
  message .t.txt -text "
setBfree - DSP tonewheel organ

http://setbfree.org/
https://github.com/pantherb/setBfree

Copyright \xa9 2003, 2004, 2008-2012
Fredrik Kilander <fk@dsv.su.se>
Robin Gareus <robin@gareus.org>
Ken Restivo <ken@restivo.org>
Will Panther <pantherb@setbfree.org>


vb3kb - virtual B3 Keyboard

Copyright \xa9 1997-2000, 2012
Takashi Iwai <tiwai@suse.de>
Robin Gareus <robin@gareus.org>

Use and redistribute under the terms of the GNU General Public License
" \
  -justify center -aspect 400 -border 2 -bg white -relief groove

  button .t.btn -relief raised -text "Close" -padx 0 -command [list destroy .t] -default active

  .t.pnd add .t.txt
  .t.pnd add .t.btn
  pack .t.pnd -fill both -expand 1
  raise .t
  bind .t <Visibility> "focus .t.btn"
  bind .t <Key-Escape> "destroy .t"
  bind .t <Key-Return> "destroy .t"
  tk::PlaceWindow .t widget .
}

proc menupgm {w t v} {
  global $v
  $w configure -text $t
  SeqProgram 0 0 [expr $$v]
}

# create the virtual keyboard panel
proc PanelCreate {{pw ""}} {
    global controls curctrl ctrlval barwid preset
    #$pw. configure -menu $pw.menubar

    frame $pw.tp
    set w $pw.tp.bars

    canvas $w -width [expr 30 * $barwid + 5 ] -height 100 -bd 1 -bg darkgray -bd 1 -highlightthickness 0 -relief sunken

    BCtrlCreate $w 0 70
    BCtrlCreate $w 1 70
    BCtrlCreate $w 2 70

    foreach c { 0 1 2 } {
      for {set i 0} { $i < [llength $preset($c)]} {incr i} {
	set v [expr int(127 - 127 * [ lindex $preset($c) $i ] / 8.0) ]
	DrawBar $w $c 70 $i $v
      }
    }

    scale $pw.tp.vol -orient vertical -from 127 -to 0 -showvalue false -command "setVolume" -bd 1 -relief sunken
    $pw.tp.vol set 127

    frame $pw.tp.rbtn
    button $pw.tp.rbtn.about -relief raised -text "About" -padx 0 -command "AboutWindow"

    set w $pw.ctrl
    frame $w

    set w $pw.ctrl.c1
    frame $w

    button $w.lesliespd -text "Leslie Spd" -command "SeqControl 0 64 1"
    checkbutton $w.percussion -indicatoron 0 -pady 4 -text "Percussion:" -command "toggleperc"
    checkbutton $w.viblonoff  -indicatoron 0 -pady 4 -text "Vibrato (L)," -command "togglevibl"
    checkbutton $w.vibuonoff  -indicatoron 0 -pady 4 -text "Vibrato (U):" -command "togglevibu"

    menubutton $w.percmode -relief raised -width 8 -menu $w.percmode.m
    menu $w.percmode.m -tearoff 0
    $w.percmode.m add radio -label "soft" -variable gpercmode -value 34 -command [list menupgm $w.percmode "soft" gpercmode]
    $w.percmode.m add radio -label "norm" -variable gpercmode -value 35 -command [list menupgm $w.percmode "norm" gpercmode]

    menubutton $w.percspd -relief raised -width 8 -menu $w.percspd.m
    menu $w.percspd.m -tearoff 0
    $w.percspd.m add radio -label "fast" -variable gpercspd -value 36 -command [list menupgm $w.percspd "fast" gpercspd]
    $w.percspd.m add radio -label "slow" -variable gpercspd -value 37 -command [list menupgm $w.percspd "slow" gpercspd]

    menubutton $w.percharm -relief raised -width 7 -menu $w.percharm.m
    menu $w.percharm.m -tearoff 0
    $w.percharm.m add radio -label "2nd" -variable gpercharm -value 38 -command [list menupgm $w.percharm "2nd" gpercharm]
    $w.percharm.m add radio -label "3rd" -variable gpercharm -value 39 -command [list menupgm $w.percharm "3rd" gpercharm]

    menubutton $w.vibrate -relief raised -width 5 -menu $w.vibrate.m
    menu $w.vibrate.m -tearoff 0
    $w.vibrate.m add radio -label "v1" -variable gvibrate -value 42 -command [list menupgm $w.vibrate "v1" gvibrate]
    $w.vibrate.m add radio -label "v2" -variable gvibrate -value 43 -command [list menupgm $w.vibrate "v2" gvibrate]
    $w.vibrate.m add radio -label "v3" -variable gvibrate -value 44 -command [list menupgm $w.vibrate "v3" gvibrate]
    $w.vibrate.m add radio -label "c1" -variable gvibrate -value 45 -command [list menupgm $w.vibrate "c1" gvibrate]
    $w.vibrate.m add radio -label "c2" -variable gvibrate -value 46 -command [list menupgm $w.vibrate "c2" gvibrate]
    $w.vibrate.m add radio -label "c3" -variable gvibrate -value 47 -command [list menupgm $w.vibrate "c3" gvibrate]

    set w $pw.ctrl.c2
    frame $w

    menubutton $w.leslie -relief raised -width 12 -menu $w.leslie.m
    menu $w.leslie.m -tearoff 0
    $w.leslie.m add radio -label "Leslie off"  -variable gleslie -value 52 -command [list menupgm $w.leslie "Leslie off" gleslie]
    $w.leslie.m add radio -label "Leslie slow" -variable gleslie -value 53 -command [list menupgm $w.leslie "Leslie slow" gleslie]
    $w.leslie.m add radio -label "Leslie fast" -variable gleslie -value 54 -command [list menupgm $w.leslie "Leslie fast" gleslie]


    checkbutton $w.overdrive  -indicatoron 0 -pady 4 -text "Overdrive:" -command "togglexov"
    checkbutton $w.convolution  -indicatoron 0 -pady 4 -text "SpeakerSimulation" -command "toggleconv"

    menubutton $w.reverb -relief raised -width 12 -menu $w.reverb.m
    menu $w.reverb.m -tearoff 0
    $w.reverb.m add radio -label "Reverb off" -variable greverb -value 27 -command [list menupgm $w.reverb "Reverb off" greverb]
    $w.reverb.m add radio -label "Reverb 16%" -variable greverb -value 28 -command [list menupgm $w.reverb "Reverb 16%" greverb]
    $w.reverb.m add radio -label "Reverb 25%" -variable greverb -value 29 -command [list menupgm $w.reverb "Reverb 25%" greverb]
    $w.reverb.m add radio -label "Reverb 33%" -variable greverb -value 30 -command [list menupgm $w.reverb "Reverb 33%" greverb]
    $w.reverb.m add radio -label "Reverb 50%" -variable greverb -value 31 -command [list menupgm $w.reverb "Reverb 50%" greverb]

    scale $w.dist -orient horizontal -from 0 -to 127 -showvalue false -command "setDistChacater"

    menubutton $pw.tp.rbtn.pctrl -relief raised -width 8 -menu $pw.tp.rbtn.pctrl.m -padx 0
    menu $pw.tp.rbtn.pctrl.m -tearoff 0
    global ctrlval programpresets
    foreach i $programpresets {
	set type [lindex $i 0]
	set label [lindex $i 1]
	$pw.tp.rbtn.pctrl.m add radio -label $label -variable curctrl -value $type -command [list NewControl $pw.tp.rbtn.pctrl $i]
	set ctrlval($type) [lindex $i 2]
    }
    NewControl $pw.tp.rbtn.pctrl [lindex $programpresets 0]

    # initialize checkbox and slider values
    global gleslie gpercmode gpercspd gvibrate greverb convolution percmode
    set gleslie 52; set gpercmode 35; set gpercspd 37; set gvibrate 42; set greverb 27; set gpercharm 39

    menupgm $w.reverb "Reverb off" greverb
    menupgm $w.leslie "Leslie off" gleslie

    set w $pw.ctrl.c1
    menupgm $w.percmode "norm" gpercmode
    menupgm $w.percspd "slow" gpercspd
    menupgm $w.vibrate "v1" gvibrate
    menupgm $w.percharm "3rd" gpercharm
    toggleperc
    togglexov
    togglevibu
    togglevibl
    toggleconv

    # arrange elements

    pack $pw.tp.rbtn.about $pw.tp.rbtn.pctrl -side top -padx 3 -pady 3 -fill both
    pack $pw.tp.vol $pw.tp.bars $pw.tp.rbtn -side left
    pack $pw.tp -side top

    set w $pw.ctrl.c1
    pack $w.percussion $w.percmode $w.percspd $w.percharm $w.viblonoff $w.vibuonoff $w.vibrate -fill x -side left
    pack $w -side top

    set w $pw.ctrl.c2
    pack $w.overdrive $w.dist $w.reverb $w.leslie $w.convolution -fill x -side left
    pack $w -side top

    set w $pw.ctrl
    pack $w -side top

    KeybdCreate $pw.kbp 2 [expr 2 * 12]
    pack $pw.kbp -side bottom -pady 1

    KeybdCreate $pw.kbd 1 [expr 5 * 12 + 1]
    pack $pw.kbd -side bottom -pady 1

    KeybdCreate $pw.kbl 0 [expr 5 * 12 + 1]
    pack $pw.kbl -side bottom -pady 1

}

proc setCCparam {v} {
  global ccparam
  set ccparam $v
}

proc sendCC {v} {
  global ccchannel ccparam
  #puts "$ccchannel $ccparam $v"
  SeqControl $ccchannel $ccparam $v
}

proc AdvancedCreate {{pw ""}} {
  global ccchannel ccparam
  set ccchannel 0
  set ccparam 127

  frame $pw.adv
  set w $pw.adv
  menubutton $w.ccchannel -relief raised -width 9 -menu $w.ccchannel.m
  menu $w.ccchannel.m -tearoff 0
  for {set c 0} {$c < 16} {incr c} {
    $w.ccchannel.m add radio -label "$c" -variable gccchannel -value $c -command "global ccchannel; set ccchannel $c; $w.ccchannel configure -text $c"
  }
  $w.ccchannel configure -text $ccchannel

  #menubutton $w.ccparam -relief raised -width 9 -menu $w.ccparam.m
  #menu $w.ccparam.m -tearoff 0
  #for {set c 1} {$c < 127} {incr c} {
  #  $w.ccparam.m add radio -label "$c" -variable gccparam -value $c -command "global ccparam; set ccparam $c; $w.ccparam configure -text $c"
  #}
  #$w.ccparam configure -text $ccparam

  scale $w.ccparam -orient horizontal -from 0 -to 127 -showvalue true -width 10 -length 164 -command "setCCparam"
  scale $w.ccvalue -orient horizontal -from 0 -to 127 -showvalue true -width 10 -length 164 -command "sendCC"

  label $w.lc -text "Chn:"
  label $w.lp -text "Par:"
  label $w.lv -text "Val:"

  pack $w.lc $w.ccchannel $w.lp $w.ccparam $w.lv $w.ccvalue -side left -anchor s
  pack $w -side top
  pack $pw.adv -side bottom -pady 1
}

#
# parse command line options
#
proc ParseOptions {argc argv} {
    global optvar

    for {set i 0} {$i <= $argc} {incr i} {
	set arg [lindex $argv $i]
	if {! [string match -* $arg]} {break}
	set arg [string range $arg 2 end]
	if {[string match *=* $arg]} {
	    set idx [string first = $arg]
	    set val [string range $arg [expr $idx + 1] end]
	    set arg [string range $arg 0 [expr $idx - 1]]
	    if {[info exists optvar($arg)]} {
		set optvar($arg) $val
	    } else {
		usage
		exit 1
	    }
	} elseif { $arg == "help" } {
	  usage
	  exit 0
	} elseif { $arg == "version" } {
	  version
	  exit 0
	} else {
	    if {[info exists optvar($arg)]} {
		incr i
		set optvar($arg) [lindex $argv $i]
	    } else {
		usage
		exit 1
	    }
	}
    }
}

#----------------------------------------------------------------
# main ...
#

# --advanced option -- undocumented
set optvar(advanced) false

ParseOptions $argc $argv

SeqOn preinit

#MenuCreate
PanelCreate

if { $optvar(advanced) != false} {
  AdvancedCreate
}

SeqProgram 0 0 60 # no split

wm title . "setBfree - DSP tonewheel organ"
wm iconname . "setBfree"
wm resizable . false false

catch {
  if {[tk windowingsystem] eq {aqua}} {
    image create photo vblogo -file ../Resources/setBfree.icns
    wm iconphoto . -default vblogo
  } else {
    image create photo vblogo -width 16 -height 16
    vblogo put #ffffff -to  1  1  15 15
    vblogo put #000000 -to  1  1  2 15 
    vblogo put #000000 -to  1  1  15 2
    vblogo put #000000 -to  5  1  8 9 
    vblogo put #000000 -to  6  9  7 14 
    vblogo put #000000 -to  11 1  14 9 
    vblogo put #000000 -to  12 9  13 14 

    vblogo put #cc0000 -to  2 2  3 14 
    vblogo put #cc0000 -to  3 2 
    vblogo put #cc0000 -to  3 7 
    vblogo put #cc0000 -to  3 13 
    vblogo put #cc0000 -to  4 3  5 7 
    vblogo put #cc0000 -to  4 8  5 13

    vblogo put #cc0000 -to  8 2  11 4
    vblogo put #cc0000 -to  10 4
    vblogo put #cc0000 -to  9 5 10 8
    vblogo put #cc0000 -to  10 8 11 13
    vblogo put #cc0000 -to  8 11
    vblogo put #cc0000 -to  8 12 10 14
    vblogo redither

    image create photo vblogo32  -width 32 -height 32
    vblogo32 copy vblogo -zoom 2 2
    wm iconphoto . -default vblogo vblogo32
  }
}

bind . <Destroy> {SeqOff}
bind . <Control-c> {exit 0}
bind . <Control-q> {exit 0}

tkwait window  .

exit 0
# vi:set ts=8 sts=2 sw=2: #
