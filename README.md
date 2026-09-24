# PortView — Windows 端口占用查看器

一个轻量的 Windows 原生端口工具：**查看每个端口被谁占用 → 定位关联文件 → 结束进程**。

- 纯 C + Win32 API，无任何运行时依赖（静态链接 CRT，单文件 exe，约 100 KB）
- 不需要安装，下载即用，支持 Windows 7 ~ Windows 11（x64 / x86）
- 支持 TCP / UDP、IPv4 / IPv6 全量连接与监听端口

## 下载

项目处于开发阶段，目前提供一个**滚动更新的 dev 预发布**：每次推送到 `main` 分支会自动重新构建，并覆盖 Release 里同名的最新产物。

👉 下载入口：[Releases → dev](../../releases/tag/dev)

| 文件 | 适用 |
| --- | --- |
| `PortView-Windows-x64-dev.exe` | 64 位 Windows（Win7 ~ Win11，绝大多数电脑选这个） |
| `PortView-Windows-x86-dev.exe` | 32 位 Windows（老机器） |

单文件、免安装，双击运行即可；要查看或结束系统级进程请右键「以管理员身份运行」。

需要正式版本时，打 `v*` 标签推送即可触发正式 Release（文件名不带 `-dev`）：

```bash
git tag v1.0.0 && git push origin v1.0.0
```

## 功能

| 功能 | 说明 |
| --- | --- |
| 端口列表 | 协议、本地/远程地址与端口、连接状态、PID、进程名、映像路径 |
| 过滤 | 顶部输入框可按端口号、PID、进程名、路径、状态任意匹配（`Ctrl+F` 聚焦） |
| 筛选菜单 | 菜单栏「筛选」集中放所有结构化条件：三个勾选项（自动刷新 / 仅监听端口 / 隐藏系统端口）、协议子菜单（全部 / TCP / UDP / IPv4 / IPv6）、以及「清除全部筛选」。勾选状态实时显示在菜单左侧，底部状态栏同步提示当前生效的条件 |
| 仅监听端口 | 菜单栏「筛选」勾选后只保留处于监听状态的端口，滤掉大量已建立的连接噪声 |
| 隐藏系统端口 | 菜单栏「筛选」勾选后滤掉 System、csrss、winlogon、services、lsass、svchost 等系统关键进程占用的端口（这类进程结束会危及系统稳定性）。默认关闭 |
| 排序 | 点击列头排序，再次点击反转 |
| 自动刷新 | 菜单栏「筛选」勾选（`Ctrl+Shift+R` 切换），每 3 秒刷新一次（保持选中项不跳） |
| 进程详情 | 双击任意行：显示映像路径、完整命令行、该进程加载的全部模块（DLL/关联文件） |
| 打开文件位置 | 右键 → “打开文件所在位置”，直接在资源管理器中定位并选中该文件；模块列表双击同理 |
| 结束进程 | 右键 → “结束进程” / “结束进程树”（含全部子进程，先子后父），带二次确认 |
| 提权 | 状态栏显示当前权限；点 “以管理员身份重启” 可一键提权以处理系统级进程 |
| 复制 | 复制映像路径、PID 或整行信息 |

快捷键：`F5` 刷新，`Ctrl+F` 定位过滤框，`Ctrl+Shift+R` 切换自动刷新，`Esc` 清除全部筛选，`Alt+F` 打开筛选菜单，双击查看详情，右键菜单。

## 为什么需要管理员权限？

- 普通权限：可查看本机所有连接与对应 PID、进程名，能结束自己的进程。
- 管理员权限：可读取系统/其它用户进程的完整路径、命令行与模块列表，并结束它们。

程序默认以普通权限启动（`asInvoker`），需要时点右上角的 “以管理员身份重启”，不会每次弹 UAC。

## 本地编译

需要 Visual Studio（MSVC）与 Windows SDK：

```bat
rem 在 “x64 Native Tools Command Prompt for VS” 中执行
build.bat        :: 编译 x64
build.bat x86    :: 编译 x86
```

产物在 `build\PortView.exe`。CI 使用 GitHub Actions（`windows-latest` + MSVC）编译并发布，
推送 `v*` 标签即自动创建 Release 并上传 x64/x86 两个 exe。

## 代码结构

```
src/
  portview.h   公共结构与接口
  main.c       入口（公共控件初始化、SeDebugPrivilege、消息循环）
  ports.c      GetExtendedTcpTable/UdpTable 枚举端口，解析占用进程
  proc.c       进程快照、映像路径、命令行（PEB）、模块枚举、结束进程/进程树、UAC 提权
  ui.c         主窗口（列表/过滤/排序/菜单）与进程详情窗口
  app.rc       版本信息与清单（ComCtl32 v6、Per-Monitor DPI）
```

实现要点：
- 端口数据来自 `GetExtendedTcpTable` / `GetExtendedUdpTable`（含 PID），进程名来自 Toolhelp 快照，映像路径来自 `QueryFullProcessImageNameW`，并按 PID 缓存，避免重复 `OpenProcess`。
- 命令行通过 `NtQueryInformationProcess` 读取目标进程 PEB 的 `ProcessParameters.CommandLine`，自动适配 WOW64（32 位进程读 32 位 PEB）。
- 关联文件通过 `EnumProcessModulesEx(LIST_MODULES_ALL)` 获取进程加载的所有模块。
- 高 DPI 使用 `GetDpiForWindow` + `SystemParametersInfoForDpi` 动态缩放字体与布局，并响应 `WM_DPICHANGED`。

## 许可

MIT
