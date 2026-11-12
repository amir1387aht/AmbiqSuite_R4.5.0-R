#!/bin/sh
# Host-only; never accesses real NAND.
set -eu
base=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
yaffs="$base/src/drivers/nand/yaffs2"
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT HUP INT TERM
flags='-std=gnu11 -g -O1 -Wall -Wextra -Wno-unused-parameter -ffunction-sections -fdata-sections -fsanitize=address,undefined -fno-omit-frame-pointer'
defs='-DCONFIG_YAFFS_DIRECT -DCONFIG_YAFFS_YAFFS2 -DCONFIG_YAFFS_DEFINES_TYPES -DCONFIG_YAFFS_PROVIDE_DEFS -DCONFIG_YAFFSFS_PROVIDE_VALUES -DCONFIG_YAFFS_SMALL_RAM -DCONFIG_YAFFS_SHORT_NAMES_IN_RAM -DCONFIG_YAFFS_USE_32_BIT_TIME_T -DCONFIG_YAFFS_NO_TRACE'
for src in "$yaffs"/*.c; do
    cc $flags $defs -D'Y_LOFF_T=long long' -I"$yaffs" -c "$src" -o "$out/${src##*/}.o"
done
cc $flags $defs -D'Y_LOFF_T=long long' -I"$yaffs" "$base/tests/yaffs_nand_sim_test.c" "$out"/*.o -Wl,--gc-sections -o "$out/test"
"$out/test" "$@"
