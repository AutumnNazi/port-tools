# PortView — Windows Port Inspector

**English** | [简体中文](README_zh.md)

A lightweight native Windows port tool: **find out what holds each port → locate the related files → kill the process**.

- Pure C + Win32 API, no runtime dependencies (statically linked CRT, single exe, ~100 KB)
- No installation, download and run; supports Windows 7 ~ Windows 11 (x64 / x86)
- Full TCP / UDP, IPv4 / IPv6 connections and listening ports
- The interface starts in Chinese and switches to English from the **Language** menu at any time; the choice is saved, so the next launch opens in the language you last used

## Download

The project is under active development. Right now there is a single **rolling dev prerelease**: every push to the `main` branch rebuilds the app and overwrites the latest artifacts of the same name in the Release.

👉 Download: [Releases → dev](../../releases/tag/dev)

| File | For |
| --- | --- |
| `PortView-Windows-x64-dev.exe` | 64-bit Windows (Win7 ~ Win11, the right choice for almost every machine) |
| `PortView-Windows-x86-dev.exe` | 32-bit Windows (older machines) |

Single file, no installer — just double-click. To inspect or end system-level processes, right-click the exe and choose "Run as administrator".

When you want a formal release, tag a `v*` version and push it; that triggers a proper Release (file names without the `-dev` suffix):

```bash
git tag v1.0.0 && git push origin v1.0.0
```

## Features

| Feature | Description |
| --- | --- |
| Port list | Protocol, local and remote address/port, connection state, PID, process name, image path |
| Filtering | The query row offers four conditions: local port, PID, process name, and a general keyword. The first three match only their own field, while the keyword keeps matching across addresses, paths, states and every other field; conditions you fill in together are combined with AND. `Ctrl+F` focuses the keyword box |
| Exact match | When checked in the **Filter** menu, the search text must equal a port, PID, protocol, state, address, process name or file name in full. For example, searching `80` keeps only port 80 and no longer pulls in 8000, 8080, or records whose path contains 80. Off by default |
| Filter menu | The **Filter** menu holds every structured condition: four check items (Auto Refresh / Listening Only / Hide System Ports / Exact Match), the Protocol submenu (All / TCP / UDP / IPv4 / IPv6), and "Clear All Filters". Check marks are shown in the menu itself, and the status bar at the bottom lists the conditions currently in effect |
| Language menu | The **Language** menu switches the whole interface between English and 中文 (Chinese by default; the choice is stored under `HKCU\Software\PortView` and reused on the next launch). Menus, column headers, the query row, the status bar, hints and message boxes all change, and the list you are looking at is repainted and re-sorted in the new language immediately — no restart |
| Listening only | When checked in the **Filter** menu, only ports in the listening state are kept, filtering out the noise of established connections |
| Hide system ports | When checked in the **Filter** menu, ports held by key system processes are dropped — System, csrss, winlogon, services, lsass, svchost and friends (ending these would threaten system stability). Off by default |
| Sorting | Click a column header to sort, click again to reverse |
| Auto refresh | Checked in the **Filter** menu (toggle with `Ctrl+Shift+R`), refreshes every 3 seconds while keeping your selection in place |
| Process details | Double-click any row: shows the image path, the full command line, and every module (DLL / related file) the process has loaded |
| Open file location | Right-click → "Open File Location" to reveal and select the file in Explorer; double-clicking a module row does the same |
| Kill process | Right-click → "Kill Process" / "Kill Process Tree" (all child processes, children first), with a confirmation step |
| Elevation | The status bar shows the current privilege level; clicking it restarts the app elevated so you can handle system-level processes |
| Copy | Copy the image path, the PID, or the whole row |

Shortcuts: `F5` refresh, `Ctrl+F` focus the filter box, `Ctrl+Shift+R` toggle auto refresh, `Esc` clear all filters, `Alt+F` open the Filter menu, `Alt+L` open the Language menu, double-click for details, right-click for the context menu.

## Why does it need administrator rights?

- Standard user: you can see every connection on the machine with its PID and process name, and end the processes you own.
- Administrator: you can also read the full path, command line and module list of system and other users' processes, and end them.

The program starts as a standard user (`asInvoker`) and only elevates when you ask it to, so there is no UAC prompt on every launch.

## Building locally

Requires Visual Studio (MSVC) and the Windows SDK:

```bat
rem run this in the "x64 Native Tools Command Prompt for VS"
build.bat        :: build x64
build.bat x86    :: build x86
```

The result is `build\PortView.exe`. CI builds and publishes with GitHub Actions (`windows-latest` + MSVC);
pushing a `v*` tag creates the Release and uploads both the x64 and x86 exe.

## Project layout

```
src/
  portview.h   shared structures and interfaces
  main.c       entry point (common controls, SeDebugPrivilege, message loop)
  ports.c      enumerate ports via GetExtendedTcpTable/UdpTable, resolve the owning process
  proc.c       process snapshot, image path, command line (PEB), module list, kill process/tree, UAC elevation
  ui.c         main window (list / filtering / sorting / menus), process details window, and the EN/ZH text table
  app.rc       version info and manifest (ComCtl32 v6, Per-Monitor DPI)
```

Implementation notes:
- Port data comes from `GetExtendedTcpTable` / `GetExtendedUdpTable` (which include the PID), process names from a Toolhelp snapshot, image paths from `QueryFullProcessImageNameW`, cached by PID so the same process is not opened over and over.
- The command line is read with `NtQueryInformationProcess` from the target's PEB (`ProcessParameters.CommandLine`), with WOW64 handled automatically (a 32-bit process is read through its 32-bit PEB).
- Related files come from `EnumProcessModulesEx(LIST_MODULES_ALL)`, i.e. every module the process has loaded.
- Connection state is carried as the raw kernel value and turned into text at render time, so switching language needs no re-enumeration.
- High DPI uses `GetDpiForWindow` + `SystemParametersInfoForDpi` to scale fonts and layout, and responds to `WM_DPICHANGED`.

## License

MIT
