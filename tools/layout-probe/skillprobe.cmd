@echo off
REM Build+run skillprobe (x86) — offsetof for SCHARSKILL / SNETLOBBY_CHARSKILL.
REM Does not modify SOURCE/.
setlocal
cd /d "%~dp0"

set "VSPATH=C:\Program Files\Microsoft Visual Studio\2022\Community"
call "%VSPATH%\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
if errorlevel 1 (
  echo [!] vcvars32 failed - check VSPATH in this script
  exit /b 1
)

set "SRC=%~dp0..\..\..\SOURCE"

if not exist obj mkdir obj

cl /nologo /EHsc /W1 /DWIN32 /D_WINDOWS ^
   /I"%SRC%\Lib_Network" ^
   /I"%SRC%\Lib_Client" ^
   /I"%SRC%\Lib_Client\G-Logic" ^
   /I"%SRC%\Lib_Engine" ^
   /I"%SRC%\Lib_Engine\G-Logic" ^
   /I"%SRC%\Lib_Engine\Common" ^
   /I"%SRC%\Lib_Engine\DxCommon9" ^
   /I"%SRC%\Lib_Engine\DxCommon" ^
   /I"%SRC%\Lib_Engine\dxframe" ^
   /I"%SRC%\Lib_Engine\DxSound" ^
   /I"%SRC%\Lib_Engine\Meshs" ^
   /I"%SRC%\Lib_Engine\NaviMesh" ^
   /I"%SRC%\Lib_Engine\DxOctree" ^
   /I"%SRC%\Lib_Engine\DxEffect" ^
   /I"%SRC%\Tik\Include" ^
   /I"%SRC%\Tik\DXInclude" ^
   skillprobe.cpp "%SRC%\Lib_Network\minTea.cpp" ^
   /Fe:skillprobe.exe /Foobj\ ^
   /link /LIBPATH:"%SRC%\Tik\Library" /LIBPATH:"%SRC%\Tik\DXLib" ws2_32.lib winmm.lib

if errorlevel 1 ( echo [!] compile failed & exit /b 1 )
.\skillprobe.exe
endlocal
