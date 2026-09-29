# PortView — Windows Port Inspector

**English** | [简体中文](README_zh.md)

A lightweight native Windows port tool: **find out what holds each port → locate the related files → kill the process**.

- Pure C + Win32 API, no runtime dependencies (statically linked CRT, single exe, ~230 KB)
- No installation, download and run; supports Windows 7 ~ Windows 11 (x64 / x86)
- Full TCP / UDP, IPv4 / IPv6 connections and listening ports
- The interface starts in Chinese and switches to English from the **Language** dropdown on the toolbar at any time; the choice is saved, so the next launch opens in the language you last used

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
| Port list | Protocol, local and remote address/port, connection state, PID, process name, image path — with real process icons and the connection state shown as a colored badge |
| Filtering | The query row offers four conditions: local port, PID, process name, and a general keyword. The first three match only their own field, while the keyword keeps matching across addresses, paths, states and every other field; conditions you fill in together are combined with AND. `Ctrl+F` focuses the keyword box, `✕` (or `Esc`) clears everything |
| Toolbar switches | Auto Refresh / Listening Only / Hide System Ports / Exact Match / Always-on-top live on the toolbar as toggle buttons — press to flip, no menu diving. Hover any button for a one-line explanation |
| Exact match | When the Exact Match switch is on, the search text must equal a port, PID, protocol, state, address, process name or file name in full. For example, searching `80` keeps only port 80 and no longer pulls in 8000, 8080, or records whose path contains 80. Off by default |
| Filter menu | The **Filter** dropdown (still on the toolbar) holds every structured condition: the four switches, the Protocol submenu (All / TCP / UDP / IPv4 / IPv6), and "Clear All Filters" |
| Language menu | The **Language** dropdown switches the whole interface between English and 中文 (Chinese by default; the choice is stored under `HKCU\Software\PortView` and reused on the next launch). Menus, column headers, the query row, the status bar, hints and message boxes all change, and the list you are looking at is repainted and re-sorted in the new language immediately — no restart |
| Listening only | When the Listening Only switch is on, only ports in the listening state are kept, filtering out the noise of established connections |
| Hide system ports | When the Hide System Ports switch is on, ports held by key system processes are dropped — System, csrss, winlogon, services, lsass, svchost and friends (ending these would threaten system stability). Off by default |
| New-connection highlight | Connections that just appeared flash green and fade out; connections that just dropped stay behind in red and fade out before leaving the list — you can see at a glance what came and went between refreshes |
| Sorting | Click a column header to sort, click again to reverse; the sort column and direction are remembered across launches |
| Auto refresh | On by default (toolbar switch or `Ctrl+Shift+R`), refreshes every 3 seconds while keeping your selection in place |
| Service names | Hovering a row shows the service name for the port (from the system services table, e.g. `https` for 443) together with the full image path |
| Process details | Double-click any row: shows the image path, the full command line, and every module (DLL / related file) the process has loaded |
| Open file location | Right-click → "Open File Location" to reveal and select the file in Explorer; double-clicking a module row does the same |
| Kill process | Right-click → "Kill Process" / "Kill Process Tree" (all child processes, children first), with a confirmation step |
| Elevation | The toolbar badge shows the current privilege level; clicking it restarts the app elevated **with your current filters carried over**, so you can handle system-level processes without setting everything up again |
| Export CSV | `Ctrl+S` or right-click → "Export as CSV" writes the filtered view to a CSV file (UTF-8 with BOM, opens in Excel without fuss) |
| Always on top | The pin switch on the toolbar keeps the window above all others; the choice is remembered |
| Remembers your layout | Window position, size, maximized state, sort order and column widths are saved on exit and restored on the next launch |
| Copy | Copy the image path, the PID, or the whole row |

Shortcuts: `F5` refresh, `Ctrl+F` focus the filter box, `Ctrl+Shift+R` toggle auto refresh, `Ctrl+S` export CSV, `Esc` clear all filters, `Alt+F` open the Filter dropdown, `Alt+L` open the Language dropdown, double-click for details, right-click for the context menu.

## Command line

Filters can be preset from the command line — handy for shortcuts and scripts:

```text
PortView.exe [--port N] [--pid N] [--name NAME] [--key TEXT]
             [--proto 0..4] [--listen 0|1] [--hidesys 0|1] [--exact 0|1] [--auto 0|1]
```

- `--port` / `--pid` / `--name` — preset the three exact match fields
- `--key` — preset the keyword box (e.g. `--key "chrome.exe"`)
- `--proto` — `0` all, `1` TCP only, `2` UDP only, `3` IPv4 only, `4` IPv6 only
- `--listen` / `--hidesys` / `--exact` / `--auto` — turn the corresponding switch on (`1`) or off (`0`) at start

Example: `PortView.exe --port 8080 --listen 1` opens already filtered to listening port 8080.
Elevating from inside the app passes the current filters to the new instance automatically.

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
