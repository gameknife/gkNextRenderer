#!/bin/sh
set -e

build/vcpkg.linux/installed/x64-linux/tools/node/bin/node tools/typescript/bin/tsc -p src/Typescript/
# copy the output from assets\scripts\ to build\linux\assets\scripts\, make sure the directory exists
cp -r assets/scripts build/linux/assets/
