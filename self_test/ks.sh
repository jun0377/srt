#!/bin/bash

kill -9 $(pidof srt-live-transmit)
kill -9 $(pidof ffmpeg)
