#!/bin/bash

function_srt()
{
    # srt-live-transmit工作于listener模式
    $(pwd)/../srt-live-transmit \
        udp://:1234 \
        srt://:4201 \
        -verbose \
        -fullstats \
        -loglevel info \
        -stats-report-frequency 1 \
        -bandwidth-report 1 \
        -timeout 0 \
        -autoreconnect no \
        -chunk 1456 \
        -buffering 10 \
        >/dev/null &

            
}

function_ffmpeg()
{
    # ffmpeg向srt listener推流
    ffmpeg \
    -f lavfi \
    -re \
    -i smptebars=duration=300:size=1280x720:rate=30 \
    -f lavfi \
    -re \
    -i sine=frequency=1000:duration=60:sample_rate=44100 \
    -pix_fmt yuv420p \
    -c:v libx264 \
    -b:v 1000k \
    -g 30 \
    -keyint_min 120 \
    -profile:v baseline \
    -preset veryfast \
    -f mpegts "udp://127.0.0.1:1234?pkt_size=1316" \
    &
}

function_srt 
function_ffmpeg


