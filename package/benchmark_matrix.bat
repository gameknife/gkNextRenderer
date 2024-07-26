@echo off
pushd bin
for /L %%i in (0,1,4) do (
	gkNextRenderer.exe --benchmark --next-scenes --renderer=%%i
)
popd