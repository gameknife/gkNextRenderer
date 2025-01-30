build\vcpkg.windows\installed\x64-windows\tools\node\node.exe tools\typescript\bin\tsc -p src\Typescript\
rem copy the output from assets\scripts\ to build\windows\assets\scripts\
xcopy /s /y assets\scripts\ build\windows\assets\scripts\