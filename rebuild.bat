@echo off
taskkill /f /im astockquantapp.exe >nul 2>&1
timeout /t 2 /nobreak >nul
del /f "bin\Release\astockquantapp.dll" 2>nul
cmake --build build --target astockquantapp --config Release
echo.
echo === DLL timestamp ===
dir "bin\Release\astockquantapp.dll" 2>nul | findstr astockquantapp
echo.
echo Done. Start the app now.
pause
