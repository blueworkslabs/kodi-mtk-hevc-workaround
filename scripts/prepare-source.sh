#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Clone Kodi 21.3-Omega, check that the tag is the expected commit, and apply the workaround.
# usage: scripts/prepare-source.sh <new-directory>
set -euo pipefail

here=$(cd "$(dirname "$0")/.." && pwd)
dir=${1:?usage: $0 <new-directory>}
tag=21.3-Omega
commit=a3a448d26b8d560a65655dab2cd122994dc4e146

if [ -e "$dir" ]; then
  echo "$dir already exists" >&2
  exit 1
fi

git clone --quiet --depth 1 --branch "$tag" https://github.com/xbmc/xbmc.git "$dir"
actual=$(git -C "$dir" rev-parse HEAD)
if [ "$actual" != "$commit" ]; then
  echo "tag $tag points at $actual, expected $commit; not applying" >&2
  exit 1
fi

for patch in "$here"/patches/*.patch; do
  git -C "$dir" apply --check "$patch"
  git -C "$dir" apply "$patch"
  echo "applied $(basename "$patch")"
done

echo "Kodi $tag ($commit) with the workaround is in $dir"
git -C "$dir" status --short
