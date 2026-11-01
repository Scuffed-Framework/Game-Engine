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

function CreateConanPackage(){
  local pkg_path="${1%/}"

  echo "==> Creating Conan package: $pkg_path"
  conan create "./scripts/conan-ext/$pkg_path/" \
      --version="$2" \
      --build=missing \
      -s:h build_type=$BUILD_TYPE \
      -s:h arch=x86_64 \
      -s:h compiler=gcc \
      -s:h compiler.version=16 \
      -s:h compiler.libcxx=libstdc++11 \
      -s:b build_type=Release
}

echo "==> Build type: $BUILD_TYPE"


CreateConanPackage "openapv/all/" "1.1.3.0"
CreateConanPackage "SDL/all/" "3.4.18"

echo "==> Running Conan root install..."
conan install . -s build_type=$BUILD_TYPE --build=missing


echo "==> Running CMake configure..."
cmake -B "000-Build-$BUILD_TYPE-x64" -DCMAKE_BUILD_TYPE=$BUILD_TYPE -S .

echo "==> Configure complete"