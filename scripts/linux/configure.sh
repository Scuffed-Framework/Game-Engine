#!/usr/bin/env bash

set -e

# Parse args
BUILD_TYPE="Debug"

while [[ "$#" -gt 0 ]]; do
    case $1 in
        -d) BUILD_TYPE="Debug" ;;
        -r) BUILD_TYPE="Release" ;;
    esac
    shift
done

echo "==> Build type: $BUILD_TYPE"

echo "==> Running Conan..."
conan install . -s build_type=$BUILD_TYPE --build=missing
conan install ./scripts/conan-ext/openapv/all/conanfile.py
conan create ./scripts/conan-ext/openapv/all/ \
    --version=1.1.3.0 \
    -s build_type=$BUILD_TYPE \
    -s arch=x86_64 \
    -s compiler=gcc \
    -s compiler.version=16 \
    -s compiler.libcxx=libstdc++11

echo "==> Running CMake configure..."
cmake -B "000-Build-$BUILD_TYPE-x64" -DCMAKE_BUILD_TYPE=$BUILD_TYPE -S .

echo "==> Configure complete"