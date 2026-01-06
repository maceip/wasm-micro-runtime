#!/bin/bash

# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

CURR_DIR=$PWD
WAMR_DIR=${PWD}/../..
OUT_DIR=${PWD}/build

rm -rf ${OUT_DIR}
mkdir ${OUT_DIR}
cd ${OUT_DIR}

cmake ${CURR_DIR}
make

echo "========================================"
echo "Build completed!"
echo "Executable: ${OUT_DIR}/cpp-mcp-server"
echo "========================================"
