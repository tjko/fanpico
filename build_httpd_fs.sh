#!/bin/sh
#
# build_httpd_fs.sh
#

FSDIR=src/httpd-fs/
FSDATAFILE=src/fanpico_fsdata.c

fatal() { echo "`basename $0`: $*"; exit 1; }

[ -d "$FSDIR" ] || fatal "cannot find fs directory: $FSDIR"

#find src/httpd-fs/ -type f -name '*~' -print -delete

makefsdata ${FSDIR} -ssi:src/httpd-fs_ssi.list -f:${FSDATAFILE} -x:html~,shtml~,json~,~
[ $? -eq 0 ] || fatal "makefsdata failed"

find build/ -type f -name 'fs.c.obj' -print -delete

