@echo off
pushd bin
for /L %%i in (0,1,2) do (
	gkNextMotionBenchmark.exe --renderer=%%i
)
popd