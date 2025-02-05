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

# Check if tools/typescript/bin/tsc exists
if [ ! -f "tools/typescript/bin/tsc" ]; then
  TSC_URL="https://registry.npmjs.org/typescript/-/typescript-4.5.4.tgz"
  mkdir -p tools/typescript
  curl -o tools/typescript.tgz $TSC_URL
  tar -xzf tools/typescript.tgz -C tools/typescript --strip-components=1
  rm tools/typescript.tgz
fi

tools/node/bin/node tools/typescript/bin/tsc -p src/Typescript/
# copy the output from assets/scripts to build/$BUILD_NAME/assets/scripts, make sure the directory exists
cp -r assets/scripts build/$BUILD_NAME/assets/