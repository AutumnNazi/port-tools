/* 界面：端口列表、过滤/排序、右键菜单、进程详情窗口 */
#include "portview.h"

#include <uxtheme.h>
#include <commdlg.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "comdlg32.lib")

#define MAIN_CLASS   L"PortViewMainWindow"
#define DETAIL_CLASS L"PortViewDetailWindow"

#define ID_EDIT_FILTER 1001
#define ID_EDIT_PORT   1014
#define ID_EDIT_PID    1015
#define ID_EDIT_NAME   1016
#define ID_LBL_PORT    1021
#define ID_LBL_PID     1022
#define ID_LBL_NAME    1023
#define ID_LBL_KEY     1024
#define ID_BTN_REFRESH 1002
#define ID_LIST        1005
#define ID_STATUS      1006
#define ID_TIMER       1007
#define ID_FILTER_TIMER 1012   /* 筛选输入防抖 */
#define ID_KILL_TIMER  1013    /* 结束进程期间的界面心跳 */
#define ID_TOOLBAR     1017    /* 顶栏工具条 */
#define ID_BTN_CLEAR   1018    /* 查询区清除按钮 */
#define ID_HL_TIMER    1019    /* 新增/消失高亮的渐退重绘 */

#define IDM_OPEN_LOC   2001
#define IDM_DETAIL     2002
#define IDM_KILL       2003
#define IDM_KILL_TREE  2004
#define IDM_COPY_PATH  2005
#define IDM_COPY_PID   2006
#define IDM_COPY_ROW   2007
#define IDM_FILTER_SEL 2008
#define IDM_CLEAR      2009    /* Esc: 清空筛选框与所有结构化条件 */
#define IDM_ELEVATE    2010    /* Ctrl+Shift+E: 提权重启 */
#define IDM_TOPMOST    2011    /* 工具条图钉: 窗口置顶 */
#define IDM_EXPORT     2012    /* Ctrl+S: 导出当前视图为 CSV */

/* 菜单栏「筛选」下的命令 */
#define IDM_AUTO       2020
#define IDM_LISTEN     2021
#define IDM_HIDESYS    2022
#define IDM_EXACT      2023   /* 搜索框按完整字段匹配，而不是包含匹配 */
#define IDM_LANG_EN    2040   /* 界面语言：英文 */
#define IDM_LANG_ZH    2041   /* 界面语言：中文 */
#define IDM_PROTO_ALL  2030
#define IDM_PROTO_TCP  2031
#define IDM_PROTO_UDP  2032
#define IDM_PROTO_V4   2033
#define IDM_PROTO_V6   2034

#define D_ED_PATH      3001
#define D_ED_CMD       3002
#define D_LIST_MOD     3003
#define D_BTN_LOC      3004
#define D_BTN_KILL     3005
#define D_BTN_CLOSE    3006
#define D_BTN_RELOAD   3007
#define D_ST_NAME      3010
#define D_ST_PATH      3011
#define D_ST_CMD       3012
#define D_ST_MOD       3013
#define D_ICO          3014    /* 进程图标 */

#define IDI_APPICON    101
#define REFRESH_MS     3000

#define WM_APP_REFRESH (WM_APP + 1)
#define WM_APP_KILLED  (WM_APP + 2)
#define WM_APP_PORTS   (WM_APP + 3)
#define WM_APP_DETAIL  (WM_APP + 4)
#define WM_APP_KILL    (WM_APP + 5)

#define FILTER_DEBOUNCE_MS 180

/*
 * 界面文案表。索引只用来定位，两条文案按 [TXT_x] 指定初始化写下标，
 * 增删条目不会互相错位；漏写的条目为 NULL，由 Tr 兜底成空串。
 */
enum {
    /* 菜单栏与「筛选」菜单 */
    TXT_FILTER = 0, TXT_LANGUAGE,
    TXT_AUTO, TXT_LISTEN, TXT_HIDE_SYSTEM, TXT_EXACT, TXT_CLEAR,
    TXT_PROTOCOL, TXT_PROTO_ALL, TXT_PROTO_TCP, TXT_PROTO_UDP, TXT_PROTO_V4, TXT_PROTO_V6,
    /* 查询区 */
    TXT_PORT, TXT_PID, TXT_PROCESS, TXT_KEYWORD, TXT_REFRESH,
    TXT_CUE_PORT, TXT_CUE_PID, TXT_CUE_NAME, TXT_CUE_KEY,
    /* 窗口标题与列表列头 */
    TXT_WINDOW, TXT_DETAIL_TITLE,
    TXT_COL_PROTO, TXT_COL_LADDR, TXT_COL_RADDR,
    TXT_COL_STATE, TXT_COL_PID, TXT_COL_NAME, TXT_COL_PATH,
    /* 连接状态 */
    TXT_STATE_CLOSED, TXT_STATE_LISTEN, TXT_STATE_SYN_SENT, TXT_STATE_SYN_RCVD,
    TXT_STATE_ESTAB, TXT_STATE_FIN1, TXT_STATE_FIN2, TXT_STATE_CLOSE_WAIT,
    TXT_STATE_CLOSING, TXT_STATE_LAST_ACK, TXT_STATE_TIME_WAIT, TXT_STATE_DELETED,
    TXT_STATE_UNKNOWN,
    /* 状态栏 */
    TXT_BAR_LOADING, TXT_BAR_FAILED, TXT_BAR_SUMMARY, TXT_BAR_PARTIAL, TXT_BAR_REFRESHING,
    TXT_BAR_KILLING, TXT_BAR_HINT,
    /* 顶栏工具条：菜单文案带助记符与快捷键后缀，按钮上不能用，单独一份纯文本 */
    TXT_TBAR_REFRESH, TXT_TBAR_AUTO, TXT_TBAR_LISTEN, TXT_TBAR_HIDESYS, TXT_TBAR_EXACT,
    TXT_TBAR_ADMIN, TXT_TBAR_ELEVATE, TXT_TBAR_FILTER, TXT_TBAR_LANG, TXT_TBAR_TOPMOST,
    /* 工具条悬停提示 */
    TXT_TIP_REFRESH, TXT_TIP_AUTO, TXT_TIP_LISTEN, TXT_TIP_HIDESYS, TXT_TIP_EXACT,
    TXT_TIP_TOPMOST, TXT_TIP_ADMIN, TXT_TIP_ELEVATE, TXT_TIP_FILTER, TXT_TIP_LANG,
    /* 行悬停信息与服务名、CSV 导出 */
    TXT_TIP_SRV, TXT_TIP_NOPATH,
    TXT_CTX_EXPORT, TXT_CSV_FILTER, TXT_CSV_FILTER_ALL, TXT_CSV_TITLE,
    TXT_MB_EXPORTED, TXT_MB_EXPORT_FAIL,
    TXT_COND_TCP, TXT_COND_UDP, TXT_COND_V4, TXT_COND_V6,
    TXT_COND_LISTEN, TXT_COND_HIDEP, TXT_COND_EXACT, TXT_COND_AUTO,
    /* 空结果提示 */
    TXT_EMPTY_LOADING, TXT_EMPTY_FAILED, TXT_EMPTY_NONE, TXT_EMPTY_HIDEP,
    TXT_EMPTY_EXACT, TXT_EMPTY_FILTER, TXT_EMPTY_NOMATCH,
    /* 右键菜单 */
    TXT_CTX_DETAIL, TXT_CTX_OPEN_LOC, TXT_CTX_KILL, TXT_CTX_KILL_TREE,
    TXT_CTX_COPY_PATH, TXT_CTX_COPY_PID, TXT_CTX_COPY_ROW, TXT_CTX_FILTER_SEL, TXT_CTX_REFRESH,
    /* 消息框 */
    TXT_MB_ELEVATE_BODY, TXT_MB_ELEVATE_TITLE, TXT_MB_ELEVATE_FAIL, TXT_MB_HINT,
    TXT_MB_KILL_BUSY, TXT_MB_WAIT, TXT_MB_OOM, TXT_MB_FAIL, TXT_MB_START_FAIL,
    TXT_MB_REUSED_BODY, TXT_MB_ABORTED, TXT_MB_PARTIAL_BODY, TXT_MB_PARTIAL_TITLE,
    TXT_MB_SELF_BODY, TXT_MB_KILL_ERR, TXT_MB_SYS_PROC, TXT_MB_CANNOT_KILL,
    TXT_MB_IDENTITY_BODY, TXT_MB_NOT_KILLED, TXT_MB_KILL_TITLE, TXT_MB_KILL_TREE_TITLE,
    TXT_MB_KILL_CONFIRM, TXT_MB_KILL_TREE_CONFIRM, TXT_MB_NO_LOCATION, TXT_MB_NO_PATH,
    TXT_MB_NO_MOD_LOC, TXT_MB_OPEN_LOC_FAIL, TXT_MB_NO_MODULES,
    /* 进程详情窗口 */
    TXT_D_PATH_LABEL, TXT_D_CMD_LABEL, TXT_D_MOD_LABEL,
    TXT_D_BTN_LOC, TXT_D_BTN_KILL, TXT_D_BTN_RELOAD, TXT_D_BTN_CLOSE,
    TXT_D_COL_MODULE, TXT_D_COL_PATH, TXT_D_COL_BASE, TXT_D_COL_SIZE,
    TXT_D_LOADING, TXT_D_UNAVAIL, TXT_D_STALE,
    TXT_COUNT
};

/* 1=英文，0=中文（默认）。启动时从注册表读回上次的选择 */
static int g_english = 0;

/*
 * 语言偏好放 HKCU\Software\PortView：单文件免安装的工具，
 * 用户切了语言后重开还是原样，不用每次进菜单。
 */
static void LoadLanguagePreference(void)
{
    HKEY key;
    DWORD type = 0, value = 0, size = sizeof(value);

    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\PortView", 0, KEY_READ, &key) != ERROR_SUCCESS)
        return;
    if (RegQueryValueExW(key, L"Language", NULL, &type, (BYTE *)&value, &size) == ERROR_SUCCESS &&
        type == REG_DWORD)
        g_english = value ? 1 : 0;
    RegCloseKey(key);
}

/*
 * 命令行初始筛选：PortView.exe --port 8080 --pid 1234 --name "nginx.exe"
 * --key "文本" --proto 0..4 --listen 0|1 --hidesys 0|1 --exact 0|1 --auto 0|1。
 * 提权重启时把当前筛选原样带给新实例，视图不断档；快捷方式/脚本也能直接拉起即筛。
 */
/* 提权/快捷方式传入的初始筛选：--port/--pid/--name/--key 与各开关 */
static WCHAR g_argPort[32], g_argPid[32], g_argName[128], g_argKey[256];
static int g_argProto = -1, g_argListen = -1, g_argHidesys = -1, g_argExact = -1, g_argAuto = -1;

static void ParseCommandLineArgs(void)
{
    int n = 0, i;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &n);

    if (!argv) return;
    for (i = 1; i + 1 < n; ++i) {
        if (!wcscmp(argv[i], L"--port")) {
            ++i; _snwprintf(g_argPort, 32, L"%s", argv[i]);
        } else if (!wcscmp(argv[i], L"--pid")) {
            ++i; _snwprintf(g_argPid, 32, L"%s", argv[i]);
        } else if (!wcscmp(argv[i], L"--name")) {
            ++i; _snwprintf(g_argName, 128, L"%s", argv[i]);
        } else if (!wcscmp(argv[i], L"--key")) {
            ++i; _snwprintf(g_argKey, 256, L"%s", argv[i]);
        } else if (!wcscmp(argv[i], L"--proto")) {
            ++i; g_argProto = _wtoi(argv[i]);
        } else if (!wcscmp(argv[i], L"--listen")) {
            ++i; g_argListen = _wtoi(argv[i]);
        } else if (!wcscmp(argv[i], L"--hidesys")) {
            ++i; g_argHidesys = _wtoi(argv[i]);
        } else if (!wcscmp(argv[i], L"--exact")) {
            ++i; g_argExact = _wtoi(argv[i]);
        } else if (!wcscmp(argv[i], L"--auto")) {
            ++i; g_argAuto = _wtoi(argv[i]);
        }
    }
    LocalFree(argv);
}

/* 参数值包引号并转义内部引号，按 CommandLineToArgvW 的解析规则 */
static void QuoteArg(WCHAR *out, size_t cch, const WCHAR *value)
{
    size_t i = 0;
    WCHAR *p = out;

    if (cch < 3) return;
    *p++ = L'"';
    for (; *value && p < out + cch - 3; ++value) {
        if (*value == L'"' && p < out + cch - 4) *p++ = L'\\';
        *p++ = *value;
    }
    *p++ = L'"';
    *p = 0;
    (void)i;
}

static void SaveLanguagePreference(void)
{
    HKEY key;
    DWORD value = g_english ? 1u : 0u;

    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\PortView", 0, NULL, 0, KEY_WRITE,
                        NULL, &key, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(key, L"Language", 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
        RegCloseKey(key);
    }
}

static const WCHAR *const TEXT_EN[TXT_COUNT] = {
    [TXT_FILTER] = L"&Filter", [TXT_LANGUAGE] = L"&Language",
    [TXT_AUTO] = L"&Auto Refresh\tCtrl+Shift+R", [TXT_LISTEN] = L"&Listening Only",
    [TXT_HIDE_SYSTEM] = L"&Hide System Ports", [TXT_EXACT] = L"&Exact Match",
    [TXT_CLEAR] = L"&Clear All Filters\tEsc", [TXT_PROTOCOL] = L"&Protocol",
    [TXT_PROTO_ALL] = L"&All Protocols", [TXT_PROTO_TCP] = L"&TCP Only",
    [TXT_PROTO_UDP] = L"&UDP Only", [TXT_PROTO_V4] = L"IPv&4 Only",
    [TXT_PROTO_V6] = L"IPv&6 Only",
    [TXT_PORT] = L"Port", [TXT_PID] = L"PID", [TXT_PROCESS] = L"Process",
    [TXT_KEYWORD] = L"Keyword", [TXT_REFRESH] = L"Refresh (F5)",
    [TXT_CUE_PORT] = L"80", [TXT_CUE_PID] = L"1234", [TXT_CUE_NAME] = L"nginx.exe",
    [TXT_CUE_KEY] = L"Address, path or state",
    [TXT_WINDOW] = L"PortView — Port Inspector", [TXT_DETAIL_TITLE] = L"Process Details",
    [TXT_COL_PROTO] = L"Protocol", [TXT_COL_LADDR] = L"Local Address",
    [TXT_COL_RADDR] = L"Remote Address", [TXT_COL_STATE] = L"State", [TXT_COL_PID] = L"PID",
    [TXT_COL_NAME] = L"Process", [TXT_COL_PATH] = L"Image Path",
    [TXT_STATE_CLOSED] = L"Closed", [TXT_STATE_LISTEN] = L"Listening",
    [TXT_STATE_SYN_SENT] = L"SYN Sent", [TXT_STATE_SYN_RCVD] = L"SYN Received",
    [TXT_STATE_ESTAB] = L"Established", [TXT_STATE_FIN1] = L"FIN Wait 1",
    [TXT_STATE_FIN2] = L"FIN Wait 2", [TXT_STATE_CLOSE_WAIT] = L"Close Wait",
    [TXT_STATE_CLOSING] = L"Closing", [TXT_STATE_LAST_ACK] = L"Last Ack",
    [TXT_STATE_TIME_WAIT] = L"Time Wait", [TXT_STATE_DELETED] = L"Deleted",
    [TXT_STATE_UNKNOWN] = L"Unknown",
    [TXT_BAR_LOADING] = L"Reading port tables… · %s",
    [TXT_BAR_FAILED] = L"Failed to read port tables · showing the previous result · %02d:%02d:%02d",
    [TXT_BAR_SUMMARY] = L"%s%s%u connections · %u listening · %u shown · %s%02d:%02d:%02d",
    [TXT_BAR_PARTIAL] = L"Partial results · ", [TXT_BAR_REFRESHING] = L"Refreshing · ",
    [TXT_BAR_KILLING] = L"Killing process…",
    [TXT_BAR_HINT] = L"F5 refresh · Esc clear · double-click for details",
    [TXT_TBAR_REFRESH] = L"Refresh", [TXT_TBAR_AUTO] = L"Auto Refresh",
    [TXT_TBAR_LISTEN] = L"Listening Only", [TXT_TBAR_HIDESYS] = L"Hide System Ports",
    [TXT_TBAR_EXACT] = L"Exact Match", [TXT_TBAR_ADMIN] = L"Admin",
    [TXT_TBAR_ELEVATE] = L"Elevate", [TXT_TBAR_FILTER] = L"Filter",
    [TXT_TBAR_LANG] = L"Language", [TXT_TBAR_TOPMOST] = L"On Top",
    [TXT_TIP_REFRESH] = L"Refresh the port list (F5)",
    [TXT_TIP_AUTO] = L"Refresh the port list every 3 seconds",
    [TXT_TIP_LISTEN] = L"Show listening ports only",
    [TXT_TIP_HIDESYS] = L"Hide ports held by key system processes",
    [TXT_TIP_EXACT] = L"Match whole fields when searching",
    [TXT_TIP_TOPMOST] = L"Keep the window above all others",
    [TXT_TIP_ADMIN] = L"Running with administrator privilege",
    [TXT_TIP_ELEVATE] = L"Restart as administrator to manage system processes",
    [TXT_TIP_FILTER] = L"Filter and protocol menu",
    [TXT_TIP_LANG] = L"Switch the interface language",
    [TXT_TIP_SRV] = L"Service: %s",
    [TXT_TIP_NOPATH] = L"(image path unavailable, may need administrator)",
    [TXT_CTX_EXPORT] = L"&Export as CSV...",
    [TXT_CSV_FILTER] = L"CSV files", [TXT_CSV_FILTER_ALL] = L"All files",
    [TXT_CSV_TITLE] = L"Export Port List",
    [TXT_MB_EXPORTED] = L"Exported %u connections:\n%s",
    [TXT_MB_EXPORT_FAIL] = L"Export failed: unable to write the file.",
    [TXT_COND_TCP] = L"TCP only · ", [TXT_COND_UDP] = L"UDP only · ",
    [TXT_COND_V4] = L"IPv4 only · ", [TXT_COND_V6] = L"IPv6 only · ",
    [TXT_COND_LISTEN] = L"Listening only · ", [TXT_COND_HIDEP] = L"System ports hidden · ",
    [TXT_COND_EXACT] = L"Exact match · ", [TXT_COND_AUTO] = L"Auto refresh · ",
    [TXT_EMPTY_LOADING] = L"Reading port tables…",
    [TXT_EMPTY_FAILED] = L"Failed to read the port tables\nShowing the last successful result",
    [TXT_EMPTY_NONE] = L"No ports are currently in use",
    [TXT_EMPTY_HIDEP] = L"No ports match the current conditions\n"
                        L"Ports held by key system processes are hidden\n"
                        L"Try turning off \"Hide System Ports\"",
    [TXT_EMPTY_EXACT] = L"No port, PID, process or address exactly matches what you typed\n"
                        L"Try turning off \"Exact Match\" to search for partial text",
    [TXT_EMPTY_FILTER] = L"No ports match the current filters\n"
                         L"Try turning off \"Listening Only\" or switching protocol",
    [TXT_EMPTY_NOMATCH] = L"No matching ports\nTry clearing the filter box",
    [TXT_CTX_DETAIL] = L"&View Process Details", [TXT_CTX_OPEN_LOC] = L"&Open File Location",
    [TXT_CTX_KILL] = L"&Kill Process", [TXT_CTX_KILL_TREE] = L"Kill Process &Tree",
    [TXT_CTX_COPY_PATH] = L"&Copy Image Path", [TXT_CTX_COPY_PID] = L"Copy &PID",
    [TXT_CTX_COPY_ROW] = L"Copy &Row", [TXT_CTX_FILTER_SEL] = L"Filter by This &Process",
    [TXT_CTX_REFRESH] = L"&Refresh\tF5",
    [TXT_MB_ELEVATE_BODY] = L"Running as administrator lets you inspect and end system-level processes.\nContinue?",
    [TXT_MB_ELEVATE_TITLE] = L"Elevation",
    [TXT_MB_ELEVATE_FAIL] = L"Elevation failed or was cancelled.",
    [TXT_MB_HINT] = L"Notice",
    [TXT_MB_KILL_BUSY] = L"The previous kill operation is still running. Please wait.",
    [TXT_MB_WAIT] = L"Please wait",
    [TXT_MB_OOM] = L"Not enough memory to start the kill operation.",
    [TXT_MB_FAIL] = L"Failed",
    [TXT_MB_START_FAIL] = L"Unable to start the kill operation.",
    [TXT_MB_REUSED_BODY] = L"That PID no longer belongs to the process you selected "
                           L"(it exited and the PID was reassigned).\n"
                           L"The kill was skipped so an unrelated process is not terminated by mistake.\n"
                           L"Check the process in the list and try again.",
    [TXT_MB_ABORTED] = L"Aborted",
    [TXT_MB_PARTIAL_BODY] = L"The process tree was handled, but some members were not killed: "
                            L"they may have exited or had their PID reused,\n"
                            L"or permission was insufficient — a process whose creation time "
                            L"cannot be read cannot be confirmed as part of the tree, so it was skipped.",
    [TXT_MB_PARTIAL_TITLE] = L"Partially done",
    [TXT_MB_SELF_BODY] = L"The scope includes this tool itself (you selected it, or it sits in that\n"
                         L"process tree — for example this tool was started from the command line\n"
                         L"you are killing).\n"
                         L"Nothing was run so the tool does not disappear mid-operation.",
    [TXT_MB_KILL_ERR] = L"Kill failed (error %u).\nIf the target is a system or another user's process, run this tool as administrator.",
    [TXT_MB_SYS_PROC] = L"This system process cannot be ended.",
    [TXT_MB_CANNOT_KILL] = L"Cannot kill",
    [TXT_MB_IDENTITY_BODY] = L"The identity of this process could not be confirmed: it may have exited,\n"
                             L"or permission was insufficient to read its start time.\n"
                             L"The operation was cancelled so a process that reused this PID is kept safe.",
    [TXT_MB_NOT_KILLED] = L"Not killed",
    [TXT_MB_KILL_TITLE] = L"Kill Process", [TXT_MB_KILL_TREE_TITLE] = L"Kill Process Tree",
    [TXT_MB_KILL_CONFIRM] = L"Kill process %s (PID %u)?\nUnsaved data will be lost.",
    [TXT_MB_KILL_TREE_CONFIRM] = L"Kill process %s (PID %u) and all of its child processes?\nUnsaved data will be lost.",
    [TXT_MB_NO_LOCATION] = L"Unable to open that location.",
    [TXT_MB_NO_PATH] = L"The image path of this process is unavailable (insufficient permission).",
    [TXT_MB_NO_MOD_LOC] = L"Unable to open that module's location.",
    [TXT_MB_OPEN_LOC_FAIL] = L"File location unavailable (process path unknown or permission denied).",
    [TXT_MB_NO_MODULES] = L"Unable to read the module list (needs higher permission, or the process has exited)",
    [TXT_D_PATH_LABEL] = L"Image path:", [TXT_D_CMD_LABEL] = L"Command line:",
    [TXT_D_MOD_LABEL] = L"Loaded modules (related files):",
    [TXT_D_BTN_LOC] = L"Open Location", [TXT_D_BTN_KILL] = L"Kill Process",
    [TXT_D_BTN_RELOAD] = L"Reload", [TXT_D_BTN_CLOSE] = L"Close",
    [TXT_D_COL_MODULE] = L"Module", [TXT_D_COL_PATH] = L"File Path",
    [TXT_D_COL_BASE] = L"Base", [TXT_D_COL_SIZE] = L"Size",
    [TXT_D_LOADING] = L"Reading…", [TXT_D_UNAVAIL] = L"(unavailable)",
    [TXT_D_STALE] = L"(the original process exited or its PID was reused; its data was not refreshed)",
};

