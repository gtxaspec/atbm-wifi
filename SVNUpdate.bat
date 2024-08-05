@echo off
::set svn_path="E:\jiangyongjiu\rtos_hmac"
set svn_path=.\
set template_file=.\svn_version.h.template
set version_file=.\hal_apollo\svn_version.h

::/closeonend：0不自动关闭对话框
::/closeonend：1会自动关闭，如果没有错误
::/closeonend：2会自动关闭，如果没有发生错误和冲突
::/closeonend：3会自动关闭，如果没有错误，冲突和合并
::/closeonend：4会自动关闭，如果没有错误，冲突和合并
TortoiseProc.exe /command:update /path:%svn_path% /closeonend:2

IF %ERRORLEVEL% NEQ 0 goto ERROR

echo 文件更新成功

subwcrev.exe  %svn_path% %template_file%    %version_file%

IF %ERRORLEVEL% NEQ 0 goto ERROR

echo SVN版本号文件写入成功

goto OK

:ERROR
echo 更新失败，请检查
pause
exit
:OK
pause