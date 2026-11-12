#!/usr/bin/env bash
if [[ ! -e ncurses-6.6 ]]; then
	curl -L https://mirror.team-cymru.com/gnu//ncurses/ncurses-6.6.tar.gz | tar xz
	cd ncurses-6.6
	patch -p1 <../../patches/ncurses-6.6.patch
	cd ..
fi
cd ncurses-6.6
./configure --prefix="$(pwd)/../../sysroot/usr" --host=i686-unstableos --with-shared --with-termlib --without-dlsym --without-ada --disable-ext-colors --enable-sigwinch --with-terminfo-dirs=/usr/share/terminfo --with-default-terminfo-dir=/usr/share/terminfo --with-install-prefix="$(pwd)/../../sysroot/" --enable-install-prefix 
make -j$(nproc) && make install
cd ..