static const WCHAR *const TEXT_ZH[TXT_COUNT] = {
    [TXT_FILTER] = L"筛选(&F)", [TXT_LANGUAGE] = L"语言(&L)",
    [TXT_AUTO] = L"自动刷新(&A)\tCtrl+Shift+R", [TXT_LISTEN] = L"仅监听端口(&L)",
    [TXT_HIDE_SYSTEM] = L"隐藏系统端口(&S)", [TXT_EXACT] = L"精确匹配(&X)",
    [TXT_CLEAR] = L"清除全部筛选(&C)\tEsc", [TXT_PROTOCOL] = L"协议(&P)",
    [TXT_PROTO_ALL] = L"全部协议(&A)", [TXT_PROTO_TCP] = L"仅 TCP(&T)",
    [TXT_PROTO_UDP] = L"仅 UDP(&U)", [TXT_PROTO_V4] = L"仅 IPv4(&4)",
    [TXT_PROTO_V6] = L"仅 IPv6(&6)",
    [TXT_PORT] = L"端口", [TXT_PID] = L"PID", [TXT_PROCESS] = L"进程名",
    [TXT_KEYWORD] = L"关键字", [TXT_REFRESH] = L"刷新 (F5)",
    [TXT_CUE_PORT] = L"80", [TXT_CUE_PID] = L"1234", [TXT_CUE_NAME] = L"nginx.exe",
    [TXT_CUE_KEY] = L"地址、路径或状态",
    [TXT_WINDOW] = L"端口占用查看器 — PortView", [TXT_DETAIL_TITLE] = L"进程详情",
    [TXT_COL_PROTO] = L"协议", [TXT_COL_LADDR] = L"本地地址",
    [TXT_COL_RADDR] = L"远程地址", [TXT_COL_STATE] = L"状态",
    [TXT_COL_PID] = L"PID", [TXT_COL_NAME] = L"进程", [TXT_COL_PATH] = L"映像路径",
    [TXT_STATE_CLOSED] = L"已关闭", [TXT_STATE_LISTEN] = L"监听",
    [TXT_STATE_SYN_SENT] = L"SYN 已发送", [TXT_STATE_SYN_RCVD] = L"SYN 已接收",
    [TXT_STATE_ESTAB] = L"已建立", [TXT_STATE_FIN1] = L"FIN 等待 1",
    [TXT_STATE_FIN2] = L"FIN 等待 2", [TXT_STATE_CLOSE_WAIT] = L"关闭等待",
    [TXT_STATE_CLOSING] = L"正在关闭", [TXT_STATE_LAST_ACK] = L"最后确认",
    [TXT_STATE_TIME_WAIT] = L"时间等待", [TXT_STATE_DELETED] = L"已删除",
    [TXT_STATE_UNKNOWN] = L"未知",
    [TXT_BAR_LOADING] = L"正在读取端口表… · %s",
    [TXT_BAR_FAILED] = L"读取端口表失败 · 显示的是上一次的结果 · %02d:%02d:%02d",
    [TXT_BAR_SUMMARY] = L"%s%s共 %u 条连接 · 监听端口 %u 个 · 显示 %u 条 · %s%02d:%02d:%02d",
    [TXT_BAR_PARTIAL] = L"部分结果 · ", [TXT_BAR_REFRESHING] = L"正在刷新 · ",
    [TXT_BAR_KILLING] = L"正在结束进程…",
    [TXT_BAR_HINT] = L"F5 刷新 · Esc 清除筛选 · 双击看详情",
    [TXT_TBAR_REFRESH] = L"刷新", [TXT_TBAR_AUTO] = L"自动刷新",
    [TXT_TBAR_LISTEN] = L"仅监听", [TXT_TBAR_HIDESYS] = L"隐藏系统端口",
    [TXT_TBAR_EXACT] = L"精确匹配", [TXT_TBAR_ADMIN] = L"管理员",
    [TXT_TBAR_ELEVATE] = L"提权", [TXT_TBAR_FILTER] = L"筛选",
    [TXT_TBAR_LANG] = L"语言", [TXT_TBAR_TOPMOST] = L"置顶",
    [TXT_TIP_REFRESH] = L"刷新端口列表（F5）",
    [TXT_TIP_AUTO] = L"每 3 秒自动刷新端口列表",
    [TXT_TIP_LISTEN] = L"只显示处于监听状态的端口",
    [TXT_TIP_HIDESYS] = L"隐藏系统关键进程占用的端口",
    [TXT_TIP_EXACT] = L"搜索时按完整字段匹配",
    [TXT_TIP_TOPMOST] = L"让窗口保持在所有窗口最前",
    [TXT_TIP_ADMIN] = L"当前以管理员权限运行",
    [TXT_TIP_ELEVATE] = L"以管理员身份重启，管理系统级进程",
    [TXT_TIP_FILTER] = L"筛选条件与协议菜单",
    [TXT_TIP_LANG] = L"切换界面语言",
    [TXT_TIP_SRV] = L"服务：%s",
    [TXT_TIP_NOPATH] = L"（映像路径不可用，可能需要管理员权限）",
    [TXT_CTX_EXPORT] = L"导出为 CSV(&E)...",
    [TXT_CSV_FILTER] = L"CSV 文件", [TXT_CSV_FILTER_ALL] = L"所有文件",
    [TXT_CSV_TITLE] = L"导出端口列表",
    [TXT_MB_EXPORTED] = L"已导出 %u 条连接：\n%s",
    [TXT_MB_EXPORT_FAIL] = L"导出失败：无法写入文件。",
    [TXT_COND_TCP] = L"仅 TCP · ", [TXT_COND_UDP] = L"仅 UDP · ",
    [TXT_COND_V4] = L"仅 IPv4 · ", [TXT_COND_V6] = L"仅 IPv6 · ",
    [TXT_COND_LISTEN] = L"仅监听 · ", [TXT_COND_HIDEP] = L"已隐藏系统端口 · ",
    [TXT_COND_EXACT] = L"精确匹配 · ", [TXT_COND_AUTO] = L"自动刷新 · ",
    [TXT_EMPTY_LOADING] = L"正在读取端口表…",
    [TXT_EMPTY_FAILED] = L"读取端口表失败\n显示的是上一次成功读取的结果",
    [TXT_EMPTY_NONE] = L"当前没有检测到端口占用",
    [TXT_EMPTY_HIDEP] = L"没有符合当前条件的端口\n当前已隐藏系统关键进程占用的端口\n试试取消「隐藏系统端口」",
    [TXT_EMPTY_EXACT] = L"没有与搜索内容完全相同的端口、PID、进程名或地址\n可取消「精确匹配」改回包含搜索",
    [TXT_EMPTY_FILTER] = L"没有符合当前筛选条件的端口\n试试取消「仅监听端口」或切换协议",
    [TXT_EMPTY_NOMATCH] = L"没有匹配的端口\n试试清空筛选框",
    [TXT_CTX_DETAIL] = L"查看进程详情(&D)", [TXT_CTX_OPEN_LOC] = L"打开文件所在位置(&O)",
    [TXT_CTX_KILL] = L"结束进程(&K)", [TXT_CTX_KILL_TREE] = L"结束进程树(&T)",
    [TXT_CTX_COPY_PATH] = L"复制映像路径(&C)", [TXT_CTX_COPY_PID] = L"复制 PID",
    [TXT_CTX_COPY_ROW] = L"复制整行", [TXT_CTX_FILTER_SEL] = L"按该进程名过滤(&F)",
    [TXT_CTX_REFRESH] = L"刷新(&R)\tF5",
    [TXT_MB_ELEVATE_BODY] = L"以管理员身份重启后可以查看并结束系统级进程。\n是否继续？",
    [TXT_MB_ELEVATE_TITLE] = L"提权",
    [TXT_MB_ELEVATE_FAIL] = L"提权失败或已被取消。",
    [TXT_MB_HINT] = L"提示",
    [TXT_MB_KILL_BUSY] = L"上一次结束操作还在进行，请稍候。",
    [TXT_MB_WAIT] = L"请稍候",
    [TXT_MB_OOM] = L"内存不足，无法开始结束操作。",
    [TXT_MB_FAIL] = L"失败",
    [TXT_MB_START_FAIL] = L"无法开始结束操作。",
    [TXT_MB_REUSED_BODY] = L"该 PID 已不是你选择的那个进程（原进程期间已退出，PID 被系统重新分配）。\n"
                           L"为避免误杀无关进程，本次没有执行结束操作。\n请确认列表上的进程后再试。",
    [TXT_MB_ABORTED] = L"已中止",
    [TXT_MB_PARTIAL_BODY] = L"进程树已处理，但有部分成员没有结束：它们可能已经退出、PID 已被复用，\n"
                            L"或权限不足——读不到创建时间的进程无法确认它是否属于这棵树，已跳过。",
    [TXT_MB_PARTIAL_TITLE] = L"部分完成",
    [TXT_MB_SELF_BODY] = L"要结束的范围里包含本工具自己（你选中的就是它，或者它在那棵进程树里，\n"
                         L"例如本工具是从你要结束的那个命令行启动的）。\n"
                         L"为避免操作进行到一半工具自己消失，本次没有执行。",
    [TXT_MB_KILL_ERR] = L"结束失败（错误 %u）。\n如果是系统或其他用户的进程，请以管理员身份运行本工具。",
    [TXT_MB_SYS_PROC] = L"该系统进程无法结束。",
    [TXT_MB_CANNOT_KILL] = L"无法结束",
    [TXT_MB_IDENTITY_BODY] = L"无法确认该进程的身份：它可能已经退出，也可能权限不足读不到它的启动时间。\n"
                             L"为避免误杀已被系统复用了同一 PID 的其它进程，本次操作已取消。",
    [TXT_MB_NOT_KILLED] = L"未能结束",
    [TXT_MB_KILL_TITLE] = L"结束进程", [TXT_MB_KILL_TREE_TITLE] = L"结束进程树",
    [TXT_MB_KILL_CONFIRM] = L"确定结束进程 %s（PID %u）吗？\n未保存的数据将丢失。",
    [TXT_MB_KILL_TREE_CONFIRM] = L"确定结束进程 %s（PID %u）及其所有子进程吗？\n未保存的数据将丢失。",
    [TXT_MB_NO_LOCATION] = L"无法打开该位置。",
    [TXT_MB_NO_PATH] = L"该进程的映像路径不可用（权限不足）。",
    [TXT_MB_NO_MOD_LOC] = L"无法打开该模块所在位置。",
    [TXT_MB_OPEN_LOC_FAIL] = L"该文件位置不可用（进程路径未知或权限不足）。",
    [TXT_MB_NO_MODULES] = L"无法读取模块列表（需要更高权限，或进程已退出）",
    [TXT_D_PATH_LABEL] = L"映像路径：", [TXT_D_CMD_LABEL] = L"命令行：",
    [TXT_D_MOD_LABEL] = L"已加载模块（关联文件）：",
    [TXT_D_BTN_LOC] = L"打开所在目录", [TXT_D_BTN_KILL] = L"结束进程",
    [TXT_D_BTN_RELOAD] = L"重新加载", [TXT_D_BTN_CLOSE] = L"关闭",
    [TXT_D_COL_MODULE] = L"模块", [TXT_D_COL_PATH] = L"文件路径",
    [TXT_D_COL_BASE] = L"基址", [TXT_D_COL_SIZE] = L"大小",
    [TXT_D_LOADING] = L"正在读取…", [TXT_D_UNAVAIL] = L"（不可用）",
    [TXT_D_STALE] = L"（原进程已退出或 PID 已被其它进程复用，未刷新它的数据）",
};

static const WCHAR *Tr(int id)
{
    const WCHAR *text;
    if (id < 0 || id >= TXT_COUNT) return L"";
    text = g_english ? TEXT_EN[id] : TEXT_ZH[id];
    return text ? text : L"";
}

/*
 * 地址与端口合并为一列（如 *:3306、192.168.2.176:52881）：端口查询场景里
 * 「地址:端口」本来就是一个心智单元，拆两列反而多一次目光跳转。
 * UDP 与监听行没有远程端点，远程列显示 —，不再留一截空白桩。
 */
enum {
    COL_PROTO = 0, COL_LADDR, COL_RADDR,
    COL_STATE, COL_PID, COL_NAME, COL_PATH, COL_COUNT
};

/*
 * 列宽按信息量分配，不平均分。合并后的地址列承载「地址:端口」，给足宽度；
 * 而映像路径是判断「谁占了端口」最该看的一列，不写死宽度，
 * 由 LayoutColumns 把窗口剩下的宽度全给它。
 */
static const int COL_WIDTHS[COL_COUNT] = {
    56, 168, 168, 96, 72, 150, 240
};

#define COL_PATH_MIN 180   /* 路径列最小宽度，窗口再窄也不低于此 */

/* 列头文案与 COL_* 一一对应，换语言时按列号取 */
static const int COL_TEXT[COL_COUNT] = {
    TXT_COL_PROTO, TXT_COL_LADDR, TXT_COL_RADDR, TXT_COL_STATE,
    TXT_COL_PID, TXT_COL_NAME, TXT_COL_PATH
};

static HINSTANCE g_hInst;
static HWND g_hwndMain;
static HWND g_hList;
static HWND g_hEdit;
static HWND g_hEditPort;
static HWND g_hEditPid;
static HWND g_hEditName;
static HWND g_hLblPort;
static HWND g_hLblPid;
static HWND g_hLblName;
static HWND g_hLblKey;

/*
 * 查询区输入框的描边。控件自己那圈框（主题的或 WS_BORDER 的）在调整文字区之后
 * 会露在描边里面，成了「框里还有一层框」，所以输入框不带任何边框样式，
 * 描边统一在 WM_PAINT 里等控件画完再补上。
 */
#define ACCENT               RGB(37, 99, 235)   /* 主色：焦点描边、切换态、排序箭头 */
#define ACCENT_SOFT          RGB(219, 231, 255) /* 主色浅底：切换按钮激活态 */
#define CARD_BORDER          RGB(226, 232, 240) /* 卡片描边与分隔线 */
#define LABEL_TEXT_RGB       RGB(75, 85, 99)    /* 查询区标签灰 */
#define QUERY_BORDER         RGB(203, 213, 225)
#define QUERY_BORDER_HOT     RGB(148, 163, 184)
#define QUERY_BORDER_FOCUS   ACCENT
#define QUERY_HOVER          1

static COLORREF QueryBorderColor(HWND hwnd)
{
    if (GetFocus() == hwnd) return QUERY_BORDER_FOCUS;
    if (GetWindowLongPtr(hwnd, GWLP_USERDATA) & QUERY_HOVER) return QUERY_BORDER_HOT;
    return QUERY_BORDER;
}

/*
 * 描边。文字区被 WM_NCCALCSIZE 收成居中的一条，控件只刷自己那一条客户区，
 * 上下留下的边会露出父窗口底色，看起来像「框里还有一层框」，
 * 所以非客户区重画时先把整块铺成输入框底色，再画描边。
 */
static void DrawQueryBorder(HWND hwnd, int fillGap)
{
    HDC hdc = GetWindowDC(hwnd);
    RECT wr, seg;
    HBRUSH brush;
    int w, h;

    if (!hdc) return;
    GetWindowRect(hwnd, &wr);
    w = wr.right - wr.left;
    h = wr.bottom - wr.top;

    if (fillGap) {
        seg.left = 0; seg.top = 0; seg.right = w; seg.bottom = h;
        FillRect(hdc, &seg, GetSysColorBrush(COLOR_WINDOW));
    }

    brush = CreateSolidBrush(QueryBorderColor(hwnd));
    seg.left = 0; seg.right = w; seg.top = 0; seg.bottom = 1;
    FillRect(hdc, &seg, brush);
    seg.top = h - 1; seg.bottom = h;
    FillRect(hdc, &seg, brush);
    seg.left = 0; seg.right = 1; seg.top = 0; seg.bottom = h;
    FillRect(hdc, &seg, brush);
    seg.left = w - 1; seg.right = w;
    FillRect(hdc, &seg, brush);
    DeleteObject(brush);
    ReleaseDC(hwnd, hdc);
}

/*
 * 焦点/悬停变化后要整窗重画：上下两条边落在被 WM_NCCALCSIZE 收窄出来的非客户区里，
 * 只失效客户区的话它们不会跟着换色。
 */
static void RepaintQueryBorder(HWND hwnd)
{
    RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_ERASE);
}

static LRESULT CALLBACK QueryEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                      UINT_PTR id, DWORD_PTR ref)
{
    /* 系统默认把示例文字贴在文字区顶部。按当前字体高度把文字区收成居中的一条。 */
    if (msg == WM_NCCALCSIZE && wp) {
        LRESULT result = DefSubclassProc(hwnd, msg, wp, lp);
        NCCALCSIZE_PARAMS *params = (NCCALCSIZE_PARAMS *)lp;
        HDC hdc = GetDC(hwnd);
        HFONT old = (HFONT)SelectObject(hdc, (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0));
        TEXTMETRICW tm;
        int height, extra;
        GetTextMetricsW(hdc, &tm);
        SelectObject(hdc, old);
        ReleaseDC(hwnd, hdc);
        height = params->rgrc[0].bottom - params->rgrc[0].top;
        extra = (height - (tm.tmHeight + tm.tmExternalLeading)) / 2;
        if (extra > 0) {
            params->rgrc[0].top += extra;
            params->rgrc[0].bottom -= extra;
        }
        (void)ref;
        return result;
    }
    /* 描边等控件把自己画完之后再补，保证始终盖在最上层（客户区已经刷白，不用再铺） */
    if (msg == WM_PAINT) {
        LRESULT result = DefSubclassProc(hwnd, msg, wp, lp);
        DrawQueryBorder(hwnd, 0);
        return result;
    }
    /* 上下两条边落在非客户区，这条路径要顺手把露底色的那几条边铺白 */
    if (msg == WM_NCPAINT) {
        DrawQueryBorder(hwnd, 1);
        return 0;
    }
    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS) {
        LRESULT result = DefSubclassProc(hwnd, msg, wp, lp);
        RepaintQueryBorder(hwnd);
        return result;
    }
    if (msg == WM_MOUSEMOVE && !(GetWindowLongPtr(hwnd, GWLP_USERDATA) & QUERY_HOVER)) {
        TRACKMOUSEEVENT tme;
        ZeroMemory(&tme, sizeof(tme));
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, QUERY_HOVER);
        RepaintQueryBorder(hwnd);
    } else if (msg == WM_MOUSELEAVE) {
        SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
        RepaintQueryBorder(hwnd);
    }
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, QueryEditProc, id);
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static void InitQueryEdit(HWND edit, UINT_PTR id)
{
    /* 主题的 EDIT 会在客户区里再画一圈边框，和上面那层 1px 描边叠成「框里有框」，
     * 所以这几个输入框不走主题，边框只由 QueryEditProc 画。 */
    SetWindowTheme(edit, L"", L"");
    SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 6));
    SetWindowSubclass(edit, QueryEditProc, id, 0);
}
static HWND g_hBtnClear;    /* 查询区清除按钮（自绘小方块） */
static HWND g_hStatus;
static HWND g_hToolbar;     /* 顶栏工具条：高频筛选开关 + 菜单入口 */
static HMENU g_hMenuBar;    /* 不再挂到窗口上，由工具条的「筛选/语言」按钮弹出 */
static HFONT g_hFont;
static HIMAGELIST g_hIcons; /* 进程图标；列表行高跟随图标高度 */

/* 进程图标按路径（无路径时按进程名）缓存：同一 exe 反复出现在多行，只提取一次 */
typedef struct {
    WCHAR key[MAX_PATH];
    int index;
} ICON_CACHE_ENTRY;
#define ICON_CACHE_MAX 160
static ICON_CACHE_ENTRY g_iconCache[ICON_CACHE_MAX];
static int g_iconCacheCount;

/* 顶栏的两个结构化筛选，与文本框是「与」的关系：文本框管模糊匹配，这两个管精确范围。
 * 工具条按钮与菜单命令都只翻这几面旗子，绘制与筛选统一从这里读。 */
static int g_protoFilter = 0;   /* 0=全部 1=仅TCP 2=仅UDP 3=仅IPv4 4=仅IPv6 */
static int g_listenOnly = 0;    /* 1=只看监听端口 */
static int g_hideSystem = 0;    /* 1=隐藏系统关键进程占用的端口 */
static int g_exactMatch = 0;    /* 1=搜索框按完整字段匹配 */
static BOOL g_autoOn = TRUE;    /* 自动刷新勾选框状态 */

/* ------------------------------------------------------------ 顶栏工具条 */

#define TOOLBAR_CLASS L"PortViewToolBar"
#define TOOLBAR_H     40   /* 96dpi 基准，实际高度走 S() */

/* 按钮顺序即布局顺序；ADMIN/FILTER/LANG 由布局函数靠右放 */
enum {
    TBB_REFRESH = 0, TBB_AUTO, TBB_LISTEN, TBB_HIDESYS, TBB_EXACT, TBB_PIN,
    TBB_ADMIN, TBB_FILTER, TBB_LANG, TBB_COUNT
};
/* 按钮为纯文字：切换态用底色区分，不再配图标，视觉更干净 */

/* 窗口置顶开关（工具条图钉按钮） */
static BOOL g_topmost = FALSE;

/* 不引 windowsx.h，鼠标坐标就地取：lParam 低/高字按有符号短整处理 */
#define TB_PT_X(lp) ((int)(short)LOWORD(lp))
#define TB_PT_Y(lp) ((int)(short)HIWORD(lp))

/* DPI 缩放在「基础工具」区才定义，工具条的绘制与布局先用到 */
static UINT GetDpiOf(HWND hwnd);
static int S(HWND hwnd, int v);

static RECT g_tbRect[TBB_COUNT];   /* 各按钮当前像素矩形，绘制与命中共用 */
static int g_tbHover = -1;
static int g_tbPressed = -1;
static HFONT g_tbFont;             /* 主窗口 DPICHANGED 重建字体后经 WM_SETFONT 同步进来 */
static HWND g_hTip;                /* 工具条悬停提示 */

/* 每个按钮的文字（按当前语言即时取） */
static int TbTextId(int btn)
{
    switch (btn) {
    case TBB_REFRESH: return TXT_TBAR_REFRESH;
    case TBB_AUTO:    return TXT_TBAR_AUTO;
    case TBB_LISTEN:  return TXT_TBAR_LISTEN;
    case TBB_HIDESYS: return TXT_TBAR_HIDESYS;
    case TBB_EXACT:   return TXT_TBAR_EXACT;
    case TBB_PIN:     return TXT_TBAR_TOPMOST;
    case TBB_ADMIN:   return ProcIsElevated() ? TXT_TBAR_ADMIN : TXT_TBAR_ELEVATE;
    case TBB_FILTER:  return TXT_TBAR_FILTER;
    case TBB_LANG:    return TXT_TBAR_LANG;
    }
    return TXT_TBAR_FILTER;
}

/* 悬停提示文案 */
static int TbTipId(int btn)
{
    switch (btn) {
    case TBB_REFRESH: return TXT_TIP_REFRESH;
    case TBB_AUTO:    return TXT_TIP_AUTO;
    case TBB_LISTEN:  return TXT_TIP_LISTEN;
    case TBB_HIDESYS: return TXT_TIP_HIDESYS;
    case TBB_EXACT:   return TXT_TIP_EXACT;
    case TBB_PIN:     return TXT_TIP_TOPMOST;
    case TBB_ADMIN:   return ProcIsElevated() ? TXT_TIP_ADMIN : TXT_TIP_ELEVATE;
    case TBB_FILTER:  return TXT_TIP_FILTER;
    case TBB_LANG:    return TXT_TIP_LANG;
    }
    return TXT_TIP_FILTER;
}

/* 切换类按钮的激活态跟全局筛选变量走；其余按钮没有激活态 */
static int TbToggled(int btn)
{
    switch (btn) {
    case TBB_AUTO:    return g_autoOn;
    case TBB_LISTEN:  return g_listenOnly;
    case TBB_HIDESYS: return g_hideSystem;
    case TBB_EXACT:   return g_exactMatch;
    case TBB_PIN:     return g_topmost;
    }
    return 0;
}

/* 按钮里的下拉小三角（筛选/语言） */
static void DrawDropDown(HWND hwnd, HDC hdc, int x, int y, COLORREF color)
{
    HGDIOBJ oldBrush, oldPen;
    POINT pts[3];
    COLORREF c = color;

    pts[0].x = x;            pts[0].y = y;
    pts[1].x = x + S(hwnd, 8); pts[1].y = y;
    pts[2].x = x + S(hwnd, 4); pts[2].y = y + S(hwnd, 4);
    oldBrush = SelectObject(hdc, CreateSolidBrush(c));
    oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
    Polygon(hdc, pts, 3);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
}

/* 圆角矩形底（无边框）：切换态/悬停态的按钮底色 */
static void FillRoundRect(HWND hwnd, HDC hdc, RECT rc, int radius, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    HGDIOBJ oldBrush, oldPen;
    HPEN pen = CreatePen(PS_NULL, 0, 0);

    oldBrush = SelectObject(hdc, brush);
    oldPen = SelectObject(hdc, pen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius * 2, radius * 2);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

/* ------------------------------------------------------------ 详情窗按钮 */

/*
 * 详情窗按钮自绘：白底圆角 + 浅描边，悬停浅灰；「结束进程」用红色描边与
 * 红字标记危险操作，悬停浅红。原来的系统凸起按钮和改造后的界面风格脱节。
 */
#define BTN_KILL_FG   RGB(201, 48, 44)
#define BTN_KILL_HOT  RGB(251, 236, 234)

static LRESULT CALLBACK DetailBtnProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                      UINT_PTR id, DWORD_PTR ref)
{
    switch (msg) {
    case WM_MOUSEMOVE:
        if (!(GetWindowLongPtr(hwnd, GWLP_USERDATA) & 1)) {
            TRACKMOUSEEVENT tme;
            ZeroMemory(&tme, sizeof(tme));
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, 1);
            InvalidateRect(hwnd, NULL, TRUE);
        }
        return 0;
    case WM_MOUSELEAVE:
        SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, DetailBtnProc, id);
        break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static void DrawDetailButton(DRAWITEMSTRUCT *dis, HFONT font)
{
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    int kill = (dis->CtlID == D_BTN_KILL);
    int hot = (int)GetWindowLongPtr(dis->hwndItem, GWLP_USERDATA) & 1;
    COLORREF fg = kill ? BTN_KILL_FG : RGB(31, 41, 55);
    COLORREF border = kill ? BTN_KILL_FG : CARD_BORDER;
    WCHAR text[128];
    HFONT oldFont;
    int oldBk;
    COLORREF oldText;
    HPEN pen;
    HGDIOBJ oldPen, oldBrush;

    if (hot) {
        HBRUSH b = CreateSolidBrush(kill ? BTN_KILL_HOT : RGB(233, 238, 246));
        FillRect(hdc, &rc, b);
        DeleteObject(b);
    } else {
        HBRUSH b = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(hdc, &rc, b);
        DeleteObject(b);
    }
    pen = CreatePen(PS_SOLID, 1, hot && kill ? BTN_KILL_FG : border);
    oldPen = SelectObject(hdc, pen);
    oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, rc.left, rc.top, rc.right - 1, rc.bottom - 1, S(dis->hwndItem, 12), S(dis->hwndItem, 12));
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    GetWindowTextW(dis->hwndItem, text, 128);
    oldFont = SelectObject(hdc, font ? font : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
    oldBk = SetBkMode(hdc, TRANSPARENT);
    oldText = SetTextColor(hdc, fg);
    DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SetTextColor(hdc, oldText);
    SetBkMode(hdc, oldBk);
    SelectObject(hdc, oldFont);
}

/*
 * 布局：左组依次排刷新与三个筛选开关；右组（提权/筛选/语言）从右边缘往左收。
 * 窗口窄到两组要重叠时，从左组尾部开始藏按钮（精确匹配先藏）——
 * 被藏的开关在「筛选」菜单里始终存在，功能不丢。
 */
static void LayoutToolbar(HWND hwnd)
{
    HDC hdc;
    HGDIOBJ oldFont;
    SIZE size;
    RECT rc;
    int i, w, h, pad, gap, btnH, x, right;
    int textW[TBB_COUNT];

    if (!g_hToolbar) return;
    GetClientRect(g_hToolbar, &rc);
    w = rc.right;
    h = rc.bottom;
    pad = S(hwnd, 10);
    gap = S(hwnd, 8);
    btnH = S(hwnd, 28);

    hdc = GetDC(hwnd);
    oldFont = SelectObject(hdc, g_tbFont);
    for (i = 0; i < TBB_COUNT; ++i) {
        const WCHAR *text = Tr(TbTextId(i));
        int t = 0;
        if (GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &size)) t = size.cx;
        /* 提权徽章有前置圆点；下拉按钮：文字 + 三角 */
        if (i == TBB_ADMIN) t += S(hwnd, 6) + S(hwnd, 5);
        if (i == TBB_FILTER || i == TBB_LANG) t += S(hwnd, 12);
        textW[i] = t + S(hwnd, 18);
    }
    SelectObject(hdc, oldFont);
    ReleaseDC(hwnd, hdc);

    /*
     * 左组五个按钮与右组（提权/筛选/语言）全部常驻：窗口最小宽度（见
     * WM_GETMINMAXINFO）就是按「两排按钮 + 一行式查询区」的需求定的，
     * 不做窄窗收纳——筛选开关藏在菜单里等于功能消失，不如把窗口锁宽。
     */
    x = pad;
    for (i = TBB_REFRESH; i <= TBB_PIN; ++i) {
        g_tbRect[i].left = x;
        g_tbRect[i].right = x + textW[i];
        g_tbRect[i].top = (h - btnH) / 2;
        g_tbRect[i].bottom = g_tbRect[i].top + btnH;
        x += textW[i] + gap;
    }
    right = w - pad;
    for (i = TBB_COUNT - 1; i >= TBB_ADMIN; --i) {
        g_tbRect[i].left = right - textW[i];
        g_tbRect[i].right = right;
        g_tbRect[i].top = (h - btnH) / 2;
        g_tbRect[i].bottom = g_tbRect[i].top + btnH;
        right -= textW[i] + gap;
    }
    /* 按钮矩形变了，悬停提示的热区跟着更新 */
    if (g_hTip) {
        TOOLINFOW ti;
        int k;

        for (k = 0; k < TBB_COUNT; ++k) {
            ZeroMemory(&ti, sizeof(ti));
            ti.cbSize = sizeof(ti);
            ti.hwnd = hwnd;
            ti.uId = (UINT_PTR)k;
            ti.rect = g_tbRect[k];
            SendMessageW(g_hTip, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti);
        }
    }
    InvalidateRect(g_hToolbar, NULL, TRUE);
}

static int TbHitTest(HWND hwnd, POINT pt)
{
    int i;
    for (i = TBB_REFRESH; i < TBB_COUNT; ++i) {
        if (PtInRect(&g_tbRect[i], pt)) return i;
    }
    return -1;
}

static void ToolbarPopup(int btn)
{
    HMENU target = NULL;
    POINT pt;

    if (!g_hToolbar || !g_hMenuBar) return;
    if (btn == TBB_FILTER) target = GetSubMenu(g_hMenuBar, 0);
    if (btn == TBB_LANG) target = GetSubMenu(g_hMenuBar, 1);
    if (!target) return;

    pt.x = g_tbRect[btn].left;
    pt.y = g_tbRect[btn].bottom + 2;
    MapWindowPoints(g_hToolbar, NULL, &pt, 1);
    TrackPopupMenu(target, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
                   pt.x, pt.y, 0, g_hwndMain, NULL);
}

/* 按钮动作全部折算成主窗口的 WM_COMMAND：状态翻转、菜单勾选、列表刷新都走原有分支 */
static void ToolbarActivate(HWND hwnd, int btn)
{
    UINT cmd;
    switch (btn) {
    case TBB_REFRESH: cmd = ID_BTN_REFRESH; break;
    case TBB_AUTO:    cmd = IDM_AUTO; break;
    case TBB_LISTEN:  cmd = IDM_LISTEN; break;
    case TBB_HIDESYS: cmd = IDM_HIDESYS; break;
    case TBB_EXACT:   cmd = IDM_EXACT; break;
    case TBB_PIN:     cmd = IDM_TOPMOST; break;
    case TBB_ADMIN:
        if (!ProcIsElevated()) SendMessageW(g_hwndMain, WM_COMMAND, IDM_ELEVATE, 0);
        return;
    case TBB_FILTER:
    case TBB_LANG:
        ToolbarPopup(btn);
        return;
    default:
        return;
    }
    SendMessageW(g_hwndMain, WM_COMMAND, cmd, 0);
}

static void DrawToolbarButton(HWND hwnd, HDC hdc, int btn)
{
    RECT rc = g_tbRect[btn];
    int toggled = TbToggled(btn);
    int hot = (g_tbHover == btn);
    int pressed = (g_tbPressed == btn && hot);
    COLORREF fg = RGB(31, 41, 55);
    const WCHAR *text = Tr(TbTextId(btn));
    SIZE size;
    HGDIOBJ oldFont;
    int oldBk;
    COLORREF oldText;
    int textLen = (int)wcslen(text);
    int x, y;

    /* 底色：切换态 > 按下 > 悬停 */
    if (btn == TBB_ADMIN) {
        if (ProcIsElevated()) {
            FillRoundRect(hwnd, hdc, rc, S(hwnd, 13), RGB(231, 244, 231));
            fg = RGB(16, 124, 16);
        } else {
            FillRoundRect(hwnd, hdc, rc, S(hwnd, 13),
                          hot ? RGB(226, 231, 239) : RGB(238, 241, 245));
            fg = RGB(75, 85, 99);
        }
    } else if (toggled) {
        FillRoundRect(hwnd, hdc, rc, S(hwnd, 6), pressed ? RGB(199, 217, 255) : ACCENT_SOFT);
        fg = ACCENT;
    } else if (pressed) {
        FillRoundRect(hwnd, hdc, rc, S(hwnd, 6), RGB(219, 226, 240));
    } else if (hot) {
        FillRoundRect(hwnd, hdc, rc, S(hwnd, 6), RGB(233, 238, 246));
    }

    oldFont = SelectObject(hdc, g_tbFont);
    GetTextExtentPoint32W(hdc, text, textLen, &size);
    oldBk = SetBkMode(hdc, TRANSPARENT);
    oldText = SetTextColor(hdc, fg);

    x = rc.left + S(hwnd, 10);
    y = (rc.top + rc.bottom - size.cy) / 2;
    if (btn == TBB_ADMIN) {
        /* 徽章前置小圆点，与列表里的状态徽章同一语言 */
        HGDIOBJ oldBrush, oldPen;
        HBRUSH brush = CreateSolidBrush(fg);
        oldBrush = SelectObject(hdc, brush);
        oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
        Ellipse(hdc, x, (rc.top + rc.bottom) / 2 - S(hwnd, 3),
                x + S(hwnd, 6), (rc.top + rc.bottom) / 2 + S(hwnd, 3));
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(brush);
        x += S(hwnd, 6) + S(hwnd, 5);
    }
    TextOutW(hdc, x, y, text, textLen);
    x += size.cx;
    if (btn == TBB_FILTER || btn == TBB_LANG) {
        DrawDropDown(hwnd, hdc, x + S(hwnd, 4),
                     (rc.top + rc.bottom) / 2 - S(hwnd, 2), fg);
    }

    SetTextColor(hdc, oldText);
    SetBkMode(hdc, oldBk);
    SelectObject(hdc, oldFont);
}

static LRESULT CALLBACK ToolbarProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        /* 悬停提示：每按钮注册一个 rect 工具，文案绘制时按语言即时取 */
        TOOLINFOW ti;
        int i;

        g_hTip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
                                 WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                                 0, 0, 0, 0, hwnd, NULL, g_hInst, NULL);
        if (!g_hTip) break;
        SetWindowPos(g_hTip, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        for (i = 0; i < TBB_COUNT; ++i) {
            ZeroMemory(&ti, sizeof(ti));
            ti.cbSize = sizeof(ti);
            ti.hwnd = hwnd;
            ti.uId = (UINT_PTR)i;
            ti.uFlags = TTF_SUBCLASS;
            ti.lpszText = LPSTR_TEXTCALLBACKW;
            SendMessageW(g_hTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        }
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        HBRUSH lineBrush;
        int i;

        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
        /* 底部 1px 分隔线，把工具条和下面的查询卡片分层 */
        lineBrush = CreateSolidBrush(CARD_BORDER);
        rc.top = rc.bottom - 1;
        FillRect(hdc, &rc, lineBrush);
        DeleteObject(lineBrush);

        for (i = TBB_REFRESH; i < TBB_COUNT; ++i) {
            DrawToolbarButton(hwnd, hdc, i);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: {
        POINT pt = { TB_PT_X(lp), TB_PT_Y(lp) };
        int hit = TbHitTest(hwnd, pt);
        if (hit != g_tbHover) {
            g_tbHover = hit;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        if (hit >= 0 && !(GetWindowLongPtr(hwnd, GWLP_USERDATA) & 1)) {
            TRACKMOUSEEVENT tme;
            ZeroMemory(&tme, sizeof(tme));
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, 1);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
        g_tbHover = -1;
        g_tbPressed = -1;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        POINT pt = { TB_PT_X(lp), TB_PT_Y(lp) };
        g_tbPressed = TbHitTest(hwnd, pt);
        SetCapture(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT pt = { TB_PT_X(lp), TB_PT_Y(lp) };
        int hit = TbHitTest(hwnd, pt);
        if (g_tbPressed >= 0 && hit == g_tbPressed) ToolbarActivate(hwnd, hit);
        g_tbPressed = -1;
        if (GetCapture() == hwnd) ReleaseCapture();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_CAPTURECHANGED:
        g_tbPressed = -1;
        return 0;
    case WM_SETFONT:
        /* 自注册类不会自动保存字体，WM_GETFONT 要能取回测量用 */
        g_tbFont = (HFONT)wp;
        LayoutToolbar(hwnd);
        return 0;
    case WM_GETFONT:
        return (LRESULT)g_tbFont;
    case WM_NOTIFY:
    {
        NMHDR *hdr = (NMHDR *)lp;
        if (hdr->code == TTN_GETDISPINFOW && hdr->hwndFrom == g_hTip) {
            NMTTDISPINFOW *tt = (NMTTDISPINFOW *)lp;
            tt->lpszText = (LPWSTR)Tr(TbTipId((int)tt->hdr.idFrom));
            return 0;
        }
        break;
    }
    case WM_SIZE:
        LayoutToolbar(hwnd);
        return 0;
    case WM_NCDESTROY:
        g_hToolbar = NULL;
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static BOOL RegisterToolbar(HINSTANCE hInst)
{
    WNDCLASSW wc;

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = ToolbarProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = TOOLBAR_CLASS;
    return RegisterClassW(&wc) != 0;
}

/* ------------------------------------------------------------ 查询区清除按钮 */

#define CLEARBTN_CLASS L"PortViewClearBtn"

/*
 * 查询卡片行尾的 ✕ 小按钮：点击等同 Esc 清除全部筛选。
 * 24px 见方的自绘方块，悬停浅灰圆角，叉号用两条线画，与工具条同一套笔触。
 */
static LRESULT CALLBACK ClearBtnProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        int hot = (int)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        int w, h, cx, cy, r;
        HPEN pen;
        HGDIOBJ oldPen, oldBrush;

        GetClientRect(hwnd, &rc);
        w = rc.right; h = rc.bottom;
        cx = w / 2; cy = h / 2;
        r = S(hwnd, 5);

        /* 非悬停也要铺白底：按钮不铺底会残留创建时的未初始化像素 */
        if (hot) FillRoundRect(hwnd, hdc, rc, S(hwnd, 6), RGB(226, 232, 240));
        else FillRect(hdc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));

        pen = CreatePen(PS_SOLID, 1, hot ? RGB(75, 85, 99) : RGB(154, 164, 178));

        pen = CreatePen(PS_SOLID, 1, hot ? RGB(75, 85, 99) : RGB(154, 164, 178));
        oldPen = SelectObject(hdc, pen);
        oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        MoveToEx(hdc, cx - r, cy - r, NULL);
        LineTo(hdc, cx + r, cy + r);
        MoveToEx(hdc, cx + r, cy - r, NULL);
        LineTo(hdc, cx - r, cy + r);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE:
        if (!(GetWindowLongPtr(hwnd, GWLP_USERDATA) & 1)) {
            TRACKMOUSEEVENT tme;
            ZeroMemory(&tme, sizeof(tme));
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, 1);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_MOUSELEAVE:
        SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_LBUTTONUP:
        SendMessageW(g_hwndMain, WM_COMMAND, IDM_CLEAR, 0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static BOOL RegisterClearBtn(HINSTANCE hInst)
{
    WNDCLASSW wc;

    ZeroMemory(&wc, sizeof(wc));
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = ClearBtnProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = CLEARBTN_CLASS;
    return RegisterClassW(&wc) != 0;
}

/* ------------------------------------------------------------ 基础工具 */

static PORT_ENTRY *g_all = NULL;
static size_t g_allCount = 0;
static PORT_ENTRY *g_view = NULL;
static size_t g_viewCount = 0;

/* 上一次 PortsEnumerate 是否一张端口表都没读出来 */
static int g_enumFailed = 0;
static unsigned g_tableMask = PORT_TABLE_ALL;
static BOOL g_portsLoading = FALSE;
static SYSTEMTIME g_lastSuccessTime;
static BOOL g_haveSuccessTime = FALSE;
static unsigned g_portsGeneration = 0;
static BOOL g_portsPending = FALSE;
static int g_colUserSized[COL_COUNT];
static int g_colSavedW[COL_COUNT];   /* 上次会话拖出的列宽（逻辑像素），-1=未记录 */
static volatile LONG g_portsBusy = 0;
static volatile LONG g_killBusy = 0;

static int g_sortCol = COL_LADDR;
static int g_sortAsc = 1;

/* ------------------------------------------------------------ 基础工具 */

static UINT GetDpiOf(HWND hwnd)
{
    typedef UINT (WINAPI *PFN)(HWND);
    static PFN pfn = NULL;
    static int init = 0;

    if (!init) {
        init = 1;
        pfn = (PFN)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    }
    if (pfn) return pfn(hwnd);

    {
        HDC hdc = GetDC(NULL);
        UINT dpi = 96;
        if (hdc) {
            dpi = (UINT)GetDeviceCaps(hdc, LOGPIXELSY);
            ReleaseDC(NULL, hdc);
        }
        return dpi;
    }
}

static int S(HWND hwnd, int v)
{
    return MulDiv(v, (int)GetDpiOf(hwnd), 96);
}

static HFONT CreateUIFont(UINT dpi)
{
    NONCLIENTMETRICSW ncm;
    LOGFONTW lf;
    typedef BOOL (WINAPI *PFN_SPID)(UINT, UINT, PVOID, UINT, UINT);
    PFN_SPID pSpiDpi;
    BOOL ok = FALSE;

    ZeroMemory(&ncm, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);

    pSpiDpi = (PFN_SPID)GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                       "SystemParametersInfoForDpi");
    if (pSpiDpi) {
        ok = pSpiDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi);
    }

    if (!ok) {
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
            ncm.lfMessageFont.lfHeight = MulDiv(ncm.lfMessageFont.lfHeight, (int)dpi, 96);
        } else {
            ZeroMemory(&ncm.lfMessageFont, sizeof(LOGFONTW));
            ncm.lfMessageFont.lfHeight = -MulDiv(12, (int)dpi, 96);
            wcscpy(ncm.lfMessageFont.lfFaceName, L"Segoe UI");
        }
    }

    lf = ncm.lfMessageFont;
    return CreateFontIndirectW(&lf);
}

static BOOL CALLBACK SetFontProc(HWND hwnd, LPARAM lp)
{
    SendMessage(hwnd, WM_SETFONT, (WPARAM)lp, TRUE);
    return TRUE;
}

/*
 * 窗口位置/尺寸与排序状态的记忆。退出时写入，启动时读回；
 * 位置记录的是还原态矩形，最大化单独记一个标志。
 */
static void SaveUiPrefs(void)
{
    WINDOWPLACEMENT wp;
    HKEY key;
    DWORD v;

    if (!g_hwndMain) return;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\PortView", 0, NULL, 0, KEY_WRITE,
                        NULL, &key, NULL) != ERROR_SUCCESS)
        return;

    wp.length = sizeof(wp);
    if (GetWindowPlacement(g_hwndMain, &wp)) {
        RegSetValueExW(key, L"WndLeft", 0, REG_DWORD,
                       (const BYTE *)&wp.rcNormalPosition.left, sizeof(DWORD));
        RegSetValueExW(key, L"WndTop", 0, REG_DWORD,
                       (const BYTE *)&wp.rcNormalPosition.top, sizeof(DWORD));
        RegSetValueExW(key, L"WndRight", 0, REG_DWORD,
                       (const BYTE *)&wp.rcNormalPosition.right, sizeof(DWORD));
        RegSetValueExW(key, L"WndBottom", 0, REG_DWORD,
                       (const BYTE *)&wp.rcNormalPosition.bottom, sizeof(DWORD));
        v = (wp.showCmd == SW_SHOWMAXIMIZED) ? 1u : 0u;
        RegSetValueExW(key, L"WndMaximized", 0, REG_DWORD, (const BYTE *)&v, sizeof(DWORD));
    }
    v = (DWORD)g_sortCol;
    RegSetValueExW(key, L"SortCol", 0, REG_DWORD, (const BYTE *)&v, sizeof(DWORD));
    v = g_sortAsc ? 1u : 0u;
    RegSetValueExW(key, L"SortAsc", 0, REG_DWORD, (const BYTE *)&v, sizeof(DWORD));
    v = g_topmost ? 1u : 0u;
    RegSetValueExW(key, L"Topmost", 0, REG_DWORD, (const BYTE *)&v, sizeof(DWORD));

    /*
     * 列宽只在用户拖过时记录（存逻辑像素，跨 DPI 恢复不失真），
     * 并写一份列数校验值——将来列结构变化时旧宽整组作废。
     */
    {
        UINT dpi = GetDpiOf(g_hwndMain);
        WCHAR name[16];
        int i;

        v = (DWORD)COL_COUNT;
        RegSetValueExW(key, L"ColCount", 0, REG_DWORD, (const BYTE *)&v, sizeof(DWORD));
        for (i = 0; i < COL_COUNT; ++i) {
            _snwprintf(name, 16, L"ColW%d", i);
            v = g_colUserSized[i]
                    ? (DWORD)MulDiv(ListView_GetColumnWidth(g_hList, i), 96, (int)dpi)
                    : (DWORD)-1;
            RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE *)&v, sizeof(DWORD));
        }
    }
    RegCloseKey(key);
}

/* 启动时读回。返回 TRUE 表示 rc 里有可用的还原矩形；排序直接写全局并夹在合法范围 */
static BOOL LoadUiPrefs(RECT *rc, BOOL *maximized)
{
    HKEY key;
    DWORD v, cb;
    RECT r;
    BOOL haveRect = TRUE;

    *maximized = FALSE;
    SetRectEmpty(rc);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\PortView", 0, KEY_READ, &key) != ERROR_SUCCESS)
        return FALSE;

    cb = sizeof(v);
    if (RegQueryValueExW(key, L"SortCol", NULL, NULL, (BYTE *)&v, &cb) == ERROR_SUCCESS &&
        v < COL_COUNT) {
        g_sortCol = (int)v;
    }
    cb = sizeof(v);
    if (RegQueryValueExW(key, L"SortAsc", NULL, NULL, (BYTE *)&v, &cb) == ERROR_SUCCESS) {
        g_sortAsc = v ? 1 : 0;
    }
    cb = sizeof(v);
    if (RegQueryValueExW(key, L"Topmost", NULL, NULL, (BYTE *)&v, &cb) == ERROR_SUCCESS) {
        g_topmost = v ? TRUE : FALSE;
    }

    /* 列宽带列数校验：列结构对不上时整组作废，全部走默认宽 */
    {
        WCHAR name[16];
        int i;
        BOOL colsOk;

        cb = sizeof(v);
        colsOk = RegQueryValueExW(key, L"ColCount", NULL, NULL, (BYTE *)&v, &cb) == ERROR_SUCCESS &&
                 v == (DWORD)COL_COUNT;
        for (i = 0; i < COL_COUNT; ++i) {
            g_colSavedW[i] = -1;
            if (colsOk) {
                _snwprintf(name, 16, L"ColW%d", i);
                cb = sizeof(v);
                if (RegQueryValueExW(key, name, NULL, NULL, (BYTE *)&v, &cb) == ERROR_SUCCESS &&
                    v >= 40 && v <= 4000) {
                    g_colSavedW[i] = (int)v;
                }
            }
        }
    }

    cb = sizeof(r.left);
    haveRect &= RegQueryValueExW(key, L"WndLeft", NULL, NULL, (BYTE *)&r.left, &cb) == ERROR_SUCCESS;
    haveRect &= RegQueryValueExW(key, L"WndTop", NULL, NULL, (BYTE *)&r.top, &cb) == ERROR_SUCCESS;
    haveRect &= RegQueryValueExW(key, L"WndRight", NULL, NULL, (BYTE *)&r.right, &cb) == ERROR_SUCCESS;
    haveRect &= RegQueryValueExW(key, L"WndBottom", NULL, NULL, (BYTE *)&r.bottom, &cb) == ERROR_SUCCESS;
    cb = sizeof(v);
    if (RegQueryValueExW(key, L"WndMaximized", NULL, NULL, (BYTE *)&v, &cb) == ERROR_SUCCESS)
        *maximized = v ? TRUE : FALSE;
    RegCloseKey(key);

    if (haveRect && r.right > r.left && r.bottom > r.top &&
        r.right - r.left >= GetSystemMetrics(SM_CXMINTRACK) &&
        r.bottom - r.top >= GetSystemMetrics(SM_CYMINTRACK)) {
        *rc = r;
        return TRUE;
    }
    return FALSE;
}

static void CopyText(HWND hwnd, const WCHAR *text)
{
    HGLOBAL h;
    WCHAR *p;
    size_t n;

    if (!text || !*text) return;
    if (!OpenClipboard(hwnd)) return;

    EmptyClipboard();
    n = wcslen(text) + 1;
    h = GlobalAlloc(GMEM_MOVEABLE, n * sizeof(WCHAR));
    if (h) {
        p = (WCHAR *)GlobalLock(h);
        if (p) {
            memcpy(p, text, n * sizeof(WCHAR));
            GlobalUnlock(h);
            SetClipboardData(CF_UNICODETEXT, h);
        } else {
            GlobalFree(h);
        }
    }
    CloseClipboard();
}

static const WCHAR *PathBaseName(const WCHAR *path)
{
    const WCHAR *p = path;
    const WCHAR *q;
    for (q = path; *q; ++q) {
        if (*q == L'\\' || *q == L'/') p = q + 1;
    }
    return p;
}

static BOOL ContainsI(const WCHAR *hay, const WCHAR *needle)
{
    size_t len;
    if (!needle || !*needle) return TRUE;
    len = wcslen(needle);
    for (; *hay; ++hay) {
        if (_wcsnicmp(hay, needle, len) == 0) return TRUE;
    }
    return FALSE;
}

static BOOL EqualsI(const WCHAR *value, const WCHAR *key)
{
    return value && key && _wcsicmp(value, key) == 0;
}

static void ReadQuery(HWND edit, WCHAR *buf, size_t cch)
{
    if (!buf || cch == 0) return;
    buf[0] = 0;
    if (!edit) return;
    GetWindowTextW(edit, buf, (int)cch);
    buf[cch - 1] = 0;
}

static BOOL MatchField(const WCHAR *value, const WCHAR *key)
{
    if (!key || !key[0]) return TRUE;
    return g_exactMatch ? EqualsI(value, key) : ContainsI(value, key);
}

/* -------------------------------------------------------------- 数据 */

/* 直接比较关键字段来定位同一条连接，避免每行都格式化出一个 key 字符串 */
static int SameEntry(const PORT_ENTRY *a, const PORT_ENTRY *b)
{
    return a->pid == b->pid &&
           a->localPort == b->localPort &&
           a->remotePort == b->remotePort &&
           _wcsicmp(a->proto, b->proto) == 0 &&
           _wcsicmp(a->localAddr, b->localAddr) == 0 &&
           _wcsicmp(a->remoteAddr, b->remoteAddr) == 0;
}

/*
 * 通配地址缩写：0.0.0.0 与 :: 表示「本机所有网卡」，占满一整列却几乎没有信息量。
 * 缩成 * 与 netstat、TCPView 的习惯一致，也让出的宽度能给映像路径。
 * 只影响显示，MatchFilter 仍用原始地址匹配，按 IP 搜索照常可用。
 */
static const WCHAR *ShortAddr(const WCHAR *addr)
{
    if (_wcsicmp(addr, L"0.0.0.0") == 0 || _wcsicmp(addr, L"::") == 0)
        return L"*";
    return addr;
}

/*
 * 连接状态文字。ports.c 只带内核原值，文案在这里按界面语言取，
 * 切换语言后重绘列表即可，不必重新枚举端口表。
 */
static const WCHAR *StateText(DWORD code)
{
    switch (code) {
    case MIB_TCP_STATE_CLOSED:     return Tr(TXT_STATE_CLOSED);
    case MIB_TCP_STATE_LISTEN:     return Tr(TXT_STATE_LISTEN);
    case MIB_TCP_STATE_SYN_SENT:   return Tr(TXT_STATE_SYN_SENT);
    case MIB_TCP_STATE_SYN_RCVD:   return Tr(TXT_STATE_SYN_RCVD);
    case MIB_TCP_STATE_ESTAB:      return Tr(TXT_STATE_ESTAB);
    case MIB_TCP_STATE_FIN_WAIT1:  return Tr(TXT_STATE_FIN1);
    case MIB_TCP_STATE_FIN_WAIT2:  return Tr(TXT_STATE_FIN2);
    case MIB_TCP_STATE_CLOSE_WAIT: return Tr(TXT_STATE_CLOSE_WAIT);
    case MIB_TCP_STATE_CLOSING:    return Tr(TXT_STATE_CLOSING);
    case MIB_TCP_STATE_LAST_ACK:   return Tr(TXT_STATE_LAST_ACK);
    case MIB_TCP_STATE_TIME_WAIT:  return Tr(TXT_STATE_TIME_WAIT);
    case MIB_TCP_STATE_DELETE_TCB: return Tr(TXT_STATE_DELETED);
    default:                       return Tr(TXT_STATE_UNKNOWN);
    }
}

static const WCHAR *EntryStateText(const PORT_ENTRY *e)
{
    if (!e->stateCode) return L"";   /* UDP 没有连接状态 */
    return StateText(e->stateCode);
}

/* 「地址:端口」一次格式化成显示文本 */
static const WCHAR *AddrPortText(WCHAR *buf, size_t cch, const WCHAR *addr, DWORD port)
{
    _snwprintf(buf, (int)cch, L"%s:%u", ShortAddr(addr), port);
    buf[cch - 1] = 0;
    return buf;
}

/* LVS_OWNERDATA 下按需提供单元格文本，文本在 ports.c 枚举时已格式化好 */
static const WCHAR *CellText(const PORT_ENTRY *e, int col)
{
    static WCHAR lbuf[80], rbuf[80];   /* DISPINFO 取走文本立即拷贝，两块缓冲互不踩 */

    switch (col) {
    case COL_PROTO: return e->proto;
    case COL_LADDR: return AddrPortText(lbuf, 80, e->localAddr, e->localPort);
    case COL_RADDR: return e->remoteAddr[0] ? AddrPortText(rbuf, 80, e->remoteAddr, e->remotePort)
                                            : L"—";
    case COL_STATE: return EntryStateText(e);
    case COL_PID:   return e->pidText;
    case COL_NAME:  return e->procName;
    case COL_PATH:  return e->procPath;
    default:        return L"";
    }
}

/* 状态徽章配色（前景字 + 浅底）：监听绿、已建立蓝、终态灰、中间态橙。
 * 按内核原值判断，与界面语言无关；UDP 行没有状态，不画徽章。 */
typedef struct {
    COLORREF fg;
    COLORREF bg;
} STATE_BADGE_COLORS;

static STATE_BADGE_COLORS StateBadgeColors(const PORT_ENTRY *e)
{
    STATE_BADGE_COLORS c;
    switch (e->stateCode) {
    case MIB_TCP_STATE_LISTEN:    c.fg = RGB(16, 124, 16);   c.bg = RGB(231, 244, 231); break;
    case MIB_TCP_STATE_ESTAB:     c.fg = RGB(11, 98, 196);   c.bg = RGB(228, 240, 252); break;
    case MIB_TCP_STATE_TIME_WAIT:
    case MIB_TCP_STATE_CLOSED:    c.fg = RGB(107, 114, 128); c.bg = RGB(238, 241, 245); break;
    default:                      c.fg = RGB(180, 83, 9);    c.bg = RGB(253, 241, 222); break;
    }
    return c;
}

/*
 * 系统关键进程名单。列在这里的进程一旦被结束，轻则服务失效、重则蓝屏，
 * 而它们的端口又大多由系统自己占用，排查时几乎不会真去动它，所以提供开关整体屏蔽。
 *
 * svchost 是特例：它同时托管 DNS 客户端、DHCP、事件日志、Windows 更新等一堆服务，
 * 屏蔽后这些端口会一起消失。开关默认关闭，且 Esc 可一键复位，就是为了不让人
 * 在需要查这些端口时找不到回来的路。
 */
static const WCHAR *const SYSTEM_OWNERS[] = {
    L"csrss.exe", L"wininit.exe", L"winlogon.exe", L"services.exe",
    L"lsass.exe", L"lsm.exe", L"smss.exe", L"svchost.exe",
    L"dwm.exe", L"spoolsv.exe", L"fontdrvhost.exe",
    /* 内核态伪进程：没有 exe，按 Toolhelp 报出的名字匹配 */
    L"Registry", L"Memory Compression", L"Secure System", L"System"
};

static BOOL IsSystemOwner(const PORT_ENTRY *e)
{
    size_t i;
    size_t n = sizeof(SYSTEM_OWNERS) / sizeof(SYSTEM_OWNERS[0]);

    /* PID 0 / 4 是内核本体，名字可能被本地化，认 PID 更稳 */
    if (e->pid == 0 || e->pid == 4) return TRUE;

    for (i = 0; i < n; ++i) {
        if (_wcsicmp(e->procName, SYSTEM_OWNERS[i]) == 0) return TRUE;
    }
    return FALSE;
}

/*
 * 协议范围筛选。协议名与地址列的 TCP6/UDP6 前后缀已经区分了 IPv4/IPv6，
 * 所以这里直接按字符串判断，不再依赖 ports.c 暴露额外标志位。
 */
static BOOL MatchProto(const PORT_ENTRY *e, int mode)
{
    BOOL isTcp = (_wcsicmp(e->proto, L"TCP") == 0 || _wcsicmp(e->proto, L"TCP6") == 0);
    BOOL isV6 = (_wcsicmp(e->proto, L"TCP6") == 0 || _wcsicmp(e->proto, L"UDP6") == 0);

    switch (mode) {
    case 1: return isTcp;
    case 2: return !isTcp;
    case 3: return !isV6;
    case 4: return isV6;
    default: return TRUE;
    }
}

/* 逐字段匹配，避免为每行拼一个临时大字符串。
 * 精确模式只接受完整字段：搜索 80 命中端口 80，不命中 8000、8080 或路径中的 80。 */
static BOOL MatchFilter(const PORT_ENTRY *e, const WCHAR *key)
{
    if (!key || !key[0]) return TRUE;

    if (g_exactMatch) {
        return EqualsI(e->proto, key) ||
               EqualsI(e->localAddr, key) ||
               EqualsI(e->portText, key) ||
               EqualsI(e->remoteAddr, key) ||
               EqualsI(e->rportText, key) ||
               EqualsI(EntryStateText(e), key) ||
               EqualsI(e->pidText, key) ||
               EqualsI(e->procName, key) ||
               EqualsI(PathBaseName(e->procPath), key);
    }

    return ContainsI(e->proto, key) ||
           ContainsI(e->localAddr, key) ||
           ContainsI(e->portText, key) ||
           ContainsI(e->remoteAddr, key) ||
           ContainsI(e->rportText, key) ||
           ContainsI(EntryStateText(e), key) ||
           ContainsI(e->pidText, key) ||
           ContainsI(e->procName, key) ||
           ContainsI(e->procPath, key);
}

static int ParseIpv4(const WCHAR *text, unsigned char out[4])
{
    unsigned values[4];
    const WCHAR *p = text;
    int i;

    for (i = 0; i < 4; ++i) {
        unsigned value = 0;
        int digits = 0;
        if (*p < L'0' || *p > L'9') return 0;
        while (*p >= L'0' && *p <= L'9') {
            value = value * 10u + (unsigned)(*p - L'0');
            if (value > 255u || ++digits > 3) return 0;
            ++p;
        }
        if (i < 3) {
            if (*p != L'.') return 0;
            ++p;
        }
        values[i] = value;
    }
    if (*p) return 0;
    for (i = 0; i < 4; ++i) out[i] = (unsigned char)values[i];
    return 1;
}

static int ParseIpv6(const WCHAR *text, unsigned char out[16], DWORD *scope)
{
    unsigned short groups[8] = {0};
    const WCHAR *p = text;
    int count = 0, compress = -1, i;
    unsigned long parsedScope;

    *scope = 0;
    if (*p == L':') {
        if (p[1] != L':') return 0;
        compress = 0;
        p += 2;
    }

    while (*p && *p != L'%') {
        const WCHAR *hex = p;
        unsigned value = 0;
        int digits = 0;

        if (*p == L':') {
            if (compress >= 0) return 0;
            compress = count;
            ++p;
            continue;
        }
        while ((*p >= L'0' && *p <= L'9') ||
               (*p >= L'a' && *p <= L'f') ||
               (*p >= L'A' && *p <= L'F')) {
            unsigned digit = (*p <= L'9') ? (unsigned)(*p - L'0')
                           : (*p <= L'F') ? (unsigned)(*p - L'A' + 10)
                                          : (unsigned)(*p - L'a' + 10);
            value = (value << 4) | digit;
            if (value > 0xFFFFu || ++digits > 4) return 0;
            ++p;
        }
        if (digits == 0 || count >= 8) return 0;
        if (*p == L'.' && count <= 6) {
            unsigned char v4[4];
            if (!ParseIpv4(hex, v4)) return 0;
            groups[count++] = (unsigned short)((v4[0] << 8) | v4[1]);
            groups[count++] = (unsigned short)((v4[2] << 8) | v4[3]);
            p = hex + wcslen(hex);
            while (*p && *p != L'%') ++p;
            break;
        }
        groups[count++] = (unsigned short)value;
        if (*p == L':') ++p;
        else if (*p && *p != L'%') return 0;
    }

    if (*p == L'%') {
        ++p;
        if (!*p) return 0;
        parsedScope = wcstoul(p, (WCHAR **)&p, 10);
        if (*p || parsedScope > 0xFFFFFFFFul) return 0;
        *scope = (DWORD)parsedScope;
    }
    if (compress < 0) {
        if (count != 8) return 0;
    } else {
        int tail = count - compress;
        int zeros = 8 - count;
        if (zeros <= 0 || tail < 0) return 0;
        for (i = 7; tail > 0; --i, --tail) groups[i] = groups[compress + tail - 1];
        for (i = 0; i < zeros; ++i) groups[compress + i] = 0;
    }
    for (i = 0; i < 8; ++i) {
        out[i * 2] = (unsigned char)(groups[i] >> 8);
        out[i * 2 + 1] = (unsigned char)groups[i];
    }
    return 1;
}

static int CmpAddress(const WCHAR *a, const WCHAR *b)
{
    unsigned char aa[16], bb[16];
    DWORD scopeA = 0, scopeB = 0;
    int kindA = 0, kindB = 0, i;

    if (!a[0] && !b[0]) return 0;
    if (!a[0]) return -1;
    if (!b[0]) return 1;
    if (ParseIpv4(a, aa)) kindA = 1;
    else if (ParseIpv6(a, aa, &scopeA)) kindA = 2;
    if (ParseIpv4(b, bb)) kindB = 1;
    else if (ParseIpv6(b, bb, &scopeB)) kindB = 2;
    if (kindA != kindB) {
        if (!kindA) return 1;
        if (!kindB) return -1;
        return kindA - kindB;
    }
    if (!kindA) return _wcsicmp(a, b);
    for (i = 0; i < (kindA == 1 ? 4 : 16); ++i) {
        if (aa[i] != bb[i]) return (int)aa[i] - (int)bb[i];
    }
    if (scopeA < scopeB) return -1;
    if (scopeA > scopeB) return 1;
    return 0;
}

static int CmpEntry(const void *pa, const void *pb)
{
    const PORT_ENTRY *a = (const PORT_ENTRY *)pa;
    const PORT_ENTRY *b = (const PORT_ENTRY *)pb;
    int r = 0;

    switch (g_sortCol) {
    case COL_PROTO: r = _wcsicmp(a->proto, b->proto); break;
    case COL_LADDR:
        /* 合并列按地址排，同地址再按端口，与「地址:端口」的阅读顺序一致 */
        r = CmpAddress(a->localAddr, b->localAddr);
        if (r == 0) r = (int)a->localPort - (int)b->localPort;
        break;
    case COL_RADDR:
        r = CmpAddress(a->remoteAddr, b->remoteAddr);
        if (r == 0) r = (int)a->remotePort - (int)b->remotePort;
        break;
    case COL_STATE: r = _wcsicmp(EntryStateText(a), EntryStateText(b)); break;
    case COL_PID:   r = (int)a->pid - (int)b->pid; break;
    case COL_NAME:  r = _wcsicmp(a->procName, b->procName); break;
    case COL_PATH:  r = _wcsicmp(a->procPath, b->procPath); break;
    default: break;
    }

    if (r == 0) r = (int)a->localPort - (int)b->localPort;
    if (r == 0) r = (int)a->remotePort - (int)b->remotePort;
    if (r == 0) r = _wcsicmp(a->proto, b->proto);
    if (r == 0) r = (int)a->pid - (int)b->pid;
    return g_sortAsc ? r : -r;
}

/*
 * 筛选菜单。不挂到窗口菜单栏上（工具条已接管高频开关），作为「筛选 ▾」按钮的
 * 下拉内容保留：协议子菜单、清除全部这类低频动作放这里，勾选态照常维护。
 */
static HMENU BuildMainMenu(void)
{
    HMENU bar, filter, proto, lang;

    filter = CreatePopupMenu();
    AppendMenuW(filter, MF_STRING, IDM_AUTO, Tr(TXT_AUTO));
    AppendMenuW(filter, MF_STRING, IDM_LISTEN, Tr(TXT_LISTEN));
    AppendMenuW(filter, MF_STRING, IDM_HIDESYS, Tr(TXT_HIDE_SYSTEM));
    AppendMenuW(filter, MF_STRING, IDM_EXACT, Tr(TXT_EXACT));
    AppendMenuW(filter, MF_SEPARATOR, 0, NULL);
    AppendMenuW(filter, MF_STRING, IDM_CLEAR, Tr(TXT_CLEAR));

    proto = CreatePopupMenu();
    AppendMenuW(proto, MF_STRING, IDM_PROTO_ALL, Tr(TXT_PROTO_ALL));
    AppendMenuW(proto, MF_STRING, IDM_PROTO_TCP, Tr(TXT_PROTO_TCP));
    AppendMenuW(proto, MF_STRING, IDM_PROTO_UDP, Tr(TXT_PROTO_UDP));
    AppendMenuW(proto, MF_STRING, IDM_PROTO_V4,  Tr(TXT_PROTO_V4));
    AppendMenuW(proto, MF_STRING, IDM_PROTO_V6,  Tr(TXT_PROTO_V6));
    AppendMenuW(filter, MF_POPUP, (UINT_PTR)proto, Tr(TXT_PROTOCOL));

    lang = CreatePopupMenu();
    AppendMenuW(lang, MF_STRING, IDM_LANG_EN, L"English");
    AppendMenuW(lang, MF_STRING, IDM_LANG_ZH, L"中文");

    bar = CreateMenu();
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)filter, Tr(TXT_FILTER));
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)lang, Tr(TXT_LANGUAGE));
    return bar;
}

