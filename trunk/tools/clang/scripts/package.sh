#!/bin/bash
# Copyright (c) 2011 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script will check out llvm and clang, and then package the results up
# to a tgz file.

THIS_DIR="$(dirname "${0}")"
LLVM_BUILD_DIR="${THIS_DIR}/../../../third_party/llvm-build"
LLVM_BIN_DIR="${LLVM_BUILD_DIR}/Release+Asserts/bin"
LLVM_LIB_DIR="${LLVM_BUILD_DIR}/Release+Asserts/lib"

set -ex

# Do a clobber build.
rm -rf "${LLVM_BUILD_DIR}"
"${THIS_DIR}"/update.sh

R=$("${LLVM_BIN_DIR}/clang" --version | \
     sed -ne 's/clang version .*(trunk \([0-9]*\))/\1/p')

PDIR=clang-$R
rm -rf $PDIR
mkdir $PDIR
mkdir $PDIR/bin
mkdir $PDIR/lib

# Copy clang into pdir, symlink clang++ to it.
cp "${LLVM_BIN_DIR}/clang" $PDIR/bin/
(cd $PDIR/bin && ln -sf clang clang++ && cd -)

# Copy plugins. Some of the dylibs are pretty big, so copy only the ones we
# care about.
if [ "$(uname -s)" = "Darwin" ]; then
  cp "${LLVM_LIB_DIR}/libFindBadConstructs.dylib" $PDIR/lib
else
  cp "${LLVM_LIB_DIR}/libFindBadConstructs.so" $PDIR/lib
fi

# Copy built-in headers (lib/clang/3.0/include).
cp -R "${LLVM_LIB_DIR}/clang" $PDIR/lib

tar zcf $PDIR.tgz -C $PDIR bin lib

if [ "$(uname -s)" = "Darwin" ]; then
  PLATFORM=Mac
else
  PLATFORM=Linux_x64
fi

echo To upload, run:
echo gsutil cp -a public-read $PDIR.tgz \
     gs://chromium-browser-clang/$PLATFORM/$PDIR.tgz
