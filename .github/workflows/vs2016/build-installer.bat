@REM Install bison and flex

echo on

set PKG=%1

echo "Set up PATH to find wix"
PATH=%PATH%;"C:\Program Files (x86)\WiX Toolset v3.11\bin"

echo "Build installer: run candle"
candle -o cbmc.wixobj -arch x64 .github\workflows\vs2016\cbmc.wxs

echo "Build installer: run light"
light -o %PKG% cbmc.wixobj

echo "Package is %PKG%"
echo %PKG%

echo %cd%
dir %cd%
