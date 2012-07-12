#! /bin/sh
# 19-sep-2004/FK
# Shell-script to generate the configuration file fartybass.cfg.
# The reason for using a shell-script is of course that it becomes very
# tedious to type and retype those harmonic amplitudes.
#
OUT=fartybass.cfg
cat <<EOF > $OUT
# 18-sep-2004/FK
# This is an approximation of the 12 complex tonewheels found in some organ
# models. I have not been able to obtain the actual harmonics or their
# strength, other than a reference in 
# http://www.bentonelectronics.com/generator.html
# that states '... a fundamental tone that is enriched with the odd-number
# harmonics.' Another description I have encountered is that the sound is
# 'farty' which seems to imply a rather strong presence of harmonics.
#
# You can use this file, or any other configuration file by placing the
# command
#   config.read=fartybass.cfg
# in a configuation file read by setBfree, or on setBfree's commandline.
#
EOF
for w in 1 2 3 4 5 6 7 8 9 10 11 12; do
  echo "osc.harmonic.w${w}.f1=1.0"
  echo "osc.harmonic.w${w}.f3=0.3333333"
  echo "osc.harmonic.w${w}.f5=0.2"
  echo "osc.harmonic.w${w}.f7=0.142857142857"
  echo "osc.harmonic.w${w}.f9=0.111111111111"
  echo "osc.harmonic.w${w}.f11=0.090909090909"
done >> $OUT

