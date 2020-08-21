#!/bin/sh

cat <<__EOF__ > PWD
UID=${TDSPWDUID}
PWD=${TDSPWDPWD}
SRV=${TDSPWDSRV}
DB=${TDSPWDDB}
__EOF__

pwd
cat PWD
