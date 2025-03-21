#!/bin/bash

../configure \
    --enable-c++11 \
    --enable-stdc++-sync \
    --enable-apps \
    --enable-bonding \
    --cmake-install-prefix=$(pwd)/build \
    --enable-logging \

cd .. && make -j$(nproc) && cd -
