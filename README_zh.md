# PortView — Windows 端口占用查看器

[English](README.md) | **简体中文**

一个轻量的 Windows 原生端口工具：**查看每个端口被谁占用 → 定位关联文件 → 结束进程**。

- 纯 C + Win32 API，无任何运行时依赖（静态链接 CRT，单文件 exe，约 230 KB）
- 不需要安装，下载即用，支持 Windows 7 ~ Windows 11（x64 / x86）
- 支持 TCP / UDP、IPv4 / IPv6 全量连接与监听端口
- 界面默认中文，可通过工具条上的「语言」下拉随时切换中英文；选择会被记住，下次打开还是上次的语言

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
| 端口列表 | 协议、本地/远程地址与端口、连接状态、PID、进程名、映像路径；行首带真实进程图标，连接状态以彩色胶囊徽章呈现 |
| 过滤 | 查询区提供四个条件：本地端口、PID、进程名、综合关键字。前三个分别只匹配对应字段，综合关键字继续匹配地址、路径、状态等所有字段；多个条件同时填写时必须全部符合。`Ctrl+F` 聚焦综合关键字框，`✕`（或 `Esc`）一键清除 |
| 工具条开关 | 自动刷新 / 仅监听 / 隐藏系统端口 / 精确匹配 / 窗口置顶以切换按钮的形式放在工具条上——按一下就翻转，不用翻菜单；悬停任意按钮有一行说明 |
| 精确匹配 | 「精确匹配」开关打开后，搜索内容必须与端口、PID、协议、状态、地址、进程名或文件名完全相同。例如搜索 `80` 只保留端口 80，不再带出 8000、8080 或路径中包含 80 的记录。默认关闭 |
| 筛选菜单 | 工具条右侧的「筛选」下拉集中放所有结构化条件：四个开关、协议子菜单（全部 / TCP / UDP / IPv4 / IPv6）、以及「清除全部筛选」 |
| 界面语言 | 「语言」下拉在 English / 中文 之间切换，默认中文，选择写进 `HKCU\Software\PortView`，重开程序自动沿用。菜单、列头、查询区、状态栏、提示与消息框一起换，正在显示的列表随即按新语言重绘与重排，不需要重启 |
| 仅监听端口 | 「仅监听」开关打开后只保留处于监听状态的端口，滤掉大量已建立的连接噪声 |
| 隐藏系统端口 | 「隐藏系统端口」开关打开后滤掉 System、csrss、winlogon、services、lsass、svchost 等系统关键进程占用的端口（这类进程结束会危及系统稳定性）。默认关闭 |
| 新连接高亮 | 刚出现的连接闪绿底渐退；刚断开的连接保留红底渐退后再移出列表——两次刷新之间谁来了、谁走了，一眼可见 |
| 排序 | 点击列头排序，再次点击反转；排序列与方向会被记住，下次打开还是上次的排序 |
| 自动刷新 | 默认开启（工具条开关，`Ctrl+Shift+R` 切换），每 3 秒刷新一次（保持选中项不跳） |
| 服务名提示 | 悬停任意行显示端口对应的服务名（读系统 services 表，如 443 → https）以及完整映像路径 |
| 进程详情 | 双击任意行：显示映像路径、完整命令行、该进程加载的全部模块（DLL/关联文件） |
| 打开文件位置 | 右键 → “打开文件所在位置”，直接在资源管理器中定位并选中该文件；模块列表双击同理 |
| 结束进程 | 右键 → “结束进程” / “结束进程树”（含全部子进程，先子后父），带二次确认 |
| 提权 | 工具条徽章显示当前权限；点击一键提权，**当前筛选条件会原样带到新实例**，提完权接着看同一个视图 |
| 导出 CSV | `Ctrl+S` 或右键 → “导出为 CSV”，把筛选后的视图写成 CSV 文件（UTF-8 带 BOM，Excel 直接打开不乱码） |
| 窗口置顶 | 工具条图钉开关让窗口常驻最前，状态会被记住 |
| 布局记忆 | 窗口位置、尺寸、最大化状态、排序列与方向、拖过的列宽，退出即存、下次打开原样恢复 |
| 复制 | 复制映像路径、PID 或整行信息 |

快捷键：`F5` 刷新，`Ctrl+F` 定位过滤框，`Ctrl+Shift+R` 切换自动刷新，`Ctrl+S` 导出 CSV，`Esc` 清除全部筛选，`Alt+F` 打开筛选下拉，`Alt+L` 打开语言下拉，双击查看详情，右键菜单。

## 命令行参数

筛选条件可以从命令行直接带入，适合做快捷方式和脚本：

```text
PortView.exe [--port N] [--pid N] [--name NAME] [--key TEXT]
             [--proto 0..4] [--listen 0|1] [--hidesys 0|1] [--exact 0|1] [--auto 0|1]
```

- `--port` / `--pid` / `--name` — 预填三个精确字段
- `--key` — 预填综合关键字框（如 `--key "chrome.exe"`）
- `--proto` — `0` 全部、`1` 仅 TCP、`2` 仅 UDP、`3` 仅 IPv4、`4` 仅 IPv6
- `--listen` / `--hidesys` / `--exact` / `--auto` — 启动时把对应开关置开（`1`）或关（`0`）

例如 `PortView.exe --port 8080 --listen 1` 打开即已筛出监听中的 8080。
程序内一键提权时，当前筛选会自动带给新实例。

## 为什么需要管理员权限？

- 普通权限：可查看本机所有连接与对应 PID、进程名，能结束自己的进程。
- 管理员权限：可读取系统/其它用户进程的完整路径、命令行与模块列表，并结束它们。

程序默认以普通权限启动（`asInvoker`），需要时点工具条上的“提权”徽章即可一键提权，不会每次弹 UAC。

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
  ui.c         主窗口（列表/过滤/排序/菜单）、进程详情窗口与中英文案表
  app.rc       版本信息与清单（ComCtl32 v6、Per-Monitor DPI）
```

实现要点：
- 端口数据来自 `GetExtendedTcpTable` / `GetExtendedUdpTable`（含 PID），进程名来自 Toolhelp 快照，映像路径来自 `QueryFullProcessImageNameW`，并按 PID 缓存，避免重复 `OpenProcess`。
- 命令行通过 `NtQueryInformationProcess` 读取目标进程 PEB 的 `ProcessParameters.CommandLine`，自动适配 WOW64（32 位进程读 32 位 PEB）。
- 关联文件通过 `EnumProcessModulesEx(LIST_MODULES_ALL)` 获取进程加载的所有模块。
- 连接状态只带内核原值，渲染时才转成文字，所以切换语言不需要重新枚举端口表。
- 高 DPI 使用 `GetDpiForWindow` + `SystemParametersInfoForDpi` 动态缩放字体与布局，并响应 `WM_DPICHANGED`。

## 许可

MIT
