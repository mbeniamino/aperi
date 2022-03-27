#!/bin/sh
LINKER_FLAGS=$(pkg-config --libs gtk4)
COMPILER_FLAGS=$(pkg-config --cflags gtk4)
gcc bopen.c -O2 ${LINKER_FLAGS} ${COMPILER_FLAGS} $@ -o bopen
