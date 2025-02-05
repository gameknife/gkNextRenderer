#!/bin/sh

set -e

# Detect the operating system
OS=$(uname -s)

# Set the Node.js download URL based on the operating system
if [ "$OS" = "Linux" ]; then
  NODE_URL="https://nodejs.org/dist/v18.17.1/node-v18.17.1-linux-x64.tar.xz"
  BUILD_NAME="linux"
elif [ "$OS" = "Darwin" ]; then
  NODE_URL="https://nodejs.org/dist/v18.17.1/node-v18.17.1-darwin-x64.tar.xz"
  BUILD_NAME="macos"
else
  echo "Unsupported OS: $OS"
  exit 1
fi

# Check if tools/node exists
if [ ! -d "tools/node" ]; then
  mkdir -p tools/node
  cd tools/node
  curl -o node.tar.xz $NODE_URL
  tar -xf node.tar.xz --strip-components=1
  rm node.tar.xz
  cd ../..
fi

tools/node/bin/node tools/typescript/bin/tsc -p src/Typescript/
# copy the output from assets\scripts\ to build\linux\assets\scripts\, make sure the directory exists
cp -r assets/scripts build/$BUILD_NAME/assets/