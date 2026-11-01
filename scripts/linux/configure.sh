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
  # Strip any trailing slashes from the incoming argument $1
  local pkg_path="${1%/}"

  echo "==> Creating Conan package: $pkg_path"
  conan install "./scripts/conan-ext/$pkg_path/conanfile.py"
  conan create "./scripts/conan-ext/$pkg_path/" \
      --version="$2" \
      -s build_type=$BUILD_TYPE \
      -s arch=x86_64 \
      -s compiler=gcc \
      -s compiler.version=16 \
      -s compiler.libcxx=libstdc++11
}

echo "==> Build type: $BUILD_TYPE"


CreateConanPackage "openapv/all/" "1.1.3.0"
CreateConanPackage "SDL/all/" "3.4.18"

echo "==> Running Conan root install..."
conan install . -s build_type=$BUILD_TYPE --build=missing


echo "==> Running CMake configure..."
cmake -B "000-Build-$BUILD_TYPE-x64" -DCMAKE_BUILD_TYPE=$BUILD_TYPE -S .

echo "==> Configure complete"