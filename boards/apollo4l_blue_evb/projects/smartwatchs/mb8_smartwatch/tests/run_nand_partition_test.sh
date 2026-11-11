#!/bin/sh
# Host-only; all transport callbacks are mocks. No physical NAND access.
set -eu
base=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
driver="$base/src/drivers/nand"
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT HUP INT TERM
for debug in 0 1; do
    cc -std=gnu11 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined \
      -fno-omit-frame-pointer -DWATCH_NAND_DEBUG_REPRO="$debug" -I"$driver" \
      "$base/tests/nand_partition_test.c" "$driver/mb8_nand_partitions.c" \
      "$driver/ds35m1gb.c" -o "$out/partition_test"
    "$out/partition_test"
done
