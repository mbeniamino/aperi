#!/bin/sh
LINKER_FLAGS=$(pkg-config --libs libmagic gtk4)
COMPILER_FLAGS=$(pkg-config --cflags libmagic gtk4)
gcc -g bopen.c ${LINKER_FLAGS} ${COMPILER_FLAGS} -o bopen
