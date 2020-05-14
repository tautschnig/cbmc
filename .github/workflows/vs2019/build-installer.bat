@REM Build installer

echo on

set PKG=%1
set NAME=cbmc-latest
set SCRIPTDIR=.github\workflows\vs2019

echo "Set up PATH to find wix"
PATH=%PATH%;"C:\Program Files (x86)\WiX Toolset v3.11\bin"

echo "Build installer: run candle"
candle -o %NAME%.wixobj -arch x64 %SCRIPTDIR%\%NAME%.wxs

echo "Build installer: run light"
light -o %PKG% -ext WixUIExtension %NAME%.wixobj

echo "Package is %PKG%"
echo %PKG%

echo %cd%
dir %cd%
