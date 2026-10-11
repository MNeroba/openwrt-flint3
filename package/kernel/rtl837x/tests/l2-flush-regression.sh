#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Compile the real flush helper against a scripted register transport.
set -eu

test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
test_tmp=$(mktemp -d)
trap 'rm -rf "$test_tmp"' EXIT HUP INT TERM
mkdir -p "$test_tmp/linux"
for header in device iopoll bits; do
	: > "$test_tmp/linux/$header.h"
done
# glibc's errno.h includes linux/errno.h: an empty stub hides its constants.
# Use the system header on Linux; macOS needs only the host errno.h above.
cat > "$test_tmp/linux/errno.h" <<'EOF'
#ifdef __linux__
#include_next <linux/errno.h>
#endif
EOF

"${CC:-cc}" -std=gnu11 -O2 -Wall -Wextra -Werror -I "$test_tmp" \
	"$test_dir/l2-flush-regression.c" -o "$test_tmp/l2-flush-regression"
"$test_tmp/l2-flush-regression"
