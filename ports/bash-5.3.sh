#!/usr/bin/env bash
if [[ ! -e bash-5.3 ]]; then
	curl -L https://mirrors.ocf.berkeley.edu/gnu/bash/bash-5.3.tar.gz | tar xz
	cd bash-5.3
	patch -p1 <../../patches/bash-5.3.patch
	cd ..
fi
cd bash-5.3
./configure CFLAGS="-DHEREDOC_PIPEMAX=2048" --prefix="$(pwd)/../../sysroot/usr" --host=i686-unstableos-elf --enable-multibyte --enable-readline --disable-nls --enable-threads=posix --disable-rpath --enable-year2038 --with-installed-readline
make -j$(nproc) && make install
cd ..
