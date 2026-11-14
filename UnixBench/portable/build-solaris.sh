#!/bin/sh
# Build the 32-bit SPARC binaries for Solaris 8 (SunOS 5.8) with Debian's GCC
# cross-compiler, in Docker:
#
#   ubench-sparc-solaris      dynamic, -mcpu=v7 (any 32-bit SPARC)
#   ubench-sparc-solaris-v8   dynamic, kernels -mcpu=supersparc
#
# The compiler is Debian bookworm's sparc64-linux-gnu-gcc 12.2 with -m32, as
# in build-sparc.sh.  It only compiles: Makefile.sparc points it at Solaris's
# headers and links against Solaris's libc.so.1 and libm.so.1.
#
# Solaris cannot be downloaded, so the headers and libraries come from an
# installed system.  On the Solaris machine, as root:
#
#   cd / && tar cf /tmp/sol8-sysroot.tar usr/include usr/lib/libc.so.1 \
#       usr/lib/libm.so.1 usr/lib/libdl.so.1 usr/lib/values-Xa.o \
#       usr/lib/libc.so usr/lib/libm.so usr/lib/libdl.so
#
# (10 MB; usr/include needs the SUNWhea package.)
#
# Usage: ./build-solaris.sh /path/to/sol8-sysroot.tar
#
# from UnixBench/portable.
set -eu

[ $# -eq 1 ] && [ -f "$1" ] ||
    { echo "usage: $0 sol8-sysroot.tar" >&2; exit 1; }
TAR=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")

cd "$(dirname "$0")/.."

docker run --rm --platform linux/amd64 -v "$TAR":/sysroot.tar:ro \
    -v "$PWD":/src debian:bookworm-slim sh -euc '
        apt-get -qq update >/dev/null
        apt-get -qq install -y make gcc-multilib-sparc64-linux-gnu \
            >/dev/null 2>&1
        mkdir /opt/solaris
        tar xf /sysroot.tar -C /opt/solaris
        cd /src/portable
        make -B -f Makefile.sparc SOLSYSROOT=/opt/solaris solaris'

ls -l portable/ubench-sparc-solaris*
