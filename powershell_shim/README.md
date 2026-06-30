# PowerShell Shim — Kiro CLI Hook 弹窗消除方案 (Windows)

## 问题

Windows 下 Kiro CLI 交互式 TUI 模式运行 hook 时，会为每个 hook 弹出一个黑色控制台窗口（命令执行完自动关闭），严重影响使用体验。

## 根因

Kiro 的 TUI 以 `CREATE_NEW_CONSOLE` 标志 spawn `powershell.exe` 来执行 hook 命令，导致 Windows 为其分配一个全新的可见控制台窗口。Kiro 从未传递 `CREATE_NO_WINDOW` 标志。

## 方案原理

1. Kiro 用**裸名 `powershell`** 经 PATH 解析来 spawn hook shell。
2. 本 shim 是一个 **GUI 子系统** (`#![windows_subsystem = "windows"]`) 的假 `powershell.exe`。
3. `CREATE_NEW_CONSOLE` 对 GUI 子系统进程无效——Windows 不会为它分配控制台。
4. Shim 再用 `CREATE_NO_WINDOW` 调用真正的 PowerShell 执行 hook 命令。
5. 全程无弹窗。

## 构建

```bash
cd powershell_shim
cargo build --release
```

产物在 `target/release/powershell.exe`。

## 安装

将构建出的 `powershell.exe` 复制到 `bin/` 目录：

```bash
cp target/release/powershell.exe bin/
```

## 使用

用 `kiro-silent.bat` 启动 Kiro CLI：

```bat
powershell_shim\kiro-silent.bat chat --agent coder
```

启动器会自动把 shimbin 加到 PATH 前面，无需修改系统环境变量。

## 注意事项

- **不要清空 PSModulePath**：否则 Kiro 改用 cmd（绝对路径调用），PATH shim 拦不住。
- 启动时 `where pwsh` 探测仍会闪一下（Kiro 用绝对路径调用 where.exe，无法拦截），对话期间不再弹窗。
- shim 仅对 Windows 有效，Mac/Linux 不需要（Kiro 在非 Windows 平台不使用 PowerShell）。
