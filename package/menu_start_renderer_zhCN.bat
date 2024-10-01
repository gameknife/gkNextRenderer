@echo off
setlocal enabledelayedexpansion

set options[0]=光线追踪渲染器
set options[1]=混合渲染器
set options[2]=现代延迟渲染器
set options[3]=传统延迟渲染器
set options[4]=退出

set count=5

:menu
cls
echo 请选择一个选项，然后按回车键确认:
for /L %%i in (0,1,%count%-1) do (
	echo %%i. !options[%%i]!
)

set /p choice=输入数字选择 (0-%count%):

if %choice% geq 0 if %choice% lss %count% (
	goto execute
) else (
	echo 无效的选择，请重试。
	pause
	goto menu
)

:execute
if %choice%==4 goto exit

pushd bin
gkNextRenderer.exe --renderer=%choice% --locale=zhCN
popd

pause
goto menu

:exit
echo 再见！
pause
exit