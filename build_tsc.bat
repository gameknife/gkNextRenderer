:: filepath: /D:/github/gkNextRenderer/build_tsc.bat
@echo off

IF NOT EXIST "tools\node\node.exe" (
  SET NODE_URL=https://nodejs.org/dist/v18.17.1/node-v18.17.1-win-x64.zip
  IF NOT EXIST "tools\node" (
    mkdir "tools\node"
  )
  powershell -Command "Invoke-WebRequest -Uri $env:NODE_URL -OutFile 'tools\\node.zip'"
  powershell -Command "Expand-Archive -LiteralPath 'tools\\node.zip' -DestinationPath 'tools'"
  ren "tools\node-v18.17.1-win-x64" node
  del "tools\node.zip"
)

IF NOT EXIST "tools\typescript\bin\tsc" (
  SET TSC_URL=https://registry.npmjs.org/typescript/-/typescript-4.5.4.tgz
  IF NOT EXIST "tools\typescript" (
    mkdir "tools\typescript"
  )
  powershell -Command "Invoke-WebRequest -Uri $env:TSC_URL -OutFile 'tools\\typescript.tgz'"
  tar -xzf tools\typescript.tgz -C tools\typescript --strip-components=1
  del "tools\\typescript.tgz"
)

tools\node\node.exe tools\typescript\bin\tsc -p src\Typescript\
xcopy /s /y assets\scripts\ build\windows\assets\scripts\