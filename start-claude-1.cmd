@echo off
setlocal

cd /d "%~dp0"

set "CLAUDE_EXE=%USERPROFILE%\.local\bin\claude.exe"
set "CLAUDE_CONFIG_DIR=%USERPROFILE%\.claude-account1"

if not exist "%CLAUDE_EXE%" (
  echo Claude Code was not found:
  echo %CLAUDE_EXE%
  pause
  exit /b 1
)

if not exist "%CLAUDE_CONFIG_DIR%" (
  mkdir "%CLAUDE_CONFIG_DIR%"
)

echo.
echo ========================================
echo Claude Code Account 1
echo Config: %CLAUDE_CONFIG_DIR%
echo ========================================
echo.

"%CLAUDE_EXE%" --dangerously-skip-permissions %*

set "EXIT_CODE=%ERRORLEVEL%"

echo.
echo Claude Code exited with code %EXIT_CODE%.

pause
exit /b %EXIT_CODE%