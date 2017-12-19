#!/bin/bash

# Generate header files containing audio information for playback in demo.
# Expects SFDIR to contain 3 files name 01.wav, ... 03.wav.
# Each file has 0.666, 1.333,... seconds of silence at their beginnings.

[ -z "$SFDIR" ] && SFDIR=/Users/audiblereality001/Documents/development/sassvz_simple_render_example_ios_xcode8/sassvz_simple_render_example_ios_xcode8/sounds/
[ -z "$INCDIR" ] && INCDIR=/Users/audiblereality001/AndroidStudioProjects/HelloJNISLES/app/src/main/cpp/
for n in `seq 1 3`;
do
    sox "$SFDIR"/0$n.wav -t f32 -c 1 -r 48k /tmp/_$n.f32 pad $(($n * 48000 / 3))s 10t norm -1 #$((($n - 1) * 48000))s 10t norm -1
    sox -t f32 -c 1 -r 48k /tmp/_$n.f32 -t f32 -c 1 -r 48k /tmp/$n.f32 trim 0 =4t
    echo "static char sf$n[] = {" > /tmp/sf$n.h
    hexdump -ve '1/1 "%#x,\n"' /tmp/$n.f32 >> /tmp/sf$n.h
    echo "};" >> /tmp/sf$n.h
    cp -f /tmp/sf$n.h "$INCDIR"
done
exit 0
