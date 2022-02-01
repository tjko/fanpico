#!/bin/sh
#
# build.sh
#

BUILD_DIR=./build

error() { echo "`basename $0`: $*"; }
fatal() { echo "`basename $0`: $*"; exit 1; }


[ -d "${BUILD_DIR}" ] || mkdir -p "${BUILD_DIR}" || fatal "failed to create: $BUILD_DIR"

cd "${BUILD_DIR}" || fatal "cannot access: $BUILD_DIR"

[ -s Makefile ] || cmake .. || fatal "cmake failed"

make -j4 
if [ $? -eq 0 ]; then
	echo "Build successful."
	picotool info -a fanpico.elf
else
	echo "Build failed."
	exit 1
fi






