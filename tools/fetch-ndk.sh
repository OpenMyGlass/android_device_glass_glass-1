#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Download the NDK pinned in tools/ndk.env, check its SHA-256 and SHA-1, and unpack it.
#   tools/fetch-ndk.sh <dir>
# The zip is kept as <dir>/<zip name> (so CI can cache it) and reused if its hashes match.
# Prints the NDK root, <dir>/android-ndk-<version>. Linux x86_64 only.
set -eu

if [ $# -ne 1 ]; then
    echo "usage: $0 <dir>" >&2
    exit 2
fi
dir=$1
. "$(dirname "$0")/ndk.env"
zip=$dir/$(basename "$NDK_URL")
mkdir -p "$dir"

check() {
    echo "$NDK_SHA256  $zip" | sha256sum -c --status - &&
        echo "$NDK_SHA1  $zip" | sha1sum -c --status -
}

if [ ! -f "$zip" ] || ! check; then
    curl -fsSL --retry 3 -o "$zip" "$NDK_URL"
    if ! check; then
        echo "error: $zip does not match the hashes in tools/ndk.env" >&2
        exit 1
    fi
fi
rm -rf "$dir/android-ndk-$NDK_VERSION"
unzip -q "$zip" "android-ndk-$NDK_VERSION/toolchains/llvm/*" -d "$dir"
echo "$dir/android-ndk-$NDK_VERSION"
