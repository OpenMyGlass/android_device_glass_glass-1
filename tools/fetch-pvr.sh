#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Fetch the SGX DDK kernel sources pinned in tools/pvr.env (only PVR_PATH) into DEST.
# Usage: tools/fetch-pvr.sh $AOSP/vendor/glass/pvr-km
# BoardConfig.mk builds pvrsrvkm_sgx540_120.ko from DEST/PVR_PATH. Needs git 2.25 or newer.
set -eu

[ $# -eq 1 ] || { echo "usage: $0 DEST" >&2; exit 2; }
dest=$1
# shellcheck disable=SC1091
. "$(dirname "$0")/pvr.env"

# checkout --force and clean -fdx below delete local files: only touch DEST if it is
# missing, empty, or already a checkout of PVR_REPO.
if [ -d "$dest/.git" ]; then
    if [ "$(git -C "$dest" remote get-url origin 2>/dev/null || true)" != "$PVR_REPO" ]; then
        echo "error: $dest is a git checkout of something else; pick another DEST" >&2
        exit 1
    fi
elif [ -e "$dest" ] && { [ ! -d "$dest" ] || [ -n "$(ls -A "$dest" 2>/dev/null || echo unreadable)" ]; }; then
    echo "error: $dest exists and is not an empty directory; pick another DEST" >&2
    exit 1
fi
if [ ! -d "$dest/.git" ]; then
    mkdir -p "$dest"
    git init -q "$dest"
    git -C "$dest" remote add origin "$PVR_REPO"
fi
git -C "$dest" remote set-url origin "$PVR_REPO"
if [ "$(git -C "$dest" rev-parse -q --verify HEAD 2>/dev/null || true)" != "$PVR_COMMIT" ]; then
    git -C "$dest" fetch -q --depth 1 --filter=blob:none origin "$PVR_COMMIT"
    git -C "$dest" sparse-checkout set "$PVR_PATH"
fi
# The build copies the DDK from here, so discard any local edits.
git -C "$dest" checkout -q --force "$PVR_COMMIT"
git -C "$dest" clean -qfdx

h=$dest/$PVR_PATH/include4/pvrversion.h
if [ ! -f "$h" ]; then
    echo "error: $h not found at $PVR_COMMIT" >&2
    exit 1
fi
branch=$(awk '$2 == "PVRVERSION_BRANCHNAME" {gsub(/"/, "", $3); print $3}' "$h")
build=$(awk '$2 == "PVRVERSION_BUILD" {print $3}' "$h")
if [ "$branch" != "$PVR_EXPECTED_BRANCH" ] || [ "$build" != "$PVR_EXPECTED_BUILD" ]; then
    echo "error: DDK is $branch@$build, expected $PVR_EXPECTED_BRANCH@$PVR_EXPECTED_BUILD" >&2
    exit 1
fi
echo "$dest at $PVR_COMMIT (DDK $branch@$build)"
