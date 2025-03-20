#!/bin/bash

../configure \
    --enable-c++11 \
    --enable-stdc++-sync \
    --enable-apps \
    --enable-bonding \
    --enable-logging \
    --enable-heavy-logging \
    --cmake-install-prefix=$(pwd)/build \


cd .. && make -j$(nproc) && cd -