/* 把当前筛选状态回写到菜单勾选标记上。所有改状态的地方都要走这里，别各写各的。 */
static void SyncFilterMenu(void)
{
    HMENU bar = g_hMenuBar;
    HMENU filter;
    HMENU proto;
    HMENU lang;

    if (!bar) return;
    filter = GetSubMenu(bar, 0);
    if (!filter) return;

    CheckMenuItem(filter, IDM_AUTO, MF_BYCOMMAND | (g_autoOn ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(filter, IDM_LISTEN, MF_BYCOMMAND | (g_listenOnly ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(filter, IDM_HIDESYS, MF_BYCOMMAND | (g_hideSystem ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(filter, IDM_EXACT, MF_BYCOMMAND | (g_exactMatch ? MF_CHECKED : MF_UNCHECKED));

    proto = GetSubMenu(filter, 6);
    if (proto) {
        CheckMenuRadioItem(proto, IDM_PROTO_ALL, IDM_PROTO_V6,
                           IDM_PROTO_ALL + g_protoFilter, MF_BYCOMMAND);
    }

    lang = GetSubMenu(bar, 1);
    if (lang) {
        int cmd = g_english ? IDM_LANG_EN : IDM_LANG_ZH;
        CheckMenuRadioItem(lang, IDM_LANG_EN, IDM_LANG_ZH, cmd, MF_BYCOMMAND);
    }

    /* 工具条按钮的激活态读的就是这几面旗子，状态一变就重画 */
    if (g_hToolbar) InvalidateRect(g_hToolbar, NULL, TRUE);
}

static void UpdateStatus(void)
{
    WCHAR text[320];
    WCHAR cond[160];
    SYSTEMTIME st;
    size_t listen = 0, i;

    for (i = 0; i < g_allCount; ++i) {
        if (g_all[i].stateCode == MIB_TCP_STATE_LISTEN) listen++;
    }

    if (g_haveSuccessTime) st = g_lastSuccessTime;
    else ZeroMemory(&st, sizeof(st));

    /*
     * 生效中的筛选条件直接拼进状态栏。勾选项都搬进菜单后，不打开菜单就看不见
     * 当前到底滤了什么，列表突然变短会让人以为程序出问题了。
     */
    {
        static const int COND_PROTO[5] = { -1, TXT_COND_TCP, TXT_COND_UDP, TXT_COND_V4, TXT_COND_V6 };
        size_t k = 0;
        cond[0] = 0;
        if (g_protoFilter) {
            k = (size_t)_snwprintf(cond, 160, L"%s", Tr(COND_PROTO[g_protoFilter]));
        }
        if (g_listenOnly && k < 150) k += (size_t)_snwprintf(cond + k, 160 - k, L"%s", Tr(TXT_COND_LISTEN));
        if (g_hideSystem && k < 150) k += (size_t)_snwprintf(cond + k, 160 - k, L"%s", Tr(TXT_COND_HIDEP));
        if (g_exactMatch && k < 150) k += (size_t)_snwprintf(cond + k, 160 - k, L"%s", Tr(TXT_COND_EXACT));
        if (g_autoOn && k < 150) k += (size_t)_snwprintf(cond + k, 160 - k, L"%s", Tr(TXT_COND_AUTO));
    }

    if (g_portsLoading && !g_haveSuccessTime) {
        _snwprintf(text, 320, Tr(TXT_BAR_LOADING), cond);
    } else if (g_enumFailed) {
        _snwprintf(text, 320, Tr(TXT_BAR_FAILED), st.wHour, st.wMinute, st.wSecond);
    } else {
        const WCHAR *partial = (g_tableMask == PORT_TABLE_ALL) ? L"" : Tr(TXT_BAR_PARTIAL);
        const WCHAR *loading = g_portsLoading ? Tr(TXT_BAR_REFRESHING) : L"";
        _snwprintf(text, 320, Tr(TXT_BAR_SUMMARY),
                   loading, partial, (unsigned)g_allCount, (unsigned)listen,
                   (unsigned)g_viewCount, cond, st.wHour, st.wMinute, st.wSecond);
    }
    text[319] = 0;

    if (g_killBusy) wcscpy(text, Tr(TXT_BAR_KILLING));
    SendMessageW(g_hStatus, SB_SETTEXTW, 0, (LPARAM)text);

    /* 右端固定放操作提示；权限状态已由工具条的徽章承担，不再在这里重复 */
    SendMessageW(g_hStatus, SB_SETTEXTW, 1, (LPARAM)Tr(TXT_BAR_HINT));
}

/*
 * 空结果提示。区分两种「空」：本来就没数据（枚举成功但 0 条）与
 * 过滤后没数据（有数据但被条件筛掉了）——后者需要告诉用户去清条件，
 * 否则会以为是程序坏了。
 */
static const WCHAR *EmptyHintText(void)
{
    /* 区分两种「空」：本来就没数据，与有数据但被筛选条件滤光。
     * 后者必须提示去清条件，否则用户会以为程序坏了。 */
    if (g_portsLoading && g_allCount == 0) return Tr(TXT_EMPTY_LOADING);
    if (g_enumFailed && g_allCount == 0) return Tr(TXT_EMPTY_FAILED);
    if (g_allCount == 0) return Tr(TXT_EMPTY_NONE);
    if (g_hideSystem)
        return Tr(TXT_EMPTY_HIDEP);
    if (g_exactMatch)
        return Tr(TXT_EMPTY_EXACT);
    if (g_listenOnly || g_protoFilter != 0)
        return Tr(TXT_EMPTY_FILTER);
    if (g_allCount > 0)
        return Tr(TXT_EMPTY_NOMATCH);
    return Tr(TXT_EMPTY_NONE);
}

/*
 * 空结果提示由 PaintEmptyHint 在列表 WM_PAINT 里叠加绘制。
 * 不能在 ApplyView 里直接画：紧接着的 InvalidateRect 会把字擦掉。
 * 这里只准备文本与状态，判断留给绘制时。
 */
static BOOL ShouldPaintEmptyHint(void)
{
    return g_viewCount == 0;
}

/*
 * 在列表空白处叠加居中提示。调用前默认绘制已完成，这里只在「一个可见行都没有」
 * 时画字，避免盖住数据。
 */
static void PaintEmptyHint(HDC hdc)
{
    RECT rc;
    const WCHAR *msg;
    HFONT font;
    HGDIOBJ oldFont;
    COLORREF oldColor;
    int oldBk;

    if (!ShouldPaintEmptyHint()) return;

    GetClientRect(g_hList, &rc);
    /* 避开列头区域（行高加大后列头也更高），让提示落在数据区中央 */
    if (rc.bottom > S(g_hwndMain, 30)) rc.top += S(g_hwndMain, 30);

    msg = EmptyHintText();
    font = g_hFont ? g_hFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    /* SelectObject 返回 HGDIOBJ、SetTextColor 返回 COLORREF，两者的还原方式不同，不能共用一个变量 */
    oldFont = SelectObject(hdc, font);
    oldBk = SetBkMode(hdc, TRANSPARENT);
    oldColor = SetTextColor(hdc, RGB(130, 130, 130));

    DrawTextW(hdc, msg, -1, &rc, DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);

    SetTextColor(hdc, oldColor);
    SetBkMode(hdc, oldBk);
    SelectObject(hdc, oldFont);
}

/*
 * 列表子类：在默认 WM_PAINT 跑完之后，若一个可见行都没有，叠加空结果提示。
 * 默认绘制必须先做完（DefSubclassProc），否则提示会被列表自己重绘擦掉。
 */
static LRESULT CALLBACK ListSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref)
{
    /* DefSubclassProc 只收 4 个参数；id/ref 由框架自己带着，无需回传 */
    LRESULT r = DefSubclassProc(hwnd, msg, wp, lp);
    UNREFERENCED_PARAMETER(id);
    UNREFERENCED_PARAMETER(ref);

    if (msg == WM_PAINT && ShouldPaintEmptyHint()) {
        HDC hdc = GetDC(hwnd);
        if (hdc) {
            PaintEmptyHint(hdc);
            ReleaseDC(hwnd, hdc);
        }
    }

    return r;
}

/* 列头排序箭头：当前排序列和方向要一眼可见 */
static void UpdateSortMark(void)
{
    HWND hdr = ListView_GetHeader(g_hList);
    HDITEM hdi;
    int i, n;

    if (!hdr) return;

    n = Header_GetItemCount(hdr);
    for (i = 0; i < n; ++i) {
        hdi.mask = HDI_FORMAT;
        if (!Header_GetItem(hdr, i, &hdi)) continue;
        hdi.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (i == g_sortCol) hdi.fmt |= (g_sortAsc ? HDF_SORTUP : HDF_SORTDOWN);
        Header_SetItem(hdr, i, &hdi);
    }
}

/* 选中行底色：取样自默认 accent 的系统选中蓝，与相邻列系统绘制无缝 */
#define SEL_ROW_BG           RGB(204, 232, 255)

#define HIGHLIGHT_MS 2500   /* 新出现/刚消失的高亮保留时长 */

/* 线性插值两色，ratio 0..256（256=完全取 b） */
static COLORREF LerpColor(COLORREF a, COLORREF b, int ratio)
{
    return RGB(GetRValue(a) + ((GetRValue(b) - GetRValue(a)) * ratio >> 8),
               GetGValue(a) + ((GetGValue(b) - GetGValue(a)) * ratio >> 8),
               GetBValue(a) + ((GetBValue(b) - GetBValue(a)) * ratio >> 8));
}

/*
 * 行底色：斑马纹为基底，叠加「新出现绿 / 刚消失红」的时间渐退。
 * 新增连接在 HIGHLIGHT_MS 内从浅绿褪回底色，用于盯「哪个连接刚起来」；
 * 刚消失的连接保留同长时长红底，用于确认「谁刚断开」。
 * 选中行不参与（保留系统选中色）。
 */
static COLORREF RowBaseBg(const PORT_ENTRY *e, DWORD_PTR row, BOOL selected)
{
    COLORREF base = (row & 1) ? RGB(245, 247, 250) : RGB(255, 255, 255);
    DWORD now = GetTickCount();

    if (!selected) {
        if (e->dieTick) {
            DWORD t = now - e->dieTick;
            if (t < HIGHLIGHT_MS)
                return LerpColor(base, RGB(252, 226, 222), 200 - (int)(200 * t / HIGHLIGHT_MS));
        } else if (e->appearTick) {
            DWORD t = now - e->appearTick;
            if (t < HIGHLIGHT_MS)
                return LerpColor(base, RGB(214, 242, 214), 190 - (int)(190 * t / HIGHLIGHT_MS));
        }
    }
    return base;
}

/*
 * 交替行底色。选中判断走 ListView_GetItemState 实时查询：双缓冲下通知里的
 * uItemState 常常恒带 CDIS_SELECTED，靠它判断会让斑马纹整列失效。
 */
static void ApplyRowBg(NMLVCUSTOMDRAW *cd)
{
    BOOL selected = (ListView_GetItemState(g_hList, (int)cd->nmcd.dwItemSpec,
                                           LVIS_SELECTED) & LVIS_SELECTED) != 0;

    if (!selected && (size_t)cd->nmcd.dwItemSpec < g_viewCount) {
        int row = (int)cd->nmcd.dwItemSpec;

        /* 悬停行铺统一浅蓝高亮（系统的 hover 色会被这里的自绘底色盖掉），
         * 与状态格的悬停底同色，整行观感一致 */
        if (ListView_GetHotItem(g_hList) == row) {
            cd->clrTextBk = RGB(228, 240, 252);
        } else {
            cd->clrTextBk = RowBaseBg(&g_view[cd->nmcd.dwItemSpec],
                                      cd->nmcd.dwItemSpec, FALSE);
        }
    }
}

/*
 * 状态格整格自绘（SKIPDEFAULT，系统不再碰这格）：先铺行底色，再画徽章——
 * 浅底圆角块 + 深色字 + 前置圆点。悬停行铺固定的浅蓝灰 hover 底（系统的
 * hover 色同样取不到，用近似值，徽章形态保持稳定）；只有选中行走系统绘制
 * （见 WM_NOTIFY）；UDP 行没有连接状态，只铺底色不画徽章。
 */
static void DrawStateBadge(HDC hdc, RECT rcCell, const PORT_ENTRY *e, BOOL selected);
static void DrawStateCell(HDC hdc, RECT rcCell, const PORT_ENTRY *e, DWORD_PTR row, BOOL selected)
{
    BOOL hot = ListView_GetHotItem(g_hList) == (int)row;
    COLORREF bg = selected ? SEL_ROW_BG
                : (hot ? RGB(228, 240, 252) : RowBaseBg(e, row, FALSE));
    HBRUSH brush = CreateSolidBrush(bg);

    FillRect(hdc, &rcCell, brush);
    DeleteObject(brush);
    if (e->stateCode) DrawStateBadge(hdc, rcCell, e, selected);
}

/*
 * 状态徽章：浅底圆角块 + 深色字 + 前置圆点；选中行的蓝底上徽章翻白底。
 */
static void DrawStateBadge(HDC hdc, RECT rcCell, const PORT_ENTRY *e, BOOL selected)
{
    STATE_BADGE_COLORS colors;
    const WCHAR *text;
    HFONT font, oldFont;
    HBRUSH brush, oldBrush;
    HPEN pen, oldPen;
    SIZE size;
    RECT rcBadge;
    COLORREF oldText;
    int oldBk;
    int textLen;
    int badgeH, badgeW, dot;
    int cy = (rcCell.top + rcCell.bottom) / 2;
    int x;

    if (!e->stateCode) return;

    colors = StateBadgeColors(e);
    text = EntryStateText(e);
    textLen = (int)wcslen(text);
    if (!textLen) return;

    font = g_hFont ? g_hFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    oldFont = SelectObject(hdc, font);
    GetTextExtentPoint32W(hdc, text, textLen, &size);

    badgeH = rcCell.bottom - rcCell.top - S(g_hwndMain, 10);
    if (badgeH > S(g_hwndMain, 20)) badgeH = S(g_hwndMain, 20);
    dot = S(g_hwndMain, 6);
    badgeW = dot + S(g_hwndMain, 5) + size.cx + S(g_hwndMain, 18);
    if (badgeW > rcCell.right - rcCell.left - S(g_hwndMain, 8))
        badgeW = rcCell.right - rcCell.left - S(g_hwndMain, 8);

    rcBadge.left = rcCell.left + S(g_hwndMain, 6);
    rcBadge.top = cy - badgeH / 2;
    rcBadge.right = rcBadge.left + badgeW;
    rcBadge.bottom = rcBadge.top + badgeH;
    if (rcBadge.right > rcCell.right - S(g_hwndMain, 2))
        rcBadge.right = rcCell.right - S(g_hwndMain, 2);

    brush = CreateSolidBrush(selected ? RGB(255, 255, 255) : colors.bg);
    pen = CreatePen(PS_NULL, 0, 0);
    oldBrush = SelectObject(hdc, brush);
    oldPen = SelectObject(hdc, pen);
    RoundRect(hdc, rcBadge.left, rcBadge.top, rcBadge.right, rcBadge.bottom,
              badgeH, badgeH);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);

    x = rcBadge.left + S(g_hwndMain, 9);
    brush = CreateSolidBrush(colors.fg);
    oldBrush = SelectObject(hdc, brush);
    oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
    Ellipse(hdc, x, cy - dot / 2, x + dot, cy + dot / 2);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);

    oldBk = SetBkMode(hdc, TRANSPARENT);
    oldText = SetTextColor(hdc, colors.fg);
    TextOutW(hdc, x + dot + S(g_hwndMain, 5),
             cy - size.cy / 2, text, textLen);
    SetTextColor(hdc, oldText);
    SetBkMode(hdc, oldBk);
    SelectObject(hdc, oldFont);
}

/*
 * 扁平表头：白底、灰字、底部一条分隔线，当前排序列带主色小三角。
 * 系统的 3D 凸起表头和整体风格脱节，全自绘（SKIPDEFAULT）。
 */
static void DrawFlatHeader(NMCUSTOMDRAW *nmc, int sortCol, int sortAsc)
{
    HWND hdr = nmc->hdr.hwndFrom;
    WCHAR text[96];
    HDITEM hdi;
    HFONT font, oldFont;
    HBRUSH brush;
    RECT rc = nmc->rc, line;
    int oldBk;
    COLORREF oldText;

    brush = (HBRUSH)GetStockObject(WHITE_BRUSH);
    FillRect(nmc->hdc, &rc, brush);
    line = rc;
    line.top = line.bottom - 1;
    brush = CreateSolidBrush(CARD_BORDER);
    FillRect(nmc->hdc, &line, brush);
    DeleteObject(brush);
    rc = nmc->rc;

    hdi.mask = HDI_TEXT;
    hdi.pszText = text;
    hdi.cchTextMax = 96;
    if (Header_GetItem(hdr, (int)nmc->dwItemSpec, &hdi)) {
        font = g_hFont ? g_hFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        oldFont = SelectObject(nmc->hdc, font);
        oldBk = SetBkMode(nmc->hdc, TRANSPARENT);
        oldText = SetTextColor(nmc->hdc, RGB(100, 116, 139));
        rc.left += S(g_hwndMain, 10);
        rc.right -= S(g_hwndMain, 4);
        DrawTextW(nmc->hdc, text, -1, &rc,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SetTextColor(nmc->hdc, oldText);
        SetBkMode(nmc->hdc, oldBk);
        SelectObject(nmc->hdc, oldFont);
    }

    /* 排序三角盖在当前列右端，方向跟升/降序走；sortCol<0 表示无排序（模块列表） */
    if ((int)nmc->dwItemSpec == sortCol) {
        POINT pts[3];
        HGDIOBJ oldBrush2, oldPen;
        int ax = nmc->rc.right - S(g_hwndMain, 18);
        int ay = (nmc->rc.top + nmc->rc.bottom) / 2;

        if (sortAsc) {
            pts[0].x = ax;         pts[0].y = ay + S(g_hwndMain, 2);
            pts[1].x = ax + S(g_hwndMain, 8); pts[1].y = ay + S(g_hwndMain, 2);
            pts[2].x = ax + S(g_hwndMain, 4); pts[2].y = ay - S(g_hwndMain, 2);
        } else {
            pts[0].x = ax;         pts[0].y = ay - S(g_hwndMain, 2);
            pts[1].x = ax + S(g_hwndMain, 8); pts[1].y = ay - S(g_hwndMain, 2);
            pts[2].x = ax + S(g_hwndMain, 4); pts[2].y = ay + S(g_hwndMain, 2);
        }
        brush = CreateSolidBrush(ACCENT);
        oldBrush2 = SelectObject(nmc->hdc, brush);
        oldPen = SelectObject(nmc->hdc, GetStockObject(NULL_PEN));
        Polygon(nmc->hdc, pts, 3);
        SelectObject(nmc->hdc, oldBrush2);
        SelectObject(nmc->hdc, oldPen);
        DeleteObject(brush);
    }
}

/*
 * 列表采用 LVS_OWNERDATA（虚拟列表）：这里只负责重算 g_view、同步行数并重绘，
 * 行文本由 LVN_GETDISPINFO 按需提供。因此刷新不再 DeleteAllItems + 逐行 InsertItem，
 * 也不会为每行做 8 次 SetItemText。
 */
/*
 * 渐退高亮需要比 3 秒刷新更细的重绘粒度：可见行里还有未褪完的高亮时开
 * 300ms 的重绘节拍，全部褪色后自动停掉，不空转。
 */
static void ManageHighlightTimer(void)
{
    size_t i;
    DWORD now = GetTickCount();
    BOOL any = FALSE;

    for (i = 0; i < g_viewCount && !any; ++i) {
        if ((g_view[i].appearTick && now - g_view[i].appearTick < HIGHLIGHT_MS) ||
            (g_view[i].dieTick && now - g_view[i].dieTick < HIGHLIGHT_MS)) {
            any = TRUE;
        }
    }
    if (any) SetTimer(g_hwndMain, ID_HL_TIMER, 300, NULL);
    else KillTimer(g_hwndMain, ID_HL_TIMER);
}

static void ApplyView(BOOL keepViewport)
{
    WCHAR filter[256], port[32], pid[32], name[128];
    PORT_ENTRY selEntry;
    size_t i, n = 0, oldCount;
    int sel = -1, top = 0, newSel = -1, haveSel = 0, anchor = 0;

    /* 记住当前选中项与滚动位置 */
    oldCount = g_viewCount;
    top = ListView_GetTopIndex(g_hList);
    if (top >= 0 && (size_t)top < g_viewCount) anchor = (int)g_view[top].localPort;
    sel = ListView_GetNextItem(g_hList, -1, LVNI_SELECTED);
    if (sel >= 0 && (size_t)sel < g_viewCount) {
        selEntry = g_view[sel];   /* 结构体拷贝，无需格式化成字符串 */
        haveSel = 1;
    }

    ReadQuery(g_hEdit, filter, 256);
    ReadQuery(g_hEditPort, port, 32);
    ReadQuery(g_hEditPid, pid, 32);
    ReadQuery(g_hEditName, name, 128);

    free(g_view);
    g_view = NULL;
    g_viewCount = 0;

    if (g_allCount) {
        g_view = (PORT_ENTRY *)malloc(g_allCount * sizeof(PORT_ENTRY));
        if (g_view) {
            for (i = 0; i < g_allCount; ++i) {
                const PORT_ENTRY *e = &g_all[i];
                BOOL listening = (e->stateCode == MIB_TCP_STATE_LISTEN);

                /* 四个条件是「与」：文本框模糊匹配 + 协议范围 + 仅监听 + 屏蔽系统端口
                 * UDP 没有连接状态，本身就是常驻端口，所以不参与「仅监听」判定，
                 * 否则勾上之后 UDP 会整片消失。 */
                if (g_listenOnly && !e->stateCode) continue;
                if (g_listenOnly && !listening) continue;
                if (g_hideSystem && IsSystemOwner(e)) continue;
                if (!MatchProto(e, g_protoFilter)) continue;
                if (!MatchField(e->portText, port)) continue;
                if (!MatchField(e->pidText, pid)) continue;
                if (!MatchField(e->procName, name)) continue;
                if (!MatchFilter(e, filter)) continue;

                g_view[n++] = *e;
            }
            if (n) qsort(g_view, n, sizeof(PORT_ENTRY), CmpEntry);
        }
    }
    g_viewCount = n;

    /* 行数变化才通知列表，之后统一重绘刷新可见区域的内容 */
    if (n != oldCount) {
        ListView_SetItemCountEx(g_hList, (int)n, LVSICF_NOINVALIDATEALL);
    }
    InvalidateRect(g_hList, NULL, TRUE);

    if (haveSel) {
        for (i = 0; i < g_viewCount; ++i) {
            if (SameEntry(&g_view[i], &selEntry)) { newSel = (int)i; break; }
        }
    }

    if (newSel >= 0) {
        ListView_SetItemState(g_hList, newSel, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        if (!keepViewport) ListView_EnsureVisible(g_hList, newSel, FALSE);
    } else if (haveSel) {
        ListView_SetItemState(g_hList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    }

    /*
     * 没有可恢复的选中项时默认选中第一行：首屏、以及筛选把选中行滤掉的
     * 场景都覆盖，详情栏永远有内容。这里必须独立判断、不能挂进下面的
     * else-if 链：启动路径恒走 keepViewport 分支，挂在链上永远轮不到。
     */
    if (keepViewport && g_viewCount) {
        int restore = top;
        if (anchor) {
            for (i = 0; i < g_viewCount; ++i) {
                if ((int)g_view[i].localPort == anchor) { restore = (int)i; break; }
            }
        }
        if (restore < 0) restore = 0;
        if ((size_t)restore >= g_viewCount) restore = (int)g_viewCount - 1;
        ListView_EnsureVisible(g_hList, restore, TRUE);
    } else if (newSel < 0 && top > 0 && g_viewCount) {
        if ((size_t)top >= g_viewCount) top = (int)g_viewCount - 1;
        ListView_EnsureVisible(g_hList, top, TRUE);
    }

    InvalidateRect(g_hList, NULL, FALSE);
    UpdateStatus();
    UpdateSortMark();
    ManageHighlightTimer();
}

typedef struct {
    unsigned generation;
    PORT_ENTRY *entries;
    size_t count;
    unsigned tables;
    int ok;
} PORTS_RESULT;

static DWORD WINAPI PortsWorker(LPVOID param)
{
    PORTS_RESULT *result = (PORTS_RESULT *)param;

    result->ok = PortsEnumerateEx(&result->entries, &result->count, &result->tables);
    if (!PostMessageW(g_hwndMain, WM_APP_PORTS, 0, (LPARAM)result)) {
        PortsFree(result->entries);
        free(result);
    }
    InterlockedExchange(&g_portsBusy, 0);
    return 0;
}

static void RequestPorts(void)
{
    PORTS_RESULT *result;
    HANDLE thread;

    if (InterlockedCompareExchange(&g_portsBusy, 1, 0) != 0) {
        g_portsPending = TRUE;
        return;
    }

    result = (PORTS_RESULT *)calloc(1, sizeof(*result));
    if (!result) {
        InterlockedExchange(&g_portsBusy, 0);
        g_enumFailed = 1;
        UpdateStatus();
        return;
    }
    result->generation = ++g_portsGeneration;
    g_portsLoading = TRUE;
    UpdateStatus();

    thread = CreateThread(NULL, 0, PortsWorker, result, 0, NULL);
    if (!thread) {
        free(result);
        InterlockedExchange(&g_portsBusy, 0);
        g_portsLoading = FALSE;
        g_enumFailed = 1;
        UpdateStatus();
        return;
    }
    CloseHandle(thread);
}

/*
 * 两轮枚举对比用的端口索引：把记录下标按 localPort 摊进 2 的幂个桶，
 * 「同一条连接」的匹配只扫同桶，复杂度从 O(n*m) 降到 O(n)——
 * 几千连接的服务器上没有这个索引，每 3 秒一次的全量比对会卡出感知。
 */
typedef struct {
    size_t nBuckets;
    size_t *start;   /* nBuckets+1：桶起点（前缀和） */
    int *items;      /* count：记录下标 */
} PORT_INDEX;

static void BuildPortIndex(const PORT_ENTRY *table, size_t count, size_t nBuckets, PORT_INDEX *ix)
{
    size_t i, b;
    size_t *cur;

    ix->nBuckets = nBuckets;
    ix->start = (size_t *)malloc((nBuckets + 1) * sizeof(size_t));
    ix->items = (int *)malloc((count ? count : 1) * sizeof(int));
    if (!ix->start || !ix->items) {
        free(ix->start);
        free(ix->items);
        ix->start = NULL;
        ix->items = NULL;
        return;
    }
    for (b = 0; b <= nBuckets; ++b) ix->start[b] = 0;
    for (i = 0; i < count; ++i) ix->start[(table[i].localPort & (nBuckets - 1)) + 1]++;
    for (b = 0; b < nBuckets; ++b) ix->start[b + 1] += ix->start[b];

    cur = (size_t *)malloc(nBuckets * sizeof(size_t));
    if (!cur) {
        free(ix->start);
        free(ix->items);
        ix->start = NULL;
        ix->items = NULL;
        return;
    }
    for (b = 0; b < nBuckets; ++b) cur[b] = ix->start[b];
    for (i = 0; i < count; ++i) {
        b = table[i].localPort & (nBuckets - 1);
        ix->items[cur[b]++] = (int)i;
    }
    free(cur);
}

static BOOL PortIndexFind(const PORT_INDEX *ix, const PORT_ENTRY *table,
                          const PORT_ENTRY *e, size_t *outIdx)
{
    size_t b = e->localPort & (ix->nBuckets - 1);
    size_t k;

    if (!ix->items) return FALSE;
    for (k = ix->start[b]; k < ix->start[b + 1]; ++k) {
        size_t i = (size_t)ix->items[k];
        if (SameEntry(&table[i], e)) {
            if (outIdx) *outIdx = i;
            return TRUE;
        }
    }
    return FALSE;
}

static void FreePortIndex(PORT_INDEX *ix)
{
    free(ix->start);
    free(ix->items);
    ix->start = NULL;
    ix->items = NULL;
}

static void ApplyPortsResult(PORTS_RESULT *result)
{
    if (!result) return;
    if (result->generation == g_portsGeneration) {
        g_portsLoading = FALSE;
        if (result->ok) {
            PORT_ENTRY *old = g_all;
            size_t oldCount = g_allCount;
            PORT_ENTRY *nu = result->entries;
            size_t nuCount = result->count;
            size_t i, j;
            DWORD now = GetTickCount();

            /*
             * 新旧两轮枚举对比：
             * 仍在的连接继承上次的「出现时刻」，新面孔记为刚出现（绿底渐退）；
             * 消失的连接保留 HIGHLIGHT_MS（红底渐退）后移出显示，让人确认「谁刚断开」。
             * 匹配走端口桶索引，连接数大时比对依然是线性的。
             */
            {
                PORT_INDEX oldIx = {0}, nuIx = {0};

                BuildPortIndex(old, oldCount, 1024, &oldIx);
                for (i = 0; i < nuCount; ++i) {
                    size_t oi;
                    if (PortIndexFind(&oldIx, old, &nu[i], &oi)) {
                        nu[i].appearTick = old[oi].appearTick;
                        nu[i].dieTick = 0;
                    } else {
                        nu[i].appearTick = now;
                    }
                }
                FreePortIndex(&oldIx);

                if (oldCount && nu) {
                    PORT_ENTRY *grown = (PORT_ENTRY *)realloc(nu, (nuCount + oldCount) * sizeof(PORT_ENTRY));
                    if (grown) {
                        size_t kept = nuCount;
                        nu = grown;
                        result->entries = grown;
                        BuildPortIndex(nu, nuCount, 1024, &nuIx);
                        for (j = 0; j < oldCount; ++j) {
                            if (old[j].dieTick && now - old[j].dieTick >= HIGHLIGHT_MS) continue;
                            if (PortIndexFind(&nuIx, nu, &old[j], NULL)) continue;
                            nu[kept] = old[j];
                            nu[kept].dieTick = now;   /* 保留显示，红底渐退倒计时重新起算 */
                            kept++;
                        }
                        FreePortIndex(&nuIx);
                        nuCount = kept;
                    }
                }
            }

            g_all = nu;
            g_allCount = nuCount;
            free(old);   /* 旧表已并入（延续行被继承、消失行已拷出），释放 */
            g_tableMask = result->tables;
            g_enumFailed = 0;
            GetLocalTime(&g_lastSuccessTime);
            g_haveSuccessTime = TRUE;
            result->entries = NULL;
        } else {
            g_enumFailed = 1;
        }
        ApplyView(TRUE);
    }
    PortsFree(result->entries);
    free(result);

    if (g_portsPending && IsWindow(g_hwndMain)) {
        g_portsPending = FALSE;
        RequestPorts();
    }
}

static void ReloadAndApply(void)
{
    RequestPorts();
}

static const PORT_ENTRY *SelectedEntry(void)
{
    int i = ListView_GetNextItem(g_hList, -1, LVNI_SELECTED);
    if (i < 0 || (size_t)i >= g_viewCount) return NULL;
    return &g_view[i];
}

/* ------------------------------------------------------------ 操作 */

/*
 * 提权确认：工具条徽章、状态栏或快捷键都走这里，统一提示文案与失败处理。
 * 提权成功后关闭当前实例，避免出现两个窗口同时枚举端口。
 * 当前筛选条件随命令行带给新实例，重启后看到的还是同一个视图。
 */
static BOOL ConfirmElevate(HWND hwnd)
{
    WCHAR params[1024], key[512], port[32], pid[32], name[256];
    WCHAR qKey[1044], qPort[72], qPid[72], qName[532];

    if (MessageBoxW(hwnd, Tr(TXT_MB_ELEVATE_BODY), Tr(TXT_MB_ELEVATE_TITLE),
                    MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return FALSE;
    }

    ReadQuery(g_hEdit, key, 512);
    ReadQuery(g_hEditPort, port, 32);
    ReadQuery(g_hEditPid, pid, 32);
    ReadQuery(g_hEditName, name, 256);
    QuoteArg(qKey, 1044, key);
    QuoteArg(qPort, 72, port);
    QuoteArg(qPid, 72, pid);
    QuoteArg(qName, 532, name);
    _snwprintf(params, 1024,
               L"--key %s --port %s --pid %s --name %s"
               L" --proto %d --listen %d --hidesys %d --exact %d --auto %d",
               qKey, qPort, qPid, qName,
               g_protoFilter, g_listenOnly ? 1 : 0, g_hideSystem ? 1 : 0,
               g_exactMatch ? 1 : 0, g_autoOn ? 1 : 0);
    params[1023] = 0;

    if (ProcElevate(hwnd, params)) {
        PostMessage(g_hwndMain, WM_CLOSE, 0, 0);
        return TRUE;
    }

    MessageBoxW(hwnd, Tr(TXT_MB_ELEVATE_FAIL), Tr(TXT_MB_HINT), MB_OK | MB_ICONWARNING);
    return FALSE;
}

typedef struct {
    HWND hwnd;
    PORT_ENTRY entry;
    BOOL tree;
    PROC_KILL_RESULT result;
    DWORD error;
} KILL_REQUEST;

static DWORD WINAPI KillWorker(LPVOID param)
{
    KILL_REQUEST *request = (KILL_REQUEST *)param;

    request->result = request->tree
        ? ProcTerminateTree(request->entry.pid, &request->entry.procCreate)
        : ProcTerminate(request->entry.pid, &request->entry.procCreate);
    request->error = GetLastError();
    if (!PostMessageW(request->hwnd, WM_APP_KILL, 0, (LPARAM)request) &&
        !PostMessageW(g_hwndMain, WM_APP_KILL, 0, (LPARAM)request)) {
        free(request);
    }
    InterlockedExchange(&g_killBusy, 0);
    return 0;
}

static void StartKill(HWND hwnd, const PORT_ENTRY *target, BOOL tree)
{
    KILL_REQUEST *request;
    HANDLE thread;

    if (InterlockedCompareExchange(&g_killBusy, 1, 0) != 0) {
        MessageBoxW(hwnd, Tr(TXT_MB_KILL_BUSY), Tr(TXT_MB_WAIT),
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    request = (KILL_REQUEST *)calloc(1, sizeof(*request));
    if (!request) {
        InterlockedExchange(&g_killBusy, 0);
        MessageBoxW(hwnd, Tr(TXT_MB_OOM), Tr(TXT_MB_FAIL), MB_OK | MB_ICONERROR);
        return;
    }
    request->hwnd = hwnd;
    request->entry = *target;
    request->tree = tree;
    SendMessageW(g_hStatus, SB_SETTEXTW, 0, (LPARAM)Tr(TXT_BAR_KILLING));
    SetTimer(g_hwndMain, ID_KILL_TIMER, 250, NULL);

    thread = CreateThread(NULL, 0, KillWorker, request, 0, NULL);
    if (!thread) {
        KillTimer(g_hwndMain, ID_KILL_TIMER);
        free(request);
        InterlockedExchange(&g_killBusy, 0);
        MessageBoxW(hwnd, Tr(TXT_MB_START_FAIL), Tr(TXT_MB_FAIL), MB_OK | MB_ICONERROR);
        UpdateStatus();
    } else {
        CloseHandle(thread);
    }
}

static void DiscardQueuedWork(HWND hwnd)
{
    MSG msg;

    g_portsGeneration++;
    g_portsPending = FALSE;
    while (PeekMessageW(&msg, hwnd, WM_APP_PORTS, WM_APP_KILL, PM_REMOVE)) {
        if (msg.message == WM_APP_PORTS) {
            PORTS_RESULT *result = (PORTS_RESULT *)msg.lParam;
            if (result) {
                PortsFree(result->entries);
                free(result);
            }
        } else if (msg.message == WM_APP_KILL) {
            free((KILL_REQUEST *)msg.lParam);
        }
    }
}

static void FinishKill(KILL_REQUEST *request)
{
    WCHAR msg[512];

    KillTimer(g_hwndMain, ID_KILL_TIMER);
    if (!request) return;
    if (IsWindow(request->hwnd)) {
        switch (request->result) {
        case PROC_KILL_OK:
            break;
        case PROC_KILL_REUSED:
            MessageBoxW(request->hwnd, Tr(TXT_MB_REUSED_BODY), Tr(TXT_MB_ABORTED),
                        MB_OK | MB_ICONWARNING);
            break;
        case PROC_KILL_PARTIAL:
            MessageBoxW(request->hwnd, Tr(TXT_MB_PARTIAL_BODY), Tr(TXT_MB_PARTIAL_TITLE),
                        MB_OK | MB_ICONWARNING);
            break;
        case PROC_KILL_SELF:
            MessageBoxW(request->hwnd, Tr(TXT_MB_SELF_BODY), Tr(TXT_MB_ABORTED),
                        MB_OK | MB_ICONWARNING);
            break;
        case PROC_KILL_FAILED:
        default:
            _snwprintf(msg, 512, Tr(TXT_MB_KILL_ERR), request->error);
            MessageBoxW(request->hwnd, msg, Tr(TXT_MB_FAIL), MB_OK | MB_ICONERROR);
            break;
        }
    }
    free(request);
    if (IsWindow(g_hwndMain)) PostMessageW(g_hwndMain, WM_APP_REFRESH, 0, 0);
    else UpdateStatus();
}

static void DoKill(HWND hwnd, const PORT_ENTRY *e, BOOL tree)
{
    WCHAR msg[512], title[128];
    PORT_ENTRY target;

    if (!e) return;

    /* 拷到本地：下面的确认框会阻塞消息循环，期间定时刷新可能已经释放掉 g_view */
    target = *e;

    if (target.pid == 0 || target.pid == 4) {
        MessageBoxW(hwnd, Tr(TXT_MB_SYS_PROC), Tr(TXT_MB_CANNOT_KILL), MB_OK | MB_ICONWARNING);
        return;
    }

    if (!target.procCreateValid) {
        MessageBoxW(hwnd, Tr(TXT_MB_IDENTITY_BODY), Tr(TXT_MB_NOT_KILLED),
                    MB_OK | MB_ICONWARNING);
        PostMessage(g_hwndMain, WM_APP_REFRESH, 0, 0);
        return;
    }

    wcsncpy(title, Tr(tree ? TXT_MB_KILL_TREE_TITLE : TXT_MB_KILL_TITLE), 127);
    title[127] = 0;

    _snwprintf(msg, 512,
               Tr(tree ? TXT_MB_KILL_TREE_CONFIRM : TXT_MB_KILL_CONFIRM),
               target.procName, target.pid);

    if (MessageBoxW(hwnd, msg, title, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;

    /* 创建时间随请求带走：确认框期间列表可能刷新，不能结束后再读界面上的旧指针。 */
    StartKill(hwnd, &target, tree);
}

/* CSV 单元格写入：含逗号/引号/换行的值包引号并把内部引号翻倍 */
static void CsvWriteField(FILE *f, const WCHAR *text)
{
    BOOL quote = wcschr(text, L',') || wcschr(text, L'"') || wcschr(text, L'\n');
    const WCHAR *p;

    if (!quote) {
        fprintf(f, "%ls", text);
        return;
    }
    fprintf(f, "\"");
    for (p = text; *p; ++p) {
        if (*p == L'"') fprintf(f, "\"\"");
        else fprintf(f, "%lc", *p);
    }
    fprintf(f, "\"");
}

/* 当前筛选后的视图导出为 CSV（UTF-8 BOM，Excel 直接打开不乱码） */
static void ExportCsv(HWND hwnd)
{
    static WCHAR filter[160];
    WCHAR file[MAX_PATH] = L"ports.csv";
    OPENFILENAMEW ofn;
    FILE *f;
    size_t i;
    int col;
    WCHAR *p;

    p = filter;
    wcscpy(p, Tr(TXT_CSV_FILTER));
    p += wcslen(p) + 1;
    wcscpy(p, L"*.csv");
    p += wcslen(p) + 1;
    wcscpy(p, Tr(TXT_CSV_FILTER_ALL));
    p += wcslen(p) + 1;
    wcscpy(p, L"*.*");
    p += wcslen(p) + 1;
    *p = 0;

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = Tr(TXT_CSV_TITLE);
    ofn.lpstrDefExt = L"csv";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetSaveFileNameW(&ofn)) return;

    f = _wfopen(file, L"wb");
    if (!f) {
        MessageBoxW(hwnd, Tr(TXT_MB_EXPORT_FAIL), Tr(TXT_MB_HINT), MB_OK | MB_ICONERROR);
        return;
    }
    fwrite("\xEF\xBB\xBF", 1, 3, f);
    for (col = 0; col < COL_COUNT; ++col) {
        if (col) fprintf(f, ",");
        CsvWriteField(f, Tr(COL_TEXT[col]));
    }
    fprintf(f, "\n");
    for (i = 0; i < g_viewCount; ++i) {
        for (col = 0; col < COL_COUNT; ++col) {
            if (col) fprintf(f, ",");
            CsvWriteField(f, CellText(&g_view[i], col));
        }
        fprintf(f, "\n");
    }
    fclose(f);

    {
        WCHAR msg[600];
        _snwprintf(msg, 600, Tr(TXT_MB_EXPORTED), (unsigned)g_viewCount, file);
        msg[599] = 0;
        MessageBoxW(hwnd, msg, Tr(TXT_CSV_TITLE), MB_OK | MB_ICONINFORMATION);
    }
}

static void ShowContextMenu(HWND hwnd, int item, int x, int y)
{
    HMENU menu;
    POINT pt;
    const PORT_ENTRY *e;

    if (item >= 0 && item < (int)g_viewCount) {
        ListView_SetItemState(g_hList, item, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
    }

    e = SelectedEntry();
    /* x/y 已是屏幕坐标（GetCursorPos / WM_CONTEXTMENU 传入），不要再转换一次 */
    pt.x = x;
    pt.y = y;

    menu = CreatePopupMenu();
    if (!menu) return;

    if (e) {
        AppendMenuW(menu, MF_STRING, IDM_DETAIL, Tr(TXT_CTX_DETAIL));
        AppendMenuW(menu, MF_STRING, IDM_OPEN_LOC, Tr(TXT_CTX_OPEN_LOC));
        AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(menu, MF_STRING, IDM_KILL, Tr(TXT_CTX_KILL));
        AppendMenuW(menu, MF_STRING, IDM_KILL_TREE, Tr(TXT_CTX_KILL_TREE));
        AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(menu, MF_STRING, IDM_COPY_PATH, Tr(TXT_CTX_COPY_PATH));
        AppendMenuW(menu, MF_STRING, IDM_COPY_PID, Tr(TXT_CTX_COPY_PID));
        AppendMenuW(menu, MF_STRING, IDM_COPY_ROW, Tr(TXT_CTX_COPY_ROW));
        AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(menu, MF_STRING, IDM_EXPORT, Tr(TXT_CTX_EXPORT));
        AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(menu, MF_STRING, IDM_FILTER_SEL, Tr(TXT_CTX_FILTER_SEL));
    }
    AppendMenuW(menu, MF_STRING, ID_BTN_REFRESH, Tr(TXT_CTX_REFRESH));

    TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(menu);
}

/* ------------------------------------------------------ 进程详情窗口 */

typedef struct {
    PORT_ENTRY entry;
    HFONT hFont;
    HICON icon;      /* 进程图标，WM_DESTROY 时 DestroyIcon */
    WCHAR *commandLine;
    unsigned generation;
    BOOL loading;
    BOOL alive;
} DETAIL_CTX;

typedef struct {
    HWND hwnd;
    unsigned generation;
    PORT_ENTRY entry;
    WCHAR *commandLine;
    int commandOk;
    int identityOk;
} DETAIL_RESULT;

static void SetCtlFont(HWND ctl, HFONT font)
{
    if (ctl && font) SendMessage(ctl, WM_SETFONT, (WPARAM)font, TRUE);
}

static void MoveCtl(HWND parent, int id, int x, int y, int w, int h)
{
    HWND c = GetDlgItem(parent, id);
    if (c) MoveWindow(c, x, y, w, h, TRUE);
}

static void FillModules(HWND hList, DWORD pid)
{
    MODULE_INFO *mods = NULL;
    size_t n = 0, i;
    LVITEMW it;

    ListView_DeleteAllItems(hList);

    if (!ProcEnumModules(pid, &mods, &n) || !mods) {
        /*
         * msg 是栈上局部数组：ListView_InsertItem 会在调用期间把字符串拷进自己的
         * 存储（本列表非 LVS_OWNERDATA），所以函数返回后指针失效是安全的。
         * 注意：若将来把这个列表改成虚拟列表 / 延迟渲染，就不能再这样传，必须先存到常驻缓冲。
         */
        WCHAR msg[160];
        wcscpy(msg, Tr(TXT_MB_NO_MODULES));
        ZeroMemory(&it, sizeof(it));
        it.mask = LVIF_TEXT;
        it.iItem = 0;
        it.iSubItem = 0;
        it.pszText = msg;
        ListView_InsertItem(hList, &it);
        return;
    }

    for (i = 0; i < n; ++i) {
        WCHAR base[32], size[32];

        ZeroMemory(&it, sizeof(it));
        it.mask = LVIF_TEXT;
        it.iItem = (int)i;
        it.iSubItem = 0;
        it.pszText = (LPWSTR)PathBaseName(mods[i].path);
        ListView_InsertItem(hList, &it);

        ListView_SetItemText(hList, (int)i, 1, mods[i].path);
        _snwprintf(base, 32, L"0x%I64X", mods[i].base);
        ListView_SetItemText(hList, (int)i, 2, base);
        _snwprintf(size, 32, L"%u KB", (unsigned)(mods[i].size / 1024));
        ListView_SetItemText(hList, (int)i, 3, size);
    }

    ProcFreeModules(mods);
}

static HICON LoadProcessIcon(const PORT_ENTRY *entry)
{
    HICON icon = NULL;

    if (entry->procPath[0] && ExtractIconExW(entry->procPath, 0, &icon, NULL, 1) > 0 && icon)
        return icon;
    if (icon) DestroyIcon(icon);
    {
        SHFILEINFOW info;
        const WCHAR *key = entry->procPath[0] ? entry->procPath : entry->procName;
        ZeroMemory(&info, sizeof(info));
        if (key[0] && SHGetFileInfoW(key, FILE_ATTRIBUTE_NORMAL, &info, sizeof(info),
                                     SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES))
            return info.hIcon;
    }
    return NULL;
}

static void ApplyProcessIcon(HWND hwnd, DETAIL_CTX *ctx, HICON icon)
{
    HWND control = GetDlgItem(hwnd, D_ICO);
    HICON old = ctx->icon;

    ctx->icon = icon;
    if (icon) {
        if (!control) {
            control = CreateWindowExW(0, L"Static", L"",
                                      WS_CHILD | WS_VISIBLE | SS_ICON | SS_CENTERIMAGE,
                                      0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)D_ICO, g_hInst, NULL);
        }
        if (control) SendMessageW(control, STM_SETICON, (WPARAM)icon, 0);
        else { DestroyIcon(icon); ctx->icon = NULL; }
    } else if (control) {
        DestroyWindow(control);
    }
    if (old && old != ctx->icon) DestroyIcon(old);
    SendMessageW(hwnd, WM_SIZE, 0, 0);
}

static void FreeDetailResult(DETAIL_RESULT *result)
{
    if (!result) return;
    free(result->commandLine);
    free(result);
}

static void DiscardQueuedDetail(HWND hwnd)
{
    MSG msg;

    while (PeekMessageW(&msg, hwnd, WM_APP_DETAIL, WM_APP_DETAIL, PM_REMOVE))
        FreeDetailResult((DETAIL_RESULT *)msg.lParam);
}

static DWORD WINAPI DetailWorker(LPVOID param)
{
    DETAIL_RESULT *result = (DETAIL_RESULT *)param;
    FILETIME current;

    ZeroMemory(&current, sizeof(current));
    result->identityOk = result->entry.procCreateValid &&
                         ProcGetStartTime(result->entry.pid, &current) &&
                         current.dwLowDateTime == result->entry.procCreate.dwLowDateTime &&
                         current.dwHighDateTime == result->entry.procCreate.dwHighDateTime;
    if (result->identityOk) {
        ProcGetPath(result->entry.pid, result->entry.procPath, MAX_PATH);
        result->commandOk = ProcGetCommandLineAlloc(result->entry.pid, &result->commandLine);
    }
    if (!PostMessageW(result->hwnd, WM_APP_DETAIL, 0, (LPARAM)result)) FreeDetailResult(result);
    return 0;
}

static void RequestDetail(HWND hwnd, DETAIL_CTX *ctx)
{
    DETAIL_RESULT *result;
    HANDLE thread;

    if (!ctx || ctx->loading) return;
    result = (DETAIL_RESULT *)calloc(1, sizeof(*result));
    if (!result) return;
    result->hwnd = hwnd;
    result->generation = ++ctx->generation;
    result->entry = ctx->entry;
    ctx->loading = TRUE;
    SetWindowTextW(GetDlgItem(hwnd, D_ED_CMD), Tr(TXT_D_LOADING));

    thread = CreateThread(NULL, 0, DetailWorker, result, 0, NULL);
    if (!thread) {
        ctx->loading = FALSE;
        FreeDetailResult(result);
        SetWindowTextW(GetDlgItem(hwnd, D_ED_CMD), Tr(TXT_D_UNAVAIL));
        return;
    }
    CloseHandle(thread);
}

static void ApplyDetailResult(HWND hwnd, DETAIL_CTX *ctx, DETAIL_RESULT *result)
{
    WCHAR title[512];
    HICON icon;

    if (!ctx || !result) { FreeDetailResult(result); return; }
    ctx->loading = FALSE;
    if (!ctx->alive || !IsWindow(hwnd) || result->generation != ctx->generation) {
        FreeDetailResult(result);
        return;
    }
    if (!result->identityOk) {
        SetWindowTextW(GetDlgItem(hwnd, D_ED_PATH), L"");
        SetWindowTextW(GetDlgItem(hwnd, D_ED_CMD), Tr(TXT_D_STALE));
        ListView_DeleteAllItems(GetDlgItem(hwnd, D_LIST_MOD));
        FreeDetailResult(result);
        return;
    }

    ctx->entry = result->entry;
    free(ctx->commandLine);
    ctx->commandLine = result->commandLine;
    result->commandLine = NULL;
    _snwprintf(title, 512, L"%s  (PID %u)", ctx->entry.procName, ctx->entry.pid);
    SetWindowTextW(GetDlgItem(hwnd, D_ST_NAME), title);
    SetWindowTextW(GetDlgItem(hwnd, D_ED_PATH), ctx->entry.procPath);
    SetWindowTextW(GetDlgItem(hwnd, D_ED_CMD),
                   result->commandOk && ctx->commandLine ? ctx->commandLine : Tr(TXT_D_UNAVAIL));
    icon = LoadProcessIcon(&ctx->entry);
    ApplyProcessIcon(hwnd, ctx, icon);
    FillModules(GetDlgItem(hwnd, D_LIST_MOD), ctx->entry.pid);
    FreeDetailResult(result);
}

static LRESULT CALLBACK DetailProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    DETAIL_CTX *ctx = (DETAIL_CTX *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    WCHAR buf[512];

    switch (msg) {
    case WM_CREATE:
    {
        CREATESTRUCT *cs = (CREATESTRUCT *)lp;
        PORT_ENTRY *pe = (PORT_ENTRY *)cs->lpCreateParams;
        HWND h;
        LVCOLUMNW col;
        int i;
        const WCHAR *titles[4] = { Tr(TXT_D_COL_MODULE), Tr(TXT_D_COL_PATH),
                                   Tr(TXT_D_COL_BASE), Tr(TXT_D_COL_SIZE) };
        const int widths[4] = { 150, 300, 110, 80 };

        ctx = (DETAIL_CTX *)calloc(1, sizeof(DETAIL_CTX));
        if (!ctx) return -1;
        if (!pe) { free(ctx); return -1; }
        ctx->entry = *pe;

        /* 先挂到窗口上：即便下面某步失败导致创建中止，WM_DESTROY 也能释放 ctx 与字体 */
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)ctx);
        ctx->hFont = CreateUIFont(GetDpiOf(hwnd));
        ctx->alive = TRUE;

        _snwprintf(buf, 512, L"%s  (PID %u)", ctx->entry.procName, ctx->entry.pid);
        h = CreateWindowExW(0, L"Static", buf, WS_CHILD | WS_VISIBLE | SS_LEFT,
                            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)D_ST_NAME, g_hInst, NULL);
        SetCtlFont(h, ctx->hFont);

        h = CreateWindowExW(0, L"Static", Tr(TXT_D_PATH_LABEL), WS_CHILD | WS_VISIBLE | SS_LEFT,
                            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)D_ST_PATH, g_hInst, NULL);
        SetCtlFont(h, ctx->hFont);

        h = CreateWindowExW(WS_EX_CLIENTEDGE, L"Edit", ctx->entry.procPath,
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_READONLY | ES_AUTOHSCROLL,
                            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)D_ED_PATH, g_hInst, NULL);
        SetCtlFont(h, ctx->hFont);

        h = CreateWindowExW(0, L"Static", Tr(TXT_D_CMD_LABEL), WS_CHILD | WS_VISIBLE | SS_LEFT,
                            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)D_ST_CMD, g_hInst, NULL);
        SetCtlFont(h, ctx->hFont);

        h = CreateWindowExW(WS_EX_CLIENTEDGE, L"Edit", Tr(TXT_D_LOADING),
                            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP |
                            ES_READONLY | ES_MULTILINE | ES_AUTOVSCROLL,
                            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)D_ED_CMD, g_hInst, NULL);
        SetCtlFont(h, ctx->hFont);

        h = CreateWindowExW(0, L"Static", Tr(TXT_D_MOD_LABEL),
                            WS_CHILD | WS_VISIBLE | SS_LEFT,
                            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)D_ST_MOD, g_hInst, NULL);
        SetCtlFont(h, ctx->hFont);

        h = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT |
                            LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)D_LIST_MOD, g_hInst, NULL);
        if (h) {
            SetWindowTheme(h, L"Explorer", NULL);
            ListView_SetExtendedListViewStyle(h, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            SetCtlFont(h, ctx->hFont);

            ZeroMemory(&col, sizeof(col));
            col.mask = LVCF_TEXT | LVCF_WIDTH;
            for (i = 0; i < 4; ++i) {
                col.pszText = (LPWSTR)titles[i];
                col.cx = S(hwnd, widths[i]);
                ListView_InsertColumn(h, i, &col);
            }
        }

        h = CreateWindowExW(0, L"Button", Tr(TXT_D_BTN_LOC),
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)D_BTN_LOC, g_hInst, NULL);
        SetCtlFont(h, ctx->hFont);
        SetWindowSubclass(h, DetailBtnProc, D_BTN_LOC, 0);

        h = CreateWindowExW(0, L"Button", Tr(TXT_D_BTN_KILL),
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)D_BTN_KILL, g_hInst, NULL);
        SetCtlFont(h, ctx->hFont);
        SetWindowSubclass(h, DetailBtnProc, D_BTN_KILL, 0);

        h = CreateWindowExW(0, L"Button", Tr(TXT_D_BTN_RELOAD),
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)D_BTN_RELOAD, g_hInst, NULL);
        SetCtlFont(h, ctx->hFont);
        SetWindowSubclass(h, DetailBtnProc, D_BTN_RELOAD, 0);

        h = CreateWindowExW(0, L"Button", Tr(TXT_D_BTN_CLOSE),
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)D_BTN_CLOSE, g_hInst, NULL);
        SetCtlFont(h, ctx->hFont);
        SetWindowSubclass(h, DetailBtnProc, D_BTN_CLOSE, 0);

        SendMessage(hwnd, WM_SIZE, 0, 0);
        RequestDetail(hwnd, ctx);
        return 0;
    }

    case WM_CTLCOLORSTATIC:
        /*
         * 详情窗的 Static（标题、标签、进程图标）必须跟窗口同底白色。
         * 不处理的话走 DefWindowProc，控件拿到系统默认的 BTNFACE 浅灰底，
         * 白底窗口上就是标题名称前面挂着一块灰方块。
         */
        SetTextColor((HDC)wp, RGB(31, 41, 55));
        SetBkColor((HDC)wp, GetSysColor(COLOR_WINDOW));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO *mmi = (MINMAXINFO *)lp;
        mmi->ptMinTrackSize.x = S(hwnd, 620);
        mmi->ptMinTrackSize.y = S(hwnd, 420);
        return 0;
    }

    case WM_DPICHANGED:
    {
        RECT *suggested = (RECT *)lp;
        if (ctx && ctx->hFont) DeleteObject(ctx->hFont);
        if (ctx) {
            ctx->hFont = CreateUIFont(GetDpiOf(hwnd));
            EnumChildWindows(hwnd, SetFontProc, (LPARAM)ctx->hFont);
        }
        SetWindowPos(hwnd, NULL, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_SIZE:
    {
        RECT rc;
        int w, h, pad, y, bh, bw;

        GetClientRect(hwnd, &rc);
        w = rc.right;
        h = rc.bottom;
        pad = S(hwnd, 10);
        bh = S(hwnd, 28);
        bw = S(hwnd, 110);

        /* 图标位于名称（PID）左侧；标题行须容纳完整图标，避免与映像路径标签重叠。 */
        {
            int iconW = GetDlgItem(hwnd, D_ICO) ? S(hwnd, 34) : 0;
            MoveCtl(hwnd, D_ICO, pad, pad, S(hwnd, 30), S(hwnd, 30));
            MoveCtl(hwnd, D_ST_NAME, pad + iconW, pad, w - pad * 2 - iconW, S(hwnd, 20));
            y = pad + S(hwnd, iconW ? 32 : 22);
        }
        MoveCtl(hwnd, D_ST_PATH, pad, y, w - pad * 2, S(hwnd, 18));
        MoveCtl(hwnd, D_ED_PATH, pad, y + S(hwnd, 18), w - pad * 2, S(hwnd, 24));
        MoveCtl(hwnd, D_ST_CMD, pad, y + S(hwnd, 44), w - pad * 2, S(hwnd, 18));
        MoveCtl(hwnd, D_ED_CMD, pad, y + S(hwnd, 62), w - pad * 2, S(hwnd, 58));
        MoveCtl(hwnd, D_ST_MOD, pad, y + S(hwnd, 122), w - pad * 2, S(hwnd, 18));

        y += S(hwnd, 140);
        {
            int gap = S(hwnd, 8);
            int buttonY = h - pad - bh;
            int listBottom = y + (h - y - pad - bh - S(hwnd, 8));
            if (w < pad * 2 + bw * 4 + gap * 3) {
                bw = (w - pad * 2 - gap) / 2;
                if (bw < S(hwnd, 88)) bw = S(hwnd, 88);
                buttonY -= bh + gap;
                listBottom = buttonY - S(hwnd, 8);
            }
            if (listBottom < y + S(hwnd, 40)) listBottom = y + S(hwnd, 40);
            MoveCtl(hwnd, D_LIST_MOD, pad, y, w - pad * 2, listBottom - y);
            MoveCtl(hwnd, D_BTN_LOC, pad, buttonY, bw, bh);
            MoveCtl(hwnd, D_BTN_KILL, pad + bw + gap, buttonY, bw, bh);
            MoveCtl(hwnd, D_BTN_RELOAD, w - pad - bw * 2 - gap, buttonY + (buttonY == h - pad - bh ? 0 : bh + gap), bw, bh);
            MoveCtl(hwnd, D_BTN_CLOSE, w - pad - bw, buttonY + (buttonY == h - pad - bh ? 0 : bh + gap), bw, bh);
        }
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case D_BTN_LOC:
            if (ctx && ctx->entry.procPath[0]) {
                if (!ProcOpenFileLocation(hwnd, ctx->entry.procPath)) {
                    MessageBoxW(hwnd, Tr(TXT_MB_NO_LOCATION), Tr(TXT_MB_HINT),
                                MB_OK | MB_ICONWARNING);
                }
            } else {
                MessageBoxW(hwnd, Tr(TXT_MB_NO_PATH), Tr(TXT_MB_HINT), MB_OK | MB_ICONWARNING);
            }
            return 0;

        case D_BTN_KILL:
            if (ctx) DoKill(hwnd, &ctx->entry, FALSE);
            return 0;

        case D_BTN_RELOAD:
            if (ctx) RequestDetail(hwnd, ctx);
            return 0;

        case D_BTN_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_DRAWITEM:
        if (wp == D_BTN_LOC || wp == D_BTN_KILL || wp == D_BTN_RELOAD || wp == D_BTN_CLOSE) {
            DrawDetailButton((DRAWITEMSTRUCT *)lp, ctx ? ctx->hFont : NULL);
            return TRUE;
        }
        break;

    case WM_NOTIFY:
        if (LOWORD(wp) == D_LIST_MOD) {
            NMHDR *nh = (NMHDR *)lp;
            if (nh->code == NM_CUSTOMDRAW &&
                nh->hwndFrom == ListView_GetHeader(GetDlgItem(hwnd, D_LIST_MOD))) {
                NMCUSTOMDRAW *nmc = (NMCUSTOMDRAW *)lp;
                if (nmc->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (nmc->dwDrawStage == CDDS_ITEMPREPAINT) {
                    DrawFlatHeader(nmc, -1, 0);   /* 模块列表不排序，只借扁平样式 */
                    return CDRF_SKIPDEFAULT;
                }
                return CDRF_DODEFAULT;
            }
        }
        if (LOWORD(wp) == D_LIST_MOD && ((NMHDR *)lp)->code == NM_DBLCLK) {
            int item = ListView_GetNextItem(GetDlgItem(hwnd, D_LIST_MOD), -1, LVNI_SELECTED);
            if (item >= 0) {
                WCHAR path[MAX_PATH];
                ListView_GetItemText(GetDlgItem(hwnd, D_LIST_MOD), item, 1, path, MAX_PATH);
                if (!path[0] || !ProcOpenFileLocation(hwnd, path)) {
                    MessageBoxW(hwnd, Tr(TXT_MB_NO_MOD_LOC), Tr(TXT_MB_HINT),
                                MB_OK | MB_ICONWARNING);
                }
            }
            return 0;
        }
        break;

    case WM_APP_DETAIL:
        if (ctx) ApplyDetailResult(hwnd, ctx, (DETAIL_RESULT *)lp);
        else FreeDetailResult((DETAIL_RESULT *)lp);
        return 0;

    case WM_DESTROY:
        DiscardQueuedDetail(hwnd);
        if (ctx) {
            ctx->alive = FALSE;
            ctx->generation++;
            if (ctx->hFont) DeleteObject(ctx->hFont);
            if (ctx->icon) DestroyIcon(ctx->icon);
            free(ctx->commandLine);
            free(ctx);
        }
        SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void OpenDetail(HWND parent, const PORT_ENTRY *e)
{
    PORT_ENTRY copy = *e;
    HWND hwnd;

    hwnd = CreateWindowExW(0, DETAIL_CLASS, Tr(TXT_DETAIL_TITLE),
                           WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                           CW_USEDEFAULT, CW_USEDEFAULT,
                           S(parent, 760), S(parent, 520),
                           parent, NULL, g_hInst, &copy);
    if (!hwnd) return;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
}

/* ---------------------------------------------------------- 主窗口 */

/*
 * 列宽自适应：前 8 列按 COL_WIDTHS 固定，映像路径列吃掉列表剩下的全部宽度。
 * 之前路径列写死 320px，而前面几列加起来已经接近甚至超过窗口宽度，路径被挤出可视区；
 * 改成「前面的定宽 + 路径吃掉剩余」后，窗口怎么缩放路径列都完整可见。
 */
static void LayoutColumns(HWND hwnd)
{
    RECT rc;
    int avail, fixed, pathW, i;

    if (!g_hList) return;

    GetClientRect(g_hList, &rc);
    avail = rc.right;

    fixed = 0;
    for (i = 0; i < COL_PATH; ++i) {
        int cx = g_colUserSized[i] ? ListView_GetColumnWidth(g_hList, i)
                                   : S(hwnd, COL_WIDTHS[i]);
        if (!g_colUserSized[i]) ListView_SetColumnWidth(g_hList, i, cx);
        fixed += cx;
    }

    /* 用户拖过路径列后保留其宽度；否则路径列继续吃掉剩余空间。 */
    if (!g_colUserSized[COL_PATH]) {
        pathW = avail - fixed - S(hwnd, 2);
        if (pathW < S(hwnd, COL_PATH_MIN)) pathW = S(hwnd, COL_PATH_MIN);
        ListView_SetColumnWidth(g_hList, COL_PATH, pathW);
    }
}

/*
 * 量出标签当前文字的实际宽度。列宽原本按中文字数写死，换成英文同样的位置
 * 要放「Process」「Keyword」这类长词，写死的宽度会把最后一个字符切掉。
 */
static int LabelWidth(HWND label, int fallback)
{
    WCHAR text[64];
    HDC hdc;
    HFONT old;
    SIZE size;
    int n;

    if (!label) return fallback;
    n = GetWindowTextW(label, text, 64);
    if (n <= 0 || n >= 64) return fallback;

    hdc = GetDC(label);
    if (!hdc) return fallback;
    old = (HFONT)SelectObject(hdc, (HFONT)SendMessageW(label, WM_GETFONT, 0, 0));
    GetTextExtentPoint32W(hdc, text, n, &size);
    SelectObject(hdc, old);
    ReleaseDC(label, hdc);

    return size.cx + S(label, 6);   /* 留一点余量，避免末字符贴边 */
}

/*
 * 进程图标列表。报告视图的行高跟随 LVSIL_SMALL 图标高度，这里用 20x26（96dpi 基准）
 * 的图标格同时解决两件事：行高比默认高出一截，文字不再贴着上缘；每个进程前
 * 挂上从 exe 提取的真实图标，扫视效率远高于逐行读名字。
 * index 0 是一张全透明占位图：路径拿不到的行显示它，不会错挂默认图标。
 */
static void ApplyListIcons(HWND hwnd)
{
    HIMAGELIST list;
    HBITMAP bmp, oldBmp;
    HDC hdc, mem;
    int cx = S(hwnd, 20), cy = S(hwnd, 26);

    hdc = GetDC(hwnd);
    if (!hdc) return;
    list = ImageList_Create(cx, cy, ILC_COLOR32 | ILC_MASK, 64, 32);
    bmp = CreateCompatibleBitmap(hdc, cx, cy);
    if (list && bmp) {
        mem = CreateCompatibleDC(hdc);
        oldBmp = (HBITMAP)SelectObject(mem, bmp);
        PatBlt(mem, 0, 0, cx, cy, BLACKNESS);   /* 全黑配全黑掩码 = 完全透明 */
        SelectObject(mem, oldBmp);
        DeleteDC(mem);
        ImageList_AddMasked(list, bmp, RGB(0, 0, 0));
    }
    if (bmp) DeleteObject(bmp);
    ReleaseDC(hwnd, hdc);
    if (!list) return;

    if (g_hIcons) ImageList_Destroy(g_hIcons);
    g_hIcons = list;
    g_iconCacheCount = 0;   /* ImageList 重建后索引全部作废，缓存清空 */
    ListView_SetImageList(g_hList, list, LVSIL_SMALL);
}

static HICON LoadProcessIcon(const PORT_ENTRY *entry);

/* 按路径（无路径时按进程名）取图标在 ImageList 里的索引，未命中才真正提取 */
static int IconIndexFor(const PORT_ENTRY *e)
{
    const WCHAR *key;
    int i, index;
    HICON icon;

    if (!g_hIcons) return 0;
    key = e->procPath[0] ? e->procPath : e->procName;
    if (!key[0]) return 0;

    for (i = 0; i < g_iconCacheCount; ++i) {
        if (_wcsicmp(g_iconCache[i].key, key) == 0) return g_iconCache[i].index;
    }

    icon = LoadProcessIcon(e);
    if (!icon) return 0;
    index = ImageList_AddIcon(g_hIcons, icon);
    DestroyIcon(icon);   /* ImageList_AddIcon 已复制，句柄立即释放 */
    if (index < 0) return 0;

    if (g_iconCacheCount < ICON_CACHE_MAX) {
        _snwprintf(g_iconCache[g_iconCacheCount].key, MAX_PATH, L"%s", key);
        g_iconCache[g_iconCacheCount].key[MAX_PATH - 1] = 0;
        g_iconCache[g_iconCacheCount].index = index;
        g_iconCacheCount++;
    }
    return index;
}

/*
 * 顶栏总高：工具条 + 一行式查询卡片（含上下留白）。
 * 窗口最小宽度保证四个条件恒排一行，列表上沿与背景分层都取这个值。
 */
static int QueryBandHeight(HWND hwnd)
{
    return S(hwnd, TOOLBAR_H + 6 + 4 + 28 + 4 + 6);
}

static void LayoutMain(HWND hwnd)
{
    RECT rc, rs;
    int w, h, pad, bh, toolbarY, sbH;
    int parts[2];

    if (!g_hList) return;

    GetClientRect(hwnd, &rc);
    w = rc.right;
    h = rc.bottom;
    pad = S(hwnd, 8);
    bh = S(hwnd, 28);
    /* 查询行从工具条下面开始：工具条高 + 卡片外留白 + 卡片内边距 */
    toolbarY = S(hwnd, TOOLBAR_H + 6 + 4);

    if (g_hToolbar) MoveWindow(g_hToolbar, 0, 0, w, S(hwnd, TOOLBAR_H), TRUE);

    /*
     * 状态栏高度必须在「状态栏自己的坐标系」里量，不能用 GetWindowRect：
     * 那是屏幕坐标，窗口一旦不贴在 (0,0)，减出来的是负数或巨大值，
     * 列表与信息栏就会被算到屏幕外面去。这里改用 MapWindowPoints 把它
     * 换算到主窗口客户区坐标，再和 h 一起用。
     */
    SendMessage(g_hStatus, WM_SIZE, 0, 0);
    GetWindowRect(g_hStatus, &rs);
    {
        POINT ptTopLeft, ptBottomRight;
        ptTopLeft.x = rs.left;
        ptTopLeft.y = rs.top;
        ptBottomRight.x = rs.right;
        ptBottomRight.y = rs.bottom;
        MapWindowPoints(NULL, hwnd, &ptTopLeft, 1);
        MapWindowPoints(NULL, hwnd, &ptBottomRight, 1);
        sbH = ptBottomRight.y - ptTopLeft.y;
    }
    if (sbH <= 0) sbH = S(hwnd, 22);   /* 量不到时给个合理兜底，不要让布局崩掉 */

    /*
     * 查询条件一行式：关键字与进程名弹性分剩余宽度，关键字封顶防止
     * 无限拉长。四个标签同宽右对齐，输入框左缘落在同一条竖线上。
     * 窗口最小宽度保证这行排得下，不做换行降级。
     */
    {
        int gap = S(hwnd, 10);
        int labelGap = S(hwnd, 6);
        int keyCap = S(hwnd, 420);
        int wLabel = LabelWidth(g_hLblKey, S(hwnd, 48));
        int t = LabelWidth(g_hLblPort, S(hwnd, 34));
        int wPort = S(hwnd, 88);      /* 端口号最长 5 位 */
        int wPid = S(hwnd, 96);       /* PID 常见 7~8 位 */
        int x = pad;
        int y = toolbarY;
        int listTop, listH;
        int keyW, nameW, flex;

        /* 四个标签取同一个宽度并右对齐 */
        if (t > wLabel) wLabel = t;
        t = LabelWidth(g_hLblPid, S(hwnd, 30));
        if (t > wLabel) wLabel = t;
        t = LabelWidth(g_hLblName, S(hwnd, 48));
        if (t > wLabel) wLabel = t;

        flex = w - pad * 2 - ((wLabel + labelGap) * 4 + wPort + wPid + gap * 4
                              + S(hwnd, 24) + S(hwnd, 12));   /* 尾部给 ✕ 清除按钮留位 */
        if (flex < S(hwnd, 240)) flex = S(hwnd, 240);   /* 最小宽度之上不该发生，兜底 */
        keyW = keyCap;
        if (keyW > flex - S(hwnd, 120)) keyW = flex - S(hwnd, 120);
        if (keyW < S(hwnd, 120)) keyW = S(hwnd, 120);
        nameW = flex - keyW;

        MoveWindow(g_hLblKey, x, y, wLabel, bh, TRUE);
        x += wLabel + labelGap;
        MoveWindow(g_hEdit, x, y, keyW, bh, TRUE);
        x += keyW + gap;
        MoveWindow(g_hLblPort, x, y, wLabel, bh, TRUE);
        x += wLabel + labelGap;
        MoveWindow(g_hEditPort, x, y, wPort, bh, TRUE);
        x += wPort + gap;
        MoveWindow(g_hLblPid, x, y, wLabel, bh, TRUE);
        x += wLabel + labelGap;
        MoveWindow(g_hEditPid, x, y, wPid, bh, TRUE);
        x += wPid + gap;
        MoveWindow(g_hLblName, x, y, wLabel, bh, TRUE);
        x += wLabel + labelGap;
        MoveWindow(g_hEditName, x, y, nameW, bh, TRUE);
        x += nameW + gap;
        MoveWindow(g_hBtnClear, x, y + (bh - S(hwnd, 24)) / 2, S(hwnd, 24), S(hwnd, 24), TRUE);

        listTop = QueryBandHeight(hwnd);
        listH = h - sbH - listTop;
        if (listH < S(hwnd, 80)) listH = S(hwnd, 80);
        MoveWindow(g_hList, 0, listTop, w, listH, TRUE);
    }
    LayoutColumns(hwnd);

    /* 右格放操作提示，英文比中文长，宽按英文余量给，窄窗时左格保底 120 */
    parts[0] = w - S(hwnd, 330);
    if (parts[0] < 120) parts[0] = 120;
    parts[1] = -1;
    SendMessage(g_hStatus, SB_SETPARTS, 2, (LPARAM)parts);
}

/*
 * 静态文案集中落地：标题、查询区标签与提示文字、刷新按钮、列头。
 * 首次创建和切换语言都走这里，两处共用一份，不会漏掉某个控件。
 */
static void ApplyStaticTexts(void)
{
    LVCOLUMNW col;
    int i;

    if (!g_hwndMain || !g_hList) return;

    SetWindowTextW(g_hwndMain, Tr(TXT_WINDOW));
    SetWindowTextW(g_hLblPort, Tr(TXT_PORT));
    SetWindowTextW(g_hLblPid, Tr(TXT_PID));
    SetWindowTextW(g_hLblName, Tr(TXT_PROCESS));
    SetWindowTextW(g_hLblKey, Tr(TXT_KEYWORD));

    SendMessageW(g_hEditPort, EM_SETCUEBANNER, TRUE, (LPARAM)Tr(TXT_CUE_PORT));
    SendMessageW(g_hEditPid, EM_SETCUEBANNER, TRUE, (LPARAM)Tr(TXT_CUE_PID));
    SendMessageW(g_hEditName, EM_SETCUEBANNER, TRUE, (LPARAM)Tr(TXT_CUE_NAME));
    SendMessageW(g_hEdit, EM_SETCUEBANNER, TRUE, (LPARAM)Tr(TXT_CUE_KEY));

    ZeroMemory(&col, sizeof(col));
    col.mask = LVCF_TEXT;
    for (i = 0; i < COL_COUNT; ++i) {
        col.pszText = (LPWSTR)Tr(COL_TEXT[i]);
        ListView_SetColumn(g_hList, i, &col);
    }
}

/*
 * 切换界面语言。菜单是整条重建的（逐项改文本容易漏掉助记符），其余控件只换文字，
 * 最后重排一次布局并让列表重新取文本——状态列的文字和排序都跟语言有关。
 */
static void ApplyLanguage(int english)
{
    HMENU old;

    if (g_english == english) return;
    g_english = english;
    SaveLanguagePreference();
    if (!g_hwndMain) return;

    old = g_hMenuBar;
    g_hMenuBar = BuildMainMenu();   /* 菜单不挂窗口，全局句柄直接换，旧的自己释放 */
    if (old) DestroyMenu(old);

    ApplyStaticTexts();
    SyncFilterMenu();
    if (g_hToolbar) {
        LayoutToolbar(g_hToolbar);   /* 按钮文字宽度随语言变化，要重新量 */
        InvalidateRect(g_hToolbar, NULL, TRUE);
    }
    LayoutMain(g_hwndMain);
    ApplyView(TRUE);   /* 只换文字，不动筛选：滚动位置没必要跟着跳 */
}

static LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
    {
        LVCOLUMNW col;
        int i;

        g_hwndMain = hwnd;
        g_hFont = CreateUIFont(GetDpiOf(hwnd));

        /*
         * 文字统一由 ApplyStaticTexts 按当前语言写入，这里只建控件。
         * 标签靠右对齐并且四个共用一个宽度（见 LayoutMain），
         * 这样两行的输入框左边缘在同一条竖线上。
         */
        /* 工具条横贯顶部，先建它在最上层占住位置 */
        g_hToolbar = CreateWindowExW(0, TOOLBAR_CLASS, L"",
                                     WS_CHILD | WS_VISIBLE,
                                     0, 0, 10, 10, hwnd,
                                     (HMENU)(INT_PTR)ID_TOOLBAR, g_hInst, NULL);
        SendMessageW(g_hToolbar, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hLblPort = CreateWindowExW(0, L"Static", L"",
                                     WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE | SS_RIGHT,
                                     0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_LBL_PORT, g_hInst, NULL);
        g_hLblPid = CreateWindowExW(0, L"Static", L"",
                                    WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE | SS_RIGHT,
                                    0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_LBL_PID, g_hInst, NULL);
        g_hLblName = CreateWindowExW(0, L"Static", L"",
                                     WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE | SS_RIGHT,
                                     0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_LBL_NAME, g_hInst, NULL);
        g_hLblKey = CreateWindowExW(0, L"Static", L"",
                                    WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE | SS_RIGHT,
                                    0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_LBL_KEY, g_hInst, NULL);

        /*
         * 关键字先建：Tab 顺序跟着创建次序走，主入口要排在精确字段前面。
         * 不带任何边框样式，描边由 InitQueryEdit 挂的子类统一画。
         */
        g_hEdit = CreateWindowExW(0, L"Edit", L"",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                  0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_EDIT_FILTER,
                                  g_hInst, NULL);
        InitQueryEdit(g_hEdit, 4);

        g_hEditPort = CreateWindowExW(0, L"Edit", L"",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                      ES_AUTOHSCROLL | ES_NUMBER,
                                      0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_EDIT_PORT,
                                      g_hInst, NULL);
        InitQueryEdit(g_hEditPort, 1);

        g_hEditPid = CreateWindowExW(0, L"Edit", L"",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                     ES_AUTOHSCROLL | ES_NUMBER,
                                     0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_EDIT_PID,
                                     g_hInst, NULL);
        InitQueryEdit(g_hEditPid, 2);

        g_hEditName = CreateWindowExW(0, L"Edit", L"",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                      0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_EDIT_NAME,
                                      g_hInst, NULL);
        InitQueryEdit(g_hEditName, 3);

        g_hBtnClear = CreateWindowExW(0, CLEARBTN_CLASS, L"",
                                      WS_CHILD | WS_VISIBLE,
                                      0, 0, 10, 10, hwnd,
                                      (HMENU)(INT_PTR)ID_BTN_CLEAR, g_hInst, NULL);

        /* LVS_OWNERDATA：虚拟列表，行文本按需提供，刷新时不必逐行重建 */
        g_hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                                  LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS |
                                  LVS_OWNERDATA,
                                  0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_LIST,
                                  g_hInst, NULL);
        SetWindowTheme(g_hList, L"Explorer", NULL);
        /* 竖向网格线去掉：斑马纹已经够分行，再加竖线就成了一张表格里塞满表格线 */
        ListView_SetExtendedListViewStyle(g_hList,
                                          LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
                                          LVS_EX_LABELTIP | LVS_EX_INFOTIP);
        ListView_SetBkColor(g_hList, RGB(255, 255, 255));
        ListView_SetTextBkColor(g_hList, RGB(255, 255, 255));
        ApplyListIcons(hwnd);
        SetWindowSubclass(g_hList, ListSubclassProc, 1, 0);

        ZeroMemory(&col, sizeof(col));
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = (LPWSTR)L"";    /* 列名由 ApplyStaticTexts 按当前语言写入 */
        for (i = 0; i < COL_COUNT; ++i) {
            col.cx = S(hwnd, COL_WIDTHS[i]);
            ListView_InsertColumn(g_hList, i, &col);
        }
        /* 上次会话拖过的列宽在这里接回：逻辑像素按当前 DPI 还原为物理值 */
        for (i = 0; i < COL_COUNT; ++i) {
            if (g_colSavedW[i] > 0) {
                ListView_SetColumnWidth(g_hList, i, S(hwnd, g_colSavedW[i]));
                g_colUserSized[i] = 1;   /* 视为用户定宽,后续 LayoutColumns 不再覆盖 */
            }
        }

        g_hStatus = CreateWindowExW(0, STATUSCLASSNAMEW, L"",
                                    WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                                    0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_STATUS,
                                    g_hInst, NULL);

        EnumChildWindows(hwnd, SetFontProc, (LPARAM)g_hFont);

        /* 默认开启自动刷新，保持端口/进程实时可见 */
        g_autoOn = TRUE;
        SetTimer(hwnd, ID_TIMER, REFRESH_MS, NULL);

        /*
         * 命令行带入的初始筛选（提权重启 / 快捷方式 --port 等）：
         * 文本写进输入框走正常的防抖筛选，开关直接翻旗子。
         */
        if (g_argKey[0]) SetWindowTextW(g_hEdit, g_argKey);
        if (g_argPort[0]) SetWindowTextW(g_hEditPort, g_argPort);
        if (g_argPid[0]) SetWindowTextW(g_hEditPid, g_argPid);
        if (g_argName[0]) SetWindowTextW(g_hEditName, g_argName);
        if (g_argProto >= 0 && g_argProto <= 4) g_protoFilter = g_argProto;
        if (g_argListen >= 0) g_listenOnly = g_argListen ? 1 : 0;
        if (g_argHidesys >= 0) g_hideSystem = g_argHidesys ? 1 : 0;
        if (g_argExact >= 0) g_exactMatch = g_argExact ? 1 : 0;
        if (g_argAuto >= 0) {
            g_autoOn = g_argAuto ? TRUE : FALSE;
            if (!g_autoOn) KillTimer(hwnd, ID_TIMER);
        }

        ApplyStaticTexts();
        LayoutMain(hwnd);
        ReloadAndApply();
        return 0;
    }

    case WM_SIZE:
        LayoutMain(hwnd);
        return 0;

    /*
     * 静态控件与列表的标签统一用窗口背景。
     */
    case WM_CTLCOLORSTATIC:
        if ((HWND)lp == g_hLblPort || (HWND)lp == g_hLblPid ||
            (HWND)lp == g_hLblName || (HWND)lp == g_hLblKey) {
            /* 标签改灰：输入框的描边与内容才是主角，标签不需要抢视觉 */
            SetTextColor((HDC)wp, LABEL_TEXT_RGB);
            SetBkColor((HDC)wp, RGB(255, 255, 255));
            return (LRESULT)GetStockObject(WHITE_BRUSH);
        }
        SetTextColor((HDC)wp, RGB(31, 41, 55));
        SetBkColor((HDC)wp, GetSysColor(COLOR_WINDOW));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);

    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, RGB(17, 24, 39));
        SetBkColor((HDC)wp, RGB(255, 255, 255));
        return (LRESULT)GetStockObject(WHITE_BRUSH);

    case WM_ERASEBKGND:
    {
        RECT rc, card;
        int band = QueryBandHeight(hwnd);
        HDC hdc = (HDC)wp;
        HPEN pen;
        HGDIOBJ oldPen, oldBrush;

        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));

        /*
         * 查询区画成一张圆角卡片：白底 + 1px 浅灰描边，与工具条、列表分层。
         * 旧版三段式底色（菜单灰/查询蓝/列表蓝白）就是「色阶接近但不成体系」，
         * 改成整窗白底后分层交给描边，不再靠三档近似色硬撑。
         */
        card.left = S(hwnd, 8);
        card.right = rc.right - S(hwnd, 8);
        card.top = S(hwnd, TOOLBAR_H + 6);
        card.bottom = band - S(hwnd, 6);
        if (card.right > card.left && card.bottom > card.top) {
            pen = CreatePen(PS_SOLID, 1, CARD_BORDER);
            oldPen = SelectObject(hdc, pen);
            oldBrush = SelectObject(hdc, (HBRUSH)GetStockObject(WHITE_BRUSH));
            RoundRect(hdc, card.left, card.top, card.right, card.bottom,
                      S(hwnd, 12), S(hwnd, 12));
            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);
        }
        return 1;
    }

    case WM_DPICHANGED:
    {
        RECT *suggested = (RECT *)lp;
        if (g_hFont) DeleteObject(g_hFont);
        g_hFont = CreateUIFont(HIWORD(wp));
        EnumChildWindows(hwnd, SetFontProc, (LPARAM)g_hFont);
        ApplyListIcons(hwnd);   /* 图标格随 DPI 重建，行高跟着走 */
        SetWindowPos(hwnd, NULL, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        LayoutMain(hwnd);
        return 0;
    }

    case WM_GETMINMAXINFO:
    {
        /*
         * 最小宽度按「工具条两排按钮全显（含置顶图钉）+ 查询条件一行 +
         * 清除按钮」的实际需求定（英文按钮最宽），宁可让窗口窄不下去，
         * 也不让按钮/字段被挤没。
         */
        MINMAXINFO *mmi = (MINMAXINFO *)lp;
        mmi->ptMinTrackSize.x = S(hwnd, 1060);
        mmi->ptMinTrackSize.y = S(hwnd, 380);
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_BTN_REFRESH:
            ReloadAndApply();
            return 0;

        case IDM_AUTO:
            g_autoOn = !g_autoOn;
            if (g_autoOn) SetTimer(hwnd, ID_TIMER, REFRESH_MS, NULL);
            else KillTimer(hwnd, ID_TIMER);
            SyncFilterMenu();
            UpdateStatus();
            return 0;

        case IDM_LISTEN:
            g_listenOnly = !g_listenOnly;
            SyncFilterMenu();
            ApplyView(FALSE);
            return 0;

        case IDM_HIDESYS:
            g_hideSystem = !g_hideSystem;
            SyncFilterMenu();
            ApplyView(FALSE);
            return 0;

        case IDM_EXACT:
            g_exactMatch = !g_exactMatch;
            SyncFilterMenu();
            ApplyView(FALSE);
            return 0;

        case IDM_PROTO_ALL:
        case IDM_PROTO_TCP:
        case IDM_PROTO_UDP:
        case IDM_PROTO_V4:
        case IDM_PROTO_V6:
            /* 协议子菜单的 5 项连续编号，偏移即 MatchProto 的取值 */
            g_protoFilter = LOWORD(wp) - IDM_PROTO_ALL;
            SyncFilterMenu();
            ApplyView(FALSE);
            return 0;

        case IDM_LANG_EN:
            ApplyLanguage(1);
            return 0;

        case IDM_LANG_ZH:
            ApplyLanguage(0);
            return 0;

        case ID_EDIT_PORT:
        case ID_EDIT_PID:
        case ID_EDIT_NAME:
        case ID_EDIT_FILTER:
            /*
             * 用 lParam 区分消息来源：控件通知的 lParam 是控件句柄（非 0），
             * 加速键和菜单项发来的 lParam 是 0。拿 HIWORD(wp) == 1 当「这是加速键」
             * 是靠一个约定俗成的通知码值在猜，控件换个通知码就会串。
             */
            if (lp == 0) {                     /* Ctrl+F：聚焦过滤框并选中已有内容 */
                SetFocus(g_hEdit);
                SendMessage(g_hEdit, EM_SETSEL, 0, -1);
            } else if (HIWORD(wp) == EN_CHANGE) {
                SetTimer(hwnd, ID_FILTER_TIMER, FILTER_DEBOUNCE_MS, NULL);
            }
            return 0;

        case IDM_DETAIL:
        {
            const PORT_ENTRY *e = SelectedEntry();
            if (e) OpenDetail(hwnd, e);
            return 0;
        }

        case IDM_OPEN_LOC:
        {
            const PORT_ENTRY *e = SelectedEntry();
            if (e) {
                if (!e->procPath[0] || !ProcOpenFileLocation(hwnd, e->procPath)) {
                    MessageBoxW(hwnd, Tr(TXT_MB_OPEN_LOC_FAIL),
                                Tr(TXT_MB_HINT), MB_OK | MB_ICONWARNING);
                }
            }
            return 0;
        }

        case IDM_KILL:
        {
            const PORT_ENTRY *e = SelectedEntry();
            if (e) DoKill(hwnd, e, FALSE);
            return 0;
        }

        case IDM_KILL_TREE:
        {
            const PORT_ENTRY *e = SelectedEntry();
            if (e) DoKill(hwnd, e, TRUE);
            return 0;
        }

        case IDM_CLEAR:
            /* Esc 一键复位：文本框和结构化条件要一起清，
             * 只清文本框的话菜单里还留着勾，用户会以为没生效 */
            SetWindowTextW(g_hEditPort, L"");
            SetWindowTextW(g_hEditPid, L"");
            SetWindowTextW(g_hEditName, L"");
            SetWindowTextW(g_hEdit, L"");
            g_protoFilter = 0;
            g_listenOnly = 0;
            g_hideSystem = 0;
            g_exactMatch = 0;
            SyncFilterMenu();
            ApplyView(FALSE);
            SetFocus(g_hList);
            return 0;

        case IDM_ELEVATE:
            if (!ProcIsElevated()) ConfirmElevate(hwnd);
            return 0;

        case IDM_TOPMOST:
            g_topmost = !g_topmost;
            SetWindowPos(hwnd, g_topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                         0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            SyncFilterMenu();   /* 工具条图钉的激活态随这面旗子走 */
            return 0;

        case IDM_EXPORT:
            ExportCsv(hwnd);
            return 0;

        case IDM_COPY_PATH:
        {
            const PORT_ENTRY *e = SelectedEntry();
            if (e) CopyText(hwnd, e->procPath);
            return 0;
        }

        case IDM_COPY_PID:
        {
            const PORT_ENTRY *e = SelectedEntry();
            WCHAR t[32];
            if (e) {
                _snwprintf(t, 32, L"%u", e->pid);
                CopyText(hwnd, t);
            }
            return 0;
        }

        case IDM_COPY_ROW:
        {
            const PORT_ENTRY *e = SelectedEntry();
            WCHAR t[1024];
            if (e) {
                _snwprintf(t, 1024, L"%s\t%s:%u\t%s:%u\t%s\t%u\t%s\t%s",
                           e->proto, e->localAddr, e->localPort,
                           e->remoteAddr, e->remotePort, EntryStateText(e),
                           e->pid, e->procName, e->procPath);
                t[1023] = 0;
                CopyText(hwnd, t);
            }
            return 0;
        }

        case IDM_FILTER_SEL:
        {
            const PORT_ENTRY *e = SelectedEntry();
            if (e) {
                SetWindowTextW(g_hEdit, e->procName);
                ApplyView(FALSE);
            }
            return 0;
        }
        }
        break;

    case WM_CONTEXTMENU:
        /* 鼠标右键已经由 NM_RCLICK 弹出菜单；这里只接键盘菜单键（坐标为 -1,-1）。 */
        if ((HWND)wp == g_hList && lp == (LPARAM)-1) {
            POINT pt = { 0, 0 };
            int item = ListView_GetNextItem(g_hList, -1, LVNI_SELECTED);
            ClientToScreen(g_hList, &pt);
            ShowContextMenu(hwnd, item, pt.x, pt.y);
            return 0;
        }
        break;

    case WM_NOTIFY:
    {
        NMHDR *hdr = (NMHDR *)lp;
        if (hdr->idFrom == ID_LIST) {
            if (hdr->code == NM_CUSTOMDRAW) {
                NMLVCUSTOMDRAW *cd = (NMLVCUSTOMDRAW *)lp;

                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT)
                    return CDRF_NOTIFYSUBITEMDRAW;

                if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    ApplyRowBg(cd);
                    return CDRF_NOTIFYSUBITEMDRAW;   /* 逐子项通知：占位符弱化 + 状态徽章 */
                }

                if (cd->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
                    if (cd->iSubItem == COL_STATE &&
                        (size_t)cd->nmcd.dwItemSpec < g_viewCount) {
                        int row = (int)cd->nmcd.dwItemSpec;
                        BOOL sel = (ListView_GetItemState(g_hList, row,
                                                          LVIS_SELECTED) & LVIS_SELECTED) != 0;

                        /*
                         * 全部行（含选中/悬停）都整格自绘，徽章形态全程稳定——
                         * 交还系统的尝试已否决：悬停时徽章会退化成纯文字。
                         * 选中底色用取样校准的常量（默认 accent 下与系统选中
                         * 色一致，见 SEL_ROW_BG）。
                         */
                        DrawStateCell(cd->nmcd.hdc, cd->nmcd.rc,
                                      &g_view[cd->nmcd.dwItemSpec],
                                      cd->nmcd.dwItemSpec, sel);
                        return CDRF_SKIPDEFAULT;
                    }
                    ApplyRowBg(cd);
                    /* 非状态列必须显式恢复默认色：CDRF_NEWFONT 的颜色会串到后面的子项 */
                    cd->clrText = CLR_DEFAULT;
                    if ((size_t)cd->nmcd.dwItemSpec < g_viewCount) {
                        const PORT_ENTRY *e = &g_view[cd->nmcd.dwItemSpec];
                        if (cd->iSubItem == COL_RADDR && !e->remoteAddr[0]) {
                            /* UDP/监听行没有远程端点，「—」占位符用浅灰弱化 */
                            cd->clrText = RGB(182, 191, 204);
                        }
                    }
                    return CDRF_NEWFONT;
                }
                return CDRF_DODEFAULT;
            }
            if (hdr->code == LVN_GETDISPINFO) {
                NMLVDISPINFOW *di = (NMLVDISPINFOW *)lp;
                int item = di->item.iItem;

                if ((di->item.mask & LVIF_IMAGE) &&
                    item >= 0 && (size_t)item < g_viewCount) {
                    di->item.iImage = IconIndexFor(&g_view[item]);
                }
                if ((di->item.mask & LVIF_TEXT) && di->item.pszText) {
                    if (item >= 0 && (size_t)item < g_viewCount) {
                        wcsncpy(di->item.pszText,
                                CellText(&g_view[item], di->item.iSubItem),
                                di->item.cchTextMax - 1);
                        di->item.pszText[di->item.cchTextMax - 1] = 0;
                    } else {
                        di->item.pszText[0] = 0;
                    }
                }
                return 0;
            }
            if (hdr->code == LVN_GETINFOTIPW) {
                /*
                 * 行悬停信息：服务名（读系统 services 表，443→https 这种）+
                 * 一行概要 + 映像路径。LVS_EX_INFOTIP 接管后 LABELTIP 不再
                 * 单独工作，长路径在这里补上。
                 */
                NMLVGETINFOTIPW *tip = (NMLVGETINFOTIPW *)lp;
                int item = tip->iItem;

                if (item >= 0 && (size_t)item < g_viewCount) {
                    const PORT_ENTRY *e = &g_view[item];
                    WCHAR line[1024], tmp[512];
                    const char *proto = (_wcsicmp(e->proto, L"UDP") == 0 ||
                                         _wcsicmp(e->proto, L"UDP6") == 0) ? "udp" : "tcp";
                    SERVENT *se = getservbyport(htons((unsigned short)(e->localPort & 0xFFFF)), proto);

                    line[0] = 0;
                    if (se && se->s_name) {
                        WCHAR srvName[64];
                        srvName[0] = 0;
                        MultiByteToWideChar(CP_UTF8, 0, se->s_name, -1, srvName, 64);
                        if (!srvName[0])
                            MultiByteToWideChar(CP_ACP, 0, se->s_name, -1, srvName, 64);
                        _snwprintf(tmp, 512, Tr(TXT_TIP_SRV), srvName);
                        tmp[511] = 0;
                        wcscat_s(line, 1024, tmp);
                        wcscat_s(line, 1024, L"\n");
                    }
                    _snwprintf(tmp, 512, L"%s %s · %s · PID %s",
                               e->proto, CellText(e, COL_LADDR),
                               EntryStateText(e)[0] ? EntryStateText(e) : e->proto,
                               e->pidText);
                    tmp[511] = 0;
                    wcscat_s(line, 1024, tmp);
                    if (e->procPath[0]) {
                        wcscat_s(line, 1024, L"\n");
                        wcscat_s(line, 1024, e->procPath);
                    } else {
                        wcscat_s(line, 1024, L"\n");
                        wcscat_s(line, 1024, Tr(TXT_TIP_NOPATH));
                    }
                    _snwprintf(tip->pszText, tip->cchTextMax, L"%s", line);
                    tip->pszText[tip->cchTextMax - 1] = 0;
                }
                return 0;
            }
            if (hdr->code == LVN_COLUMNCLICK) {
                int col = ((NMLISTVIEW *)lp)->iSubItem;
                if (col == g_sortCol) {
                    g_sortAsc = !g_sortAsc;
                } else {
                    g_sortCol = col;
                    g_sortAsc = 1;
                }
                ApplyView(FALSE);
                return 0;
            }
            if (hdr->code == NM_DBLCLK) {
                const PORT_ENTRY *e = SelectedEntry();
                if (e) OpenDetail(hwnd, e);
                return 0;
            }
            if (hdr->code == NM_RCLICK) {
                NMITEMACTIVATE *ia = (NMITEMACTIVATE *)lp;
                POINT pt;
                GetCursorPos(&pt);
                ShowContextMenu(hwnd, ia->iItem, pt.x, pt.y);
                return 0;
            }
        }
        if (hdr->hwndFrom == ListView_GetHeader(g_hList) &&
            hdr->code == NM_CUSTOMDRAW) {
            NMCUSTOMDRAW *nmc = (NMCUSTOMDRAW *)lp;
            if (nmc->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (nmc->dwDrawStage == CDDS_ITEMPREPAINT) {
                DrawFlatHeader(nmc, g_sortCol, g_sortAsc);
                return CDRF_SKIPDEFAULT;
            }
            return CDRF_DODEFAULT;
        }
        if (hdr->hwndFrom == ListView_GetHeader(g_hList) &&
            (hdr->code == HDN_ENDTRACKW || hdr->code == HDN_DIVIDERDBLCLICKW)) {
            NMHEADERW *header = (NMHEADERW *)lp;
            if (header->iItem >= 0 && header->iItem < COL_COUNT)
                g_colUserSized[header->iItem] = 1;
            return 0;
        }
        break;
    }

    case WM_TIMER:
        if (wp == ID_FILTER_TIMER) {
            KillTimer(hwnd, ID_FILTER_TIMER);
            ApplyView(FALSE);
        } else if (wp == ID_TIMER) {
            ReloadAndApply();
        } else if (wp == ID_HL_TIMER) {
            InvalidateRect(g_hList, NULL, FALSE);
            ManageHighlightTimer();   /* 高亮全部褪色后自动停表 */
        } else if (wp == ID_KILL_TIMER) {
            SendMessageW(g_hStatus, SB_SETTEXTW, 0, (LPARAM)Tr(TXT_BAR_KILLING));
        }
        return 0;

    case WM_APP_REFRESH:
        ReloadAndApply();
        return 0;

    case WM_APP_PORTS:
        ApplyPortsResult((PORTS_RESULT *)lp);
        return 0;

    case WM_APP_KILL:
        FinishKill((KILL_REQUEST *)lp);
        return 0;

    case WM_DESTROY:
        DiscardQueuedWork(hwnd);
        KillTimer(hwnd, ID_TIMER);
        KillTimer(hwnd, ID_FILTER_TIMER);
        KillTimer(hwnd, ID_KILL_TIMER);
        KillTimer(hwnd, ID_HL_TIMER);
        SaveUiPrefs();
        WSACleanup();
        free(g_all);
        free(g_view);
        g_all = NULL;
        g_view = NULL;
        if (g_hFont) DeleteObject(g_hFont);
        if (g_hIcons) { ImageList_Destroy(g_hIcons); g_hIcons = NULL; }
        if (g_hMenuBar) { DestroyMenu(g_hMenuBar); g_hMenuBar = NULL; }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}


