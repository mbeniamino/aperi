#!/bin/sh
LINKER_FLAGS=$(pkg-config --libs gtk4)
COMPILER_FLAGS=$(pkg-config --cflags gtk4)
gcc -g bopen.c ${LINKER_FLAGS} ${COMPILER_FLAGS} -o bopen
