@echo off
REM ============================================================================
REM kiro-silent.bat - Launch Kiro CLI with NO hook popup windows on Windows.
REM
REM Root cause (verified): Kiro's interactive TUI spawns hook commands as
REM   powershell -Command "<hook>"  using CREATE_NEW_CONSOLE, which forces a
REM   new VISIBLE console window for each hook. Kiro never passes
REM   CREATE_NO_WINDOW, so every hook flashes a window. Changing the hook
REM   command itself (pythonw / vbs / GUI exe) cannot help: the window belongs
REM   to powershell and is created BEFORE the hook command runs.
REM
REM The fix: prepend a GUI-subsystem "powershell.exe" shim to PATH. Kiro picks
REM   the hook shell with M4e(): if PSModulePath is set (it is, by default) it
REM   uses `powershell` resolved by BARE NAME via PATH -- so our shim wins.
REM   CREATE_NEW_CONSOLE has no effect on a GUI-subsystem process, so the shim
REM   itself shows no window; it then runs the REAL powershell with
REM   CREATE_NO_WINDOW. This covers ALL hooks, including the agentSpawn hook
REM   that fires on startup (the "startup flash" you saw was that hook).
REM
REM   IMPORTANT: do NOT clear PSModulePath. Doing so makes Kiro use cmd, which
REM   it spawns by ABSOLUTE path (C:\Windows\System32\cmd.exe) -- a PATH/ComSpec
REM   shim cannot intercept it, so the flash returns. PowerShell is the only
REM   shell Kiro resolves by bare name, hence interceptable.
REM
REM Usage:
REM   kiro-silent.bat                 (defaults to: chat)
REM   kiro-silent.bat chat --agent coder
REM   kiro-silent.bat chat --agent planner
REM ============================================================================

setlocal

REM Directory holding the GUI-subsystem powershell.exe shim.
set "SHIMDIR=%~dp0bin"

REM Prepend shim dir so Kiro resolves OUR powershell.exe first (bare-name lookup).
set "PATH=%SHIMDIR%;%PATH%"

REM Absolute path to the real Kiro CLI.
set "KIRO=%LOCALAPPDATA%\Kiro-Cli\kiro-cli.exe"

REM Default to `chat` if no args given.
if "%~1"=="" (
    "%KIRO%" chat
) else (
    "%KIRO%" %*
)

endlocal
