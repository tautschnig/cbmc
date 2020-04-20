@REM Build CBMC with Visual Studio 2016

echo "Set up PATH to find Visual Studio tools"
call "C:\Program Files (x86)\Microsoft Visual Studio\2016\Enterprise\VC\Auxiliary\Build\vcvars64.bat"

echo "Set up PATH to find Git Bash shell tools and bison/flex"
PATH=%PATH%;"c:\Program Files\Git\usr\bin";%cd%

echo "Remove chocolatey"
del /s /q "C:\ProgramData\Chocolatey\bin"

REM No cmake in VS2016
REM echo "Remove cmake"
REM del /s /q "C:\Program Files\CMake\bin"

echo "Remove strawberry"
del /s /q "c:/Strawberry/c/bin"

REM No ninja in VS2016
echo "Configure CBMC with cmake"
cmake -S. -Bbuild -DWITH_JBMC=NO
REM cmake -S. -Bbuild -GNinja -DWITH_JBMC=NO

echo "Build CBMC with cmake"
cmake --build build
