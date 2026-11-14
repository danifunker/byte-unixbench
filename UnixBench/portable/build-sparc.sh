#!/bin/sh
# Build the 32-bit SPARC binaries with Debian's GCC cross-compiler, in Docker:
#
#   ubench-sparc-netbsd      NetBSD/sparc, static, -mcpu=v7 (any 32-bit SPARC)
#   ubench-sparc-netbsd-v8   NetBSD/sparc, static, kernels -mcpu=supersparc
#   ubench-sparc-linux       Linux/sparc32, static, for testing under qemu
#
# The compiler is Debian bookworm's sparc64-linux-gnu-gcc 12.2 with -m32.  For
# NetBSD it only compiles: Makefile.sparc points it at NetBSD's headers and
# links against NetBSD's own crt files, libc, libm and libgcc.
#
# The first run downloads into the Docker volume "sparc-netbsd-cross":
#   - the NetBSD 11.0 sparc sets base.tgz and comp.tgz (190 MB), from
#     https://cdn.netbsd.org/pub/NetBSD/NetBSD-11.0/sparc/binary/sets/
#     and keeps their usr/include, usr/lib and lib (about 200 MB)
# The sets are checked against the SHA512 file next to them.
#
# Usage: ./build-sparc.sh          all three
#        ./build-sparc.sh netbsd   only the NetBSD binaries
#        ./build-sparc.sh linux    only the Linux binary
#
# from UnixBench/portable.
set -eu

cd "$(dirname "$0")/.."

case "${1:-all}" in
    all)    TARGETS="all ubench-sparc-linux" ;;
    netbsd) TARGETS="all" ;;
    linux)  TARGETS="ubench-sparc-linux" ;;
    *)      echo "usage: $0 [netbsd|linux]" >&2; exit 1 ;;
esac

docker run --rm --platform linux/amd64 -v sparc-netbsd-cross:/opt/sparc \
    -v "$PWD":/src debian:bookworm-slim sh -euc '
        apt-get -qq update >/dev/null
        apt-get -qq install -y make curl ca-certificates \
            gcc-multilib-sparc64-linux-gnu >/dev/null 2>&1
        SYSROOT=/opt/sparc/netbsd-11.0
        if [ ! -f $SYSROOT/.complete ]; then
            echo "Downloading the NetBSD 11.0 sparc sets..."
            SETS=https://cdn.netbsd.org/pub/NetBSD/NetBSD-11.0/sparc/binary/sets
            mkdir -p $SYSROOT /tmp/sets
            cd /tmp/sets
            curl -fsSL -O $SETS/SHA512 -O $SETS/base.tgz -O $SETS/comp.tgz
            for set in base comp; do
                sum=$(sha512sum $set.tgz | cut -d" " -f1)
                grep -q "^SHA512 ($set.tgz) = $sum\$" SHA512 ||
                    { echo "$set.tgz: checksum mismatch" >&2; exit 1; }
            done
            tar xzf base.tgz -C $SYSROOT ./usr/include ./usr/lib ./lib
            tar xzf comp.tgz -C $SYSROOT ./usr/include ./usr/lib
            touch $SYSROOT/.complete
        fi
        cd /src/portable
        make -f Makefile.sparc clean
        make -f Makefile.sparc SYSROOT=$SYSROOT '"$TARGETS"

ls -l portable/ubench-sparc-*
