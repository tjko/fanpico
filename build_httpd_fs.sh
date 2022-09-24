#!/bin/sh
#
# build_httpd_fs.sh
#

FSDIR=src/httpd-fs/
FSDATAFILE=src/fanpico_fsdata.c

fatal() { echo "`basename $0`: $*"; exit 1; }

[ -d "$FSDIR" ] || fatal "cannot find fs directory: $FSDIR"

makefsdata ${FSDIR} -f:${FSDATAFILE} -x:html~ -x:shtml~
[ $? -eq 0 ] || fatal "makefsdata failed"

find build/ -type f -name 'fs.c.obj' -print -delete


