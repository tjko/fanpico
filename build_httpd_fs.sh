#!/bin/sh
#
# build_httpd_fs.sh
#

SERVER="lwIP/2.2.1 (FanPico)"

EXCLUDE="html~,shtml~,json~,csv~"
SSIFILENAME=src/httpd-fs_ssi.list
FSDIR=src/httpd-fs/
FSDATAFILE=src/fanpico_fsdata.c

fatal() { echo "`basename $0`: $*"; exit 1; }

[ -d "$FSDIR" ] || fatal "cannot find fs directory: $FSDIR"

./contrib/makefsdata.py ${FSDIR} -M -svr "${SERVER}" -ssi "${SSIFILENAME}" -f ${FSDATAFILE} -x "${EXCLUDE}" -v
