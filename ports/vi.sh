#!/usr/bin/env bash

if [[ ! -e ex-050325 ]]; then
	curl -fSL https://sourceforge.net/projects/ex-vi/files/ex-vi/050325/ex-050325.tar.bz2/download | tar xvj 
	cd ex-050325
	patch -p1 <../../patches/vi.patch
	cd ..
fi
cd ex-050325

CC=$(pwd)/../../toolchain/bin/i686-unstableos-gcc
AS=$(pwd)/../../toolchain/bin/i686-unstableos-as
AR=$(pwd)/../../toolchain/bin/i686-unstableos-lr
LD=$(pwd)/../../toolchain/bin/i686-unstableos-ld

# okay... there's some UB in vi and -fstack-protector (and -Og) is the only flag where it works
# i have absolutely no clue why but i'm so fucking done; i don't wanna ever see this sw again
make CFLAGS="-Wno-incompatible-pointer-types -std=c90 -fstack-protector"
# if that wasn't enough, the first compilation fails on ex_vars.h, which succeeds on the second one
make CFLAGS="-Wno-incompatible-pointer-types -std=c90 -fstack-protector" && make install

cd ..
