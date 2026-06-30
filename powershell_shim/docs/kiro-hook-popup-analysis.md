# Kiro CLI Hook 弹窗问题：分析与解决方案

## 问题现象

Windows 下 Kiro CLI 交互式 TUI 模式运行 hook 时，每次 hook 触发都弹出一个黑色控制台窗口（命令执行完自动关闭）。

## 根本原因

Kiro CLI 的 TUI 以 `CREATE_NEW_CONSOLE` 标志 spawn `powershell.exe` 来执行 hook 命令。
`CREATE_NEW_CONSOLE` 导致 Windows 为 powershell 进程分配一个全新的可见控制台窗口。
Kiro **从未传递 `CREATE_NO_WINDOW` 标志**。

窗口属于 powershell 进程，在 hook 命令启动**之前**就已被创建。因此任何修改 hook 命令本身的方案（pythonw、VBS、GUI exe）都无效。

## 解决方案：GUI 子系统 PowerShell Shim

核心原理：`CREATE_NEW_CONSOLE` 对 GUI 子系统进程无效——Windows 不会为它分配控制台。

1. Kiro 用裸名 `powershell` 经 PATH 解析
2. 本 shim（GUI 子系统）抢先被解析到 → 无窗口
3. Shim 用 `CREATE_NO_WINDOW` 调用真正的 PowerShell → 无窗口

## 重要限制

- 不要清空 `PSModulePath`：否则 Kiro 改用 cmd（绝对路径），shim 拦不住
- 启动时 `where pwsh` 探测会闪一次（Kiro 用绝对路径调用 where.exe，无法拦截）
- 对话期间 hook 弹窗彻底消除

## 验证结果

使用 Win32 API 探针实证：
- 对话期 hook：`GetConsoleWindow() = 0x0`（无窗口）✓
- Hook 功能正常，stdin/stdout/stderr 透传，退出码正确 ✓
