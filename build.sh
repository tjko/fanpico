#!/bin/sh
#
# build.sh
#

BUILD_DIR=./build

error() { echo "`basename $0`: $*"; }
fatal() { echo "`basename $0`: $*"; exit 1; }


# asm hack (CMake cant seem to handle .incbin in .s file)
for s_file in src/*.s; do
	d_file=$(echo $s_file | sed -e 's/\.s/.json/')
	if [ -s "$d_file" ]; then
		if [ "$d_file" -nt "$s_file" ]; then
			echo "$d_file updated: touch $s_file"
			touch "$s_file"	
		fi
	fi
done


[ -d "${BUILD_DIR}" ] || mkdir -p "${BUILD_DIR}" || fatal "failed to create: $BUILD_DIR"

cd "${BUILD_DIR}" || fatal "cannot access: $BUILD_DIR"

cmake .. || fatal "cmake failed"

make -j
if [ $? -eq 0 ]; then
	echo "Build successful."
	picotool info -a fanpico.uf2
else
	echo "Build failed."
	exit 1
fi

# eof :-)
