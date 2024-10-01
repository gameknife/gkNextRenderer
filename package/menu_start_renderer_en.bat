@echo off
setlocal enabledelayedexpansion

set options[0]=RayQueryRenderer(RTPipeline)
set options[1]=HybridRenderer
set options[2]=ModernDeferredRenderer
set options[3]=LegacyDeferredRenderer
set options[4]=Exit

set count=5

:menu
cls
echo Please select an option and press Enter:
for /L %%i in (0,1,%count%-1) do (
	echo %%i. !options[%%i]!
)

set /p choice=Enter a number to make a selection (0-%count%):

if %choice% geq 0 if %choice% lss %count% (
	goto execute
) else (
	echo Invalid selection, please try again.
	pause
	goto menu
)

:execute
if %choice%==4 goto exit

pushd bin
gkNextRenderer.exe --renderer=%choice% --locale=en
popd

pause
goto menu

:exit
echo Bye！
pause
exit