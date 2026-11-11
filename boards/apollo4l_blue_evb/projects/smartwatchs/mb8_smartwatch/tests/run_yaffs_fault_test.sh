#!/bin/sh
# Host test only; does not access a device.
set -eu
base=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
yaffs="$base/src/drivers/nand/yaffs2"
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT HUP INT TERM
flags='-std=gnu11 -g -O1 -Wall -Wextra -Wno-unused-parameter -ffunction-sections -fdata-sections -fsanitize=address,undefined -fno-omit-frame-pointer'
defs='-DCONFIG_YAFFS_DIRECT -DCONFIG_YAFFS_YAFFS2 -DCONFIG_YAFFS_DEFINES_TYPES -DCONFIG_YAFFS_PROVIDE_DEFS -DCONFIG_YAFFSFS_PROVIDE_VALUES -DCONFIG_YAFFS_SMALL_RAM -DCONFIG_YAFFS_SHORT_NAMES_IN_RAM -DCONFIG_YAFFS_USE_32_BIT_TIME_T -DCONFIG_YAFFS_NO_TRACE'
# The test includes yaffs_guts.c so its static GC function is tested directly.
for src in "$yaffs"/*.c; do
    [ "${src##*/}" = yaffs_guts.c ] && continue
    cc $flags $defs -D'Y_LOFF_T=long long' -I"$yaffs" -c "$src" -o "$out/${src##*/}.o"
done
cc $flags $defs -D'Y_LOFF_T=long long' -I"$yaffs" "$base/tests/yaffs_fault_test.c" "$out"/*.o -Wl,--gc-sections,--wrap=yaffs_checkpoint_save,--wrap=yaffs_deinitialise,--wrap=yaffs_guts_cleanup -o "$out/yaffs_fault_test"
"$out/yaffs_fault_test" "$@"
