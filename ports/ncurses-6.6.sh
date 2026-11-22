#!/usr/bin/env bash
if [[ ! -e ncurses-6.6 ]]; then
	curl -L https://mirror.team-cymru.com/gnu//ncurses/ncurses-6.6.tar.gz | tar xz
	cd ncurses-6.6
	patch -p1 <../../patches/ncurses-6.6.patch
	cd ..
fi
cd ncurses-6.6
./configure --prefix="/usr" --host=i686-unstableos --with-shared --with-termlib --without-dlsym --without-ada --without-cxx-binding --disable-ext-colors --enable-sigwinch --with-terminfo-dirs=/usr/share/terminfo --with-default-terminfo-dir=/usr/share/terminfo --with-install-prefix="$(pwd)/../../sysroot/" --without-tests 
make -j$(nproc) && make install
cd ..
