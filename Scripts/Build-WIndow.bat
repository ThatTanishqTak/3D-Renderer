@echo off
echo Runing build script...

cmake -B "../Build" "../"
cmake .

pause