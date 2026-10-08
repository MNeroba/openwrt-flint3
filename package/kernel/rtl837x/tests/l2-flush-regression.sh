#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Compile the real flush helper against a scripted register transport.
set -eu

test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
test_tmp=$(mktemp -d)
trap 'rm -rf "$test_tmp"' EXIT HUP INT TERM
mkdir -p "$test_tmp/linux"
for header in device errno iopoll bits; do
	: > "$test_tmp/linux/$header.h"
done

"${CC:-cc}" -std=gnu11 -O2 -Wall -Wextra -Werror -I "$test_tmp" \
	"$test_dir/l2-flush-regression.c" -o "$test_tmp/l2-flush-regression"
"$test_tmp/l2-flush-regression"
