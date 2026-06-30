//! powershell.exe shim — a GUI-subsystem stand-in for Windows PowerShell that
//! runs the REAL powershell with CREATE_NO_WINDOW so Kiro CLI hooks never flash
//! a console window.
//!
//! Why this works:
//!   * Kiro spawns hooks as `powershell -Command "<cmd>"` using the bare name,
//!     resolved via PATH. In TUI mode it passes CREATE_NEW_CONSOLE, which forces
//!     a new visible console for a CONSOLE-subsystem child.
//!   * CREATE_NEW_CONSOLE has NO effect on a GUI-subsystem process — Windows
//!     never allocates a console for it. So this shim itself shows no window.
//!   * The shim re-launches the real powershell.exe with CREATE_NO_WINDOW
//!     (the flag Kiro forgot), inheriting our (absent) console, and pipes
//!     stdin/stdout/stderr through, forwarding the exit code.
//!
//! Install: put the built powershell.exe on a directory that precedes
//! System32\WindowsPowerShell\v1.0 on PATH for the Kiro process only.

#![windows_subsystem = "windows"]

use std::env;
use std::os::windows::process::CommandExt;
use std::process::{Command, Stdio};

// Absolute path to the genuine Windows PowerShell, so we never recurse into ourselves.
const REAL_PWSH: &str = r"C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe";

// https://learn.microsoft.com/windows/win32/procthread/process-creation-flags
const CREATE_NO_WINDOW: u32 = 0x0800_0000;

fn main() {
    // Forward every argument Kiro passed (e.g. -Command "<hook cmd>").
    let args: Vec<String> = env::args().skip(1).collect();

    let status = Command::new(REAL_PWSH)
        .args(&args)
        // Inherit the shim's std handles so the hook's stdin (JSON) and
        // stdout/stderr (consumed by Kiro) pass straight through.
        .stdin(Stdio::inherit())
        .stdout(Stdio::inherit())
        .stderr(Stdio::inherit())
        // The fix Kiro omits: run the real shell with no console window.
        .creation_flags(CREATE_NO_WINDOW)
        .status();

    let code = match status {
        Ok(s) => s.code().unwrap_or(1),
        Err(_) => 1,
    };
    std::process::exit(code);
}
