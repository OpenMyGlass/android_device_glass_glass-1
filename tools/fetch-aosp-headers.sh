#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Fetch the AOSP projects pinned in tools/aosp-headers.lock for one tag, shallow, and check
# that each one is the pinned commit.
#   tools/fetch-aosp-headers.sh <tag> <out-dir> [project ...]
# Without projects, every project of <tag> in the lock file is fetched. Each project lands
# in <out-dir>/<project without "platform/">, e.g. <out-dir>/hardware/libhardware.
set -eu

if [ $# -lt 2 ]; then
    echo "usage: $0 <tag> <out-dir> [project ...]" >&2
    exit 2
fi
tag=$1
out=$2
shift 2
lock=$(dirname "$0")/aosp-headers.lock
base=${AOSP_GIT_BASE:-https://android.googlesource.com}

if [ $# -eq 0 ]; then
    set -- $(awk -v t="$tag" '$1 == t { print $2 }' "$lock")
fi
if [ $# -eq 0 ]; then
    echo "error: no project pinned for $tag in $lock" >&2
    exit 1
fi

for project in "$@"; do
    want=$(awk -v t="$tag" -v p="$project" '$1 == t && $2 == p { print $3 }' "$lock")
    if [ -z "$want" ]; then
        echo "error: $project at $tag is not pinned in $lock" >&2
        exit 1
    fi
    dir=$out/${project#platform/}
    rm -rf "$dir"
    mkdir -p "$dir"
    git -C "$dir" init -q
    git -C "$dir" fetch -q --depth 1 "$base/$project" "refs/tags/$tag"
    got=$(git -C "$dir" rev-parse 'FETCH_HEAD^{commit}')
    if [ "$got" != "$want" ]; then
        echo "error: $project $tag is $got, pinned $want" >&2
        exit 1
    fi
    git -C "$dir" -c advice.detachedHead=false checkout -q "$got"
    echo "$project $tag $got"
done
