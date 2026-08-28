#!/bin/sh

export CC=$(realpath "$(dirname "$0")/toolchain/bin/i686-unstableos-gcc")
export AS=$(realpath "$(dirname "$0")/toolchain/bin/i686-unstableos-as")
export AR=$(realpath "$(dirname "$0")/toolchain/bin/i686-unstableos-ar")
export LD=$(realpath "$(dirname "$0")/toolchain/bin/i686-unstableos-ld")
