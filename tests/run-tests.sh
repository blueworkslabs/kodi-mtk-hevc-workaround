#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Build and run the tests against the patched Kodi tree.
# usage: tests/run-tests.sh <patched-kodi-directory>
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
kodi=${1:?usage: $0 <patched-kodi-directory>}
src="$kodi/xbmc/cores/VideoPlayer/DVDCodecs/Video"
test -f "$src/MediaCodecOutputReorder.cpp" || { echo "$src has no MediaCodecOutputReorder.cpp; is the patch applied?" >&2; exit 1; }

out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
${CXX:-g++} -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$src" "$here/test_reorder.cpp" "$src/MediaCodecOutputReorder.cpp" -o "$out/test_reorder"
"$out/test_reorder" "$here/fixtures"
