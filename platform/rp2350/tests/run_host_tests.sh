#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"
if [ "$#" -ne 1 ]; then
    echo "Usage: sh platform/rp2350/tests/run_host_tests.sh /path/to/original/PAK0.PAK" >&2
    exit 2
fi
mkdir -p build-host
CC=${CC:-clang}
"$CC" -std=c11 -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
    -Wall -Wextra -Werror -Iplatform/rp2350 platform/rp2350/qpak.c \
    platform/rp2350/qmix.c platform/rp2350/tests/test_qpak.c -o build-host/test_qpak
./build-host/test_qpak assets-local/shareware.qpak "$1"
"$CC" -std=c11 -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
    -Wall -Wextra -Werror -Iplatform/rp2350 platform/rp2350/qmix.c \
    platform/rp2350/tests/test_qmix.c -o build-host/test_qmix
./build-host/test_qmix
python3 platform/rp2350/tests/test_tools.py
