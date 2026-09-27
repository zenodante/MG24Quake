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
    platform/rp2350/qmix.c platform/rp2350/qfiles.c platform/rp2350/tests/test_qpak.c -o build-host/test_qpak
./build-host/test_qpak assets-local/shareware.qpak "$1"
"$CC" -std=c11 -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
    -Wall -Wextra -Werror -Iplatform/rp2350 platform/rp2350/qmix.c \
    platform/rp2350/tests/test_qmix.c -o build-host/test_qmix
./build-host/test_qmix
"$CC" -std=c11 -O1 -g -fsanitize=${QBSP_SANITIZERS:-undefined} -fno-sanitize-recover=all \
    -Wall -Wextra -Werror -Iplatform/rp2350 platform/rp2350/qpak.c \
    platform/rp2350/qbsp.c platform/rp2350/tests/test_qbsp.c -o build-host/test_qbsp
./build-host/test_qbsp assets-local/shareware.qpak "$1"
mkdir -p build-host/render
"$CC" -std=c11 -O2 -g -fsanitize=undefined -fno-sanitize-recover=all \
    -Wall -Wextra -Werror -Iplatform/rp2350 platform/rp2350/qpak.c \
    platform/rp2350/qbsp.c platform/rp2350/qrender.c platform/rp2350/tests/test_qrender.c -o build-host/test_qrender
./build-host/test_qrender assets-local/shareware.qpak build-host/render
"$CC" -std=c11 -O2 -g -fsanitize=undefined -fno-sanitize-recover=all \
    -Wall -Wextra -Werror -Iplatform/rp2350 platform/rp2350/qpak.c \
    platform/rp2350/qbsp.c platform/rp2350/qrender.c platform/rp2350/qcollision.c \
    platform/rp2350/tests/test_qcollision.c -o build-host/test_qcollision
./build-host/test_qcollision assets-local/shareware.qpak
python3 platform/rp2350/tests/test_tools.py
