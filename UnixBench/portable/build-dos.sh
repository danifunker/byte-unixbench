#!/bin/sh
# Build ubench.exe for MS-DOS with a DJGPP cross-compiler, in Docker.
#
# The first run downloads into the Docker volume "djgpp-cross":
#   - DJGPP GCC 12.2 for Linux, from https://github.com/andrewwutw/build-djgpp
#   - CWSDPMI r7 (csdpmi7b.zip), from the DJGPP archive at delorie.com
# CWSDSTUB.EXE is bound into each .exe, so it runs from plain DOS with no
# CWSDPMI.EXE alongside.
#
# Usage: ./build-dos.sh            ubench.exe (generic, any 386 or later)
#        ./build-dos.sh variants   also ub486.exe ub586.exe ub686.exe
#
# from UnixBench/portable.
set -eu

cd "$(dirname "$0")/.."

TARGETS="ubench.exe"
[ "${1:-}" = variants ] && TARGETS="ubench.exe variants"

docker run --rm --platform linux/amd64 -v djgpp-cross:/opt/djgpp -v "$PWD":/src \
    debian:bookworm-slim sh -euc '
        apt-get -qq update >/dev/null
        apt-get -qq install -y make curl bzip2 unzip ca-certificates >/dev/null
        if [ ! -x /opt/djgpp/djgpp/bin/i586-pc-msdosdjgpp-gcc ]; then
            echo "Downloading DJGPP..."
            curl -fsSL https://github.com/andrewwutw/build-djgpp/releases/download/v3.4/djgpp-linux64-gcc1220.tar.bz2 \
                | tar xjf - -C /opt/djgpp
        fi
        if [ ! -f /opt/djgpp/CWSDSTUB.EXE ]; then
            echo "Downloading CWSDPMI..."
            curl -fsSL -o /tmp/csdpmi.zip http://www.delorie.com/pub/djgpp/current/v2misc/csdpmi7b.zip
            unzip -qjo /tmp/csdpmi.zip bin/CWSDSTUB.EXE bin/CWSDPMI.EXE bin/cwsdpmi.doc -d /opt/djgpp
        fi
        export PATH=/opt/djgpp/djgpp/bin:/opt/djgpp/djgpp/i586-pc-msdosdjgpp/bin:$PATH
        cd /src/portable
        make -f Makefile.dos clean
        make -f Makefile.dos CC=i586-pc-msdosdjgpp-gcc EXE2COFF=exe2coff \
            CWSDSTUB=/opt/djgpp/CWSDSTUB.EXE '"$TARGETS"

ls -l portable/*.exe
