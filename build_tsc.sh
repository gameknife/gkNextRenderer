#!/bin/sh

set -e

# Check if tools/node exists
if [ ! -d "tools/node" ]; then
  mkdir -p tools/node
  cd tools/node
  curl -o node.tar.xz https://nodejs.org/dist/v18.17.1/node-v18.17.1-linux-x64.tar.xz
  tar -xf node.tar.xz --strip-components=1
  rm node.tar.xz
  cd ../..
fi

tools/node/bin/node tools/typescript/bin/tsc -p src/Typescript/
# copy the output from assets\scripts\ to build\linux\assets\scripts\, make sure the directory exists
cp -r assets/scripts build/linux/assets/