int UiRun(HINSTANCE hInst, int nCmdShow)
{
    WNDCLASSEXW wc, wcd;
    HWND hwnd;
    MSG msg;
    HACCEL hAccel;
    ACCEL accels[10];
    UINT dpi;
    RECT savedRc;
    BOOL startMaximized = FALSE;
    WSADATA wsa;

    g_hInst = hInst;
    LoadLanguagePreference();
    ParseCommandLineArgs();
    LoadUiPrefs(&savedRc, &startMaximized);
    /* 服务名提示用 getservbyport，失败只影响这一项提示，不阻塞启动 */
    WSAStartup(MAKEWORD(2, 2), &wsa);

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = (HICON)LoadImage(g_hInst, MAKEINTRESOURCE(IDI_APPICON), IMAGE_ICON,
                                GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    wc.hIconSm = (HICON)LoadImage(g_hInst, MAKEINTRESOURCE(IDI_APPICON), IMAGE_ICON,
                                  GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = MAIN_CLASS;
    if (!RegisterClassExW(&wc)) return 1;

    ZeroMemory(&wcd, sizeof(wcd));
    wcd.cbSize = sizeof(wcd);
    wcd.style = CS_HREDRAW | CS_VREDRAW;
    wcd.lpfnWndProc = DetailProc;
    wcd.hInstance = hInst;
    wcd.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcd.hIcon = (HICON)LoadImage(g_hInst, MAKEINTRESOURCE(IDI_APPICON), IMAGE_ICON,
                                 GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    wcd.hIconSm = (HICON)LoadImage(g_hInst, MAKEINTRESOURCE(IDI_APPICON), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wcd.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcd.lpszClassName = DETAIL_CLASS;
    if (!RegisterClassExW(&wcd)) return 1;

    dpi = 96;
    {
        typedef UINT (WINAPI *PFN)(void);
        PFN p = (PFN)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForSystem");
        if (p) dpi = p();
    }

    if (!RegisterToolbar(hInst)) return 1;
    if (!RegisterClearBtn(hInst)) return 1;

    g_hMenuBar = BuildMainMenu();

    hwnd = CreateWindowExW(0, MAIN_CLASS, Tr(TXT_WINDOW),
                           WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                           savedRc.left >= 0 ? savedRc.left : CW_USEDEFAULT,
                           savedRc.top >= 0 ? savedRc.top : CW_USEDEFAULT,
                           savedRc.left >= 0 ? savedRc.right - savedRc.left : MulDiv(980, (int)dpi, 96),
                           savedRc.top >= 0 ? savedRc.bottom - savedRc.top : MulDiv(620, (int)dpi, 96),
                           NULL, NULL, hInst, NULL);
    if (!hwnd) {
        if (g_hMenuBar) DestroyMenu(g_hMenuBar);
        return 1;
    }

    SyncFilterMenu();

    ShowWindow(hwnd, startMaximized ? SW_SHOWMAXIMIZED : nCmdShow);
    UpdateWindow(hwnd);

    /* 上次退出时开着置顶，这次继续钉在最前 */
    if (g_topmost) {
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    accels[0].fVirt = FVIRTKEY;
    accels[0].key = VK_F5;
    accels[0].cmd = ID_BTN_REFRESH;
    accels[1].fVirt = FVIRTKEY | FCONTROL;
    accels[1].key = 'F';
    accels[1].cmd = ID_EDIT_FILTER;

    /* 工具类软件的常用操作交给键盘：查看详情、结束进程、复制行都要能一按到底 */
    accels[2].fVirt = FVIRTKEY;
    accels[2].key = VK_ESCAPE;
    accels[2].cmd = IDM_CLEAR;

    accels[3].fVirt = FVIRTKEY | FCONTROL | FSHIFT;
    accels[3].key = 'E';
    accels[3].cmd = IDM_ELEVATE;

    accels[4].fVirt = FVIRTKEY | FCONTROL | FSHIFT;
    accels[4].key = 'R';
    accels[4].cmd = IDM_AUTO;

    accels[5].fVirt = FVIRTKEY | FCONTROL;
    accels[5].key = 'S';
    accels[5].cmd = IDM_EXPORT;

    hAccel = CreateAcceleratorTableW(accels, 6);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        HWND focus = GetFocus();
        BOOL listKey = focus == g_hList && msg.message == WM_KEYDOWN;
        if (listKey && msg.wParam == VK_RETURN) {
            SendMessageW(hwnd, WM_COMMAND, IDM_DETAIL, 0);
            continue;
        }
        if (listKey && msg.wParam == VK_DELETE) {
            SendMessageW(hwnd, WM_COMMAND, IDM_KILL, 0);
            continue;
        }
        if (listKey && msg.wParam == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            SendMessageW(hwnd, WM_COMMAND, IDM_COPY_ROW, 0);
            continue;
        }
        if (listKey && msg.wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            SendMessageW(hwnd, WM_COMMAND, IDM_COPY_PATH, 0);
            continue;
        }
        /* 菜单栏撤掉后，Alt+F / Alt+L 两个助记键由这里接住，行为不变 */
        if (msg.message == WM_SYSKEYDOWN &&
            (msg.lParam & (1 << 29)) &&
            (msg.wParam == 'F' || msg.wParam == 'L')) {
            ToolbarPopup(msg.wParam == 'F' ? TBB_FILTER : TBB_LANG);
            continue;
        }
        if (!hAccel || !TranslateAcceleratorW(hwnd, hAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (hAccel) DestroyAcceleratorTable(hAccel);
    return (int)msg.wParam;
}
