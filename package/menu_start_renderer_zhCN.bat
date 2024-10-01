@echo off
setlocal enabledelayedexpansion

set options[0]=����׷����Ⱦ��
set options[1]=�����Ⱦ��
set options[2]=�ִ��ӳ���Ⱦ��
set options[3]=��ͳ�ӳ���Ⱦ��
set options[4]=�˳�

set count=5

:menu
cls
echo ��ѡ��һ��ѡ�Ȼ�󰴻س���ȷ��:
for /L %%i in (0,1,%count%-1) do (
	echo %%i. !options[%%i]!
)

set /p choice=��������ѡ�� (0-%count%):

if %choice% geq 0 if %choice% lss %count% (
	goto execute
) else (
	echo ��Ч��ѡ�������ԡ�
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
echo �ټ���
pause
exit