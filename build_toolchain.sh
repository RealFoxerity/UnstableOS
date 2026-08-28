#!/bin/bash
set -e

mkdir -p sysroot/usr/lib sysroot/usr/include/
cp -rv libc/src/include/* sysroot/usr/include/

mkdir -p toolchain

if [[ ! -e binutils-2.47 ]]; then
	curl -L https://ftpmirror.gnu.org/binutils/binutils-2.47.tar.xz | tar --xz -x
	cd binutils-2.47
	patch -p1 <../patches/binutils-2_47.patch
	cd ..
fi

if [[ ! -e gcc-16.2.0 ]]; then
	curl -L https://ftpmirror.gnu.org/gnu/gcc/gcc-16.2.0/gcc-16.2.0.tar.gz | tar xz
	cd gcc-16.2.0
	patch -p1 <../patches/gcc-16.2.0.patch
	cd ..
fi

cd binutils-2.47
if [[ ! -e autoconf-2.69 ]]; then
	curl -L https://ftpmirror.gnu.org/gnu/autoconf/autoconf-2.69.tar.gz | tar xz
	cd autoconf-2.69
	./configure --prefix="$(pwd)/../install"
	# this old autoconf has borked doc generation, so have to do it like this
	# the make fails on some random point, but the main executable does get built
	make || :
	make install || :
	cd ..
fi
if [[ ! -e automake-1.15.1 ]]; then
	curl -L https://ftpmirror.gnu.org/gnu/automake/automake-1.15.1.tar.gz | tar xz
	cd automake-1.15.1
	./configure --prefix="$(pwd)/../install"
	make && make install
	cd ..
fi


cd ld
env PATH="$PWD/../install/bin:$PATH" automake
cd ..
./configure --prefix="$(pwd)/../toolchain" --with-sysroot="$(pwd)/../sysroot" --enable-year2038 --disable-werror --target=i686-unstableos --enable-shared --enable-pie
env PATH="$PWD/install/bin:$PATH" make -j$(nproc) && make install

cd ../gcc-16.2.0

cd libstdc++-v3
env PATH="$PWD/../../binutils-2.47/install/bin:$PATH" autoconf
cd ..

mkdir -p build
cd build

../configure --target=i686-unstableos --prefix="$(pwd)/../../toolchain" --with-sysroot="$(pwd)/../../sysroot" --enable-year2038 --enable-languages=c,c++ --enable-tls --enable-initfini-array --disable-werror --enable-shared --enable-default-pie --enable-threads=posix
make -j$(nproc) all-gcc all-target-libgcc
make install-gcc install-target-libgcc

