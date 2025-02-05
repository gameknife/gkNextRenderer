:: filepath: /D:/github/gkNextRenderer/build_tsc.bat
@echo off

IF NOT EXIST "tools\node\node.exe" (
  SET NODE_URL=https://nodejs.org/dist/v18.17.1/node-v18.17.1-win-x64.zip
  powershell -Command "Invoke-WebRequest -Uri $env:NODE_URL -OutFile 'tools\\node.zip'"
  powershell -Command "Expand-Archive -LiteralPath 'tools\\node.zip' -DestinationPath 'tools'"
  ren "tools\node-v18.17.1-win-x64" node
  del "tools\node.zip"
)

tools\node\node.exe tools\typescript\bin\tsc -p src\Typescript\
xcopy /s /y assets\scripts\ build\windows\assets\scripts\