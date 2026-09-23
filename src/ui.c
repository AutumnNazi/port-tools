/* 界面：端口列表、过滤/排序、右键菜单、进程详情窗口 */
#include "portview.h"

#include <uxtheme.h>
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

#define MAIN_CLASS   L"PortViewMainWindow"
#define DETAIL_CLASS L"PortViewDetailWindow"

#define ID_EDIT_FILTER 1001
#define ID_BTN_REFRESH 1002
#define ID_CHK_AUTO    1003
#define ID_BTN_ADMIN   1004
#define ID_LIST        1005
#define ID_STATUS      1006
#define ID_TIMER       1007

#define IDM_OPEN_LOC   2001
#define IDM_DETAIL     2002
#define IDM_KILL       2003
#define IDM_KILL_TREE  2004
#define IDM_COPY_PATH  2005
#define IDM_COPY_PID   2006
#define IDM_COPY_ROW   2007
#define IDM_FILTER_SEL 2008

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

#define WM_APP_REFRESH (WM_APP + 1)
#define WM_APP_KILLED  (WM_APP + 2)

enum {
    COL_PROTO = 0, COL_LADDR, COL_LPORT, COL_RADDR,
    COL_RPORT, COL_STATE, COL_PID, COL_NAME, COL_PATH, COL_COUNT
};

static const WCHAR *COL_TITLES[COL_COUNT] = {
    L"协议", L"本地地址", L"本地端口", L"远程地址", L"远程端口",
    L"状态", L"PID", L"进程", L"映像路径"
};

static const int COL_WIDTHS[COL_COUNT] = {
    56, 140, 74, 140, 74, 92, 62, 130, 320
};

static HINSTANCE g_hInst;
static HWND g_hwndMain;
static HWND g_hList;
static HWND g_hEdit;
static HWND g_hBtnRefresh;
static HWND g_hChkAuto;
static HWND g_hBtnAdmin;
static HWND g_hStatus;
static HFONT g_hFont;

static PORT_ENTRY *g_all = NULL;
static size_t g_allCount = 0;
static PORT_ENTRY *g_view = NULL;
static size_t g_viewCount = 0;

static int g_sortCol = COL_LPORT;
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
    typedef BOOL (WINAPI *PFN_SPID)(UINT, UINT, PVOID, UINT);
    PFN_SPID pSpiDpi;
    BOOL ok = FALSE;

    ZeroMemory(&ncm, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);

    pSpiDpi = (PFN_SPID)GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                       "SystemParametersInfoForDpi");
    if (pSpiDpi) {
        ok = pSpiDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
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

/* -------------------------------------------------------------- 数据 */

static void EntryKey(const PORT_ENTRY *e, WCHAR *buf, size_t cch)
{
    _snwprintf(buf, cch, L"%s|%s|%u|%s|%u|%u",
               e->proto, e->localAddr, e->localPort,
               e->remoteAddr, e->remotePort, e->pid);
    buf[cch - 1] = 0;
}

static BOOL MatchFilter(const PORT_ENTRY *e, const WCHAR *key)
{
    WCHAR buf[1024];
    if (!key || !key[0]) return TRUE;

    _snwprintf(buf, 1024, L"%s %s %u %s %u %s %u %s %s",
               e->proto, e->localAddr, e->localPort,
               e->remoteAddr, e->remotePort, e->state,
               e->pid, e->procName, e->procPath);
    buf[1023] = 0;

    return ContainsI(buf, key);
}

static int CmpEntry(const void *pa, const void *pb)
{
    const PORT_ENTRY *a = (const PORT_ENTRY *)pa;
    const PORT_ENTRY *b = (const PORT_ENTRY *)pb;
    int r = 0;

    switch (g_sortCol) {
    case COL_PROTO: r = _wcsicmp(a->proto, b->proto); break;
    case COL_LADDR: r = _wcsicmp(a->localAddr, b->localAddr); break;
    case COL_LPORT: r = (int)a->localPort - (int)b->localPort; break;
    case COL_RADDR: r = _wcsicmp(a->remoteAddr, b->remoteAddr); break;
    case COL_RPORT: r = (int)a->remotePort - (int)b->remotePort; break;
    case COL_STATE: r = _wcsicmp(a->state, b->state); break;
    case COL_PID:   r = (int)a->pid - (int)b->pid; break;
    case COL_NAME:  r = _wcsicmp(a->procName, b->procName); break;
    case COL_PATH:  r = _wcsicmp(a->procPath, b->procPath); break;
    default: break;
    }

    if (r == 0) r = (int)a->localPort - (int)b->localPort;
    if (r == 0) r = _wcsicmp(a->proto, b->proto);
    return g_sortAsc ? r : -r;
}

static void UpdateStatus(void)
{
    WCHAR text[320];
    SYSTEMTIME st;
    size_t listen = 0, i;

    for (i = 0; i < g_allCount; ++i) {
        if (_wcsicmp(g_all[i].state, L"监听") == 0) listen++;
    }

    GetLocalTime(&st);
    _snwprintf(text, 320, L"共 %u 条连接 · 监听端口 %u 个 · 显示 %u 条 · %02d:%02d:%02d",
               (unsigned)g_allCount, (unsigned)listen, (unsigned)g_viewCount,
               st.wHour, st.wMinute, st.wSecond);
    text[319] = 0;

    SendMessageW(g_hStatus, SB_SETTEXTW, 0, (LPARAM)text);
    SendMessageW(g_hStatus, SB_SETTEXTW, 1,
                 (LPARAM)(ProcIsElevated() ? L"管理员" : L"标准用户（部分进程受限）"));
}

static void ApplyView(void)
{
    WCHAR selKey[256], key[256];
    WCHAR filter[256];
    size_t i, n = 0;
    int sel = -1, top = 0, newSel = -1;
    LVITEMW it;

    /* 记住当前选中项与滚动位置 */
    top = ListView_GetTopIndex(g_hList);
    sel = ListView_GetNextItem(g_hList, -1, LVNI_SELECTED);
    if (sel >= 0 && (size_t)sel < g_viewCount) {
        EntryKey(&g_view[sel], selKey, 256);
    } else {
        selKey[0] = 0;
    }

    GetWindowTextW(g_hEdit, filter, 256);
    filter[255] = 0;

    free(g_view);
    g_view = NULL;
    g_viewCount = 0;

    if (g_allCount) {
        g_view = (PORT_ENTRY *)malloc(g_allCount * sizeof(PORT_ENTRY));
        if (g_view) {
            for (i = 0; i < g_allCount; ++i) {
                if (MatchFilter(&g_all[i], filter)) g_view[n++] = g_all[i];
            }
            if (n) qsort(g_view, n, sizeof(PORT_ENTRY), CmpEntry);
        }
    }
    g_viewCount = n;

    SendMessage(g_hList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_hList);

    for (i = 0; i < g_viewCount; ++i) {
        PORT_ENTRY *e = &g_view[i];
        WCHAR num[32];

        EntryKey(e, key, 256);
        if (selKey[0] && _wcsicmp(key, selKey) == 0) newSel = (int)i;

        ZeroMemory(&it, sizeof(it));
        it.mask = LVIF_TEXT;
        it.iItem = (int)i;
        it.iSubItem = 0;
        it.pszText = e->proto;
        ListView_InsertItem(g_hList, &it);

        ListView_SetItemText(g_hList, (int)i, COL_LADDR, e->localAddr);
        _snwprintf(num, 32, L"%u", e->localPort);
        ListView_SetItemText(g_hList, (int)i, COL_LPORT, num);
        ListView_SetItemText(g_hList, (int)i, COL_RADDR, e->remoteAddr);
        if (e->remotePort || e->remoteAddr[0]) {
            _snwprintf(num, 32, L"%u", e->remotePort);
        } else {
            num[0] = 0;
        }
        ListView_SetItemText(g_hList, (int)i, COL_RPORT, num);
        ListView_SetItemText(g_hList, (int)i, COL_STATE, e->state);
        _snwprintf(num, 32, L"%u", e->pid);
        ListView_SetItemText(g_hList, (int)i, COL_PID, num);
        ListView_SetItemText(g_hList, (int)i, COL_NAME, e->procName);
        ListView_SetItemText(g_hList, (int)i, COL_PATH, e->procPath);
    }

    if (newSel >= 0) {
        ListView_SetItemState(g_hList, newSel, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(g_hList, newSel, FALSE);
    } else if (top > 0 && g_viewCount) {
        if (top >= (int)g_viewCount) top = (int)g_viewCount - 1;
        ListView_EnsureVisible(g_hList, top, TRUE);
    }

    SendMessage(g_hList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_hList, NULL, TRUE);
    UpdateStatus();
}

static void ReloadAndApply(void)
{
    PORT_ENTRY *list = NULL;
    size_t count = 0;

    PortsEnumerate(&list, &count);

    free(g_all);
    g_all = list;
    g_allCount = count;

    ApplyView();
}

static const PORT_ENTRY *SelectedEntry(void)
{
    int i = ListView_GetNextItem(g_hList, -1, LVNI_SELECTED);
    if (i < 0 || (size_t)i >= g_viewCount) return NULL;
    return &g_view[i];
}

/* ------------------------------------------------------------ 操作 */

static void DoKill(HWND hwnd, DWORD pid, const WCHAR *name, BOOL tree)
{
    WCHAR msg[512], title[128];
    int r;
    BOOL ok;

    if (pid == 0 || pid == 4) {
        MessageBoxW(hwnd, L"该系统进程无法结束。", L"无法结束", MB_OK | MB_ICONWARNING);
        return;
    }

    wcsncpy(title, tree ? L"结束进程树" : L"结束进程", 127);
    title[127] = 0;

    _snwprintf(msg, 512,
               tree ? L"确定结束进程 %s（PID %u）及其所有子进程吗？\n未保存的数据将丢失。"
                    : L"确定结束进程 %s（PID %u）吗？\n未保存的数据将丢失。",
               name, pid);

    r = MessageBoxW(hwnd, msg, title, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (r != IDYES) return;

    ok = tree ? ProcTerminateTree(pid) : ProcTerminate(pid);

    if (!ok) {
        DWORD err = GetLastError();
        _snwprintf(msg, 512, L"结束失败（错误 %u）。\n如果是系统或其他用户的进程，请以管理员身份运行本工具。",
                   err);
        MessageBoxW(hwnd, msg, L"失败", MB_OK | MB_ICONERROR);
    }

    PostMessage(g_hwndMain, WM_APP_REFRESH, 0, 0);
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
    pt.x = x;
    pt.y = y;
    ClientToScreen(hwnd, &pt);

    menu = CreatePopupMenu();
    if (!menu) return;

    if (e) {
        AppendMenuW(menu, MF_STRING, IDM_DETAIL, L"查看进程详情(&D)");
        AppendMenuW(menu, MF_STRING, IDM_OPEN_LOC, L"打开文件所在位置(&O)");
        AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(menu, MF_STRING, IDM_KILL, L"结束进程(&K)");
        AppendMenuW(menu, MF_STRING, IDM_KILL_TREE, L"结束进程树(&T)");
        AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(menu, MF_STRING, IDM_COPY_PATH, L"复制映像路径(&C)");
        AppendMenuW(menu, MF_STRING, IDM_COPY_PID, L"复制 PID");
        AppendMenuW(menu, MF_STRING, IDM_COPY_ROW, L"复制整行");
        AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(menu, MF_STRING, IDM_FILTER_SEL, L"按该进程名过滤(&F)");
    }
    AppendMenuW(menu, MF_STRING, ID_BTN_REFRESH, L"刷新(&R)\tF5");

    TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(menu);
}

/* ------------------------------------------------------ 进程详情窗口 */

typedef struct {
    PORT_ENTRY entry;
    HFONT hFont;
} DETAIL_CTX;

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
        WCHAR msg[] = L"无法读取模块列表（需要更高权限，或进程已退出）";
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
        const WCHAR *titles[4] = { L"模块", L"文件路径", L"基址", L"大小" };
        const int widths[4] = { 150, 300, 110, 80 };

        ctx = (DETAIL_CTX *)malloc(sizeof(DETAIL_CTX));
        if (!ctx) return -1;
        ctx->entry = *pe;
        ctx->hFont = CreateUIFont(GetDpiOf(hwnd));
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)ctx);

        _snwprintf(buf, 512, L"%s  (PID %u)", ctx->entry.procName, ctx->entry.pid);
        h = CreateWindowExW(0, L"Static", buf, WS_CHILD | WS_VISIBLE | SS_LEFT,
                            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)D_ST_NAME, g_hInst, NULL);
        SendMessage(h, WM_SETFONT, (WPARAM)ctx->hFont, TRUE);

        h = CreateWindowExW(0, L"Static", L"映像路径：", WS_CHILD | WS_VISIBLE | SS_LEFT,
                            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)D_ST_PATH, g_hInst, NULL);
        SendMessage(h, WM_SETFONT, (WPARAM)ctx->hFont, TRUE);

        h = CreateWindowExW(WS_EX_CLIENTEDGE, L"Edit", ctx->entry.procPath,
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_READONLY | ES_AUTOHSCROLL,
                            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)D_ED_PATH, g_hInst, NULL);
        SendMessage(h, WM_SETFONT, (WPARAM)ctx->hFont, TRUE);

        h = CreateWindowExW(0, L"Static", L"命令行：", WS_CHILD | WS_VISIBLE | SS_LEFT,
                            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)D_ST_CMD, g_hInst, NULL);
        SendMessage(h, WM_SETFONT, (WPARAM)ctx->hFont, TRUE);

        buf[0] = 0;
        if (!ProcGetCommandLine(ctx->entry.pid, buf, 512)) {
            wcsncpy(buf, L"（不可用）", 511);
            buf[511] = 0;
        }
        h = CreateWindowExW(WS_EX_CLIENTEDGE, L"Edit", buf,
                            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP |
                            ES_READONLY | ES_MULTILINE | ES_AUTOVSCROLL,
                            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)D_ED_CMD, g_hInst, NULL);
        SendMessage(h, WM_SETFONT, (WPARAM)ctx->hFont, TRUE);

        h = CreateWindowExW(0, L"Static", L"已加载模块（关联文件）：",
                            WS_CHILD | WS_VISIBLE | SS_LEFT,
                            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)D_ST_MOD, g_hInst, NULL);
        SendMessage(h, WM_SETFONT, (WPARAM)ctx->hFont, TRUE);

        h = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT |
                            LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)D_LIST_MOD, g_hInst, NULL);
        SetWindowTheme(h, L"Explorer", NULL);
        ListView_SetExtendedListViewStyle(h, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        SendMessage(h, WM_SETFONT, (WPARAM)ctx->hFont, TRUE);

        ZeroMemory(&col, sizeof(col));
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        for (i = 0; i < 4; ++i) {
            col.pszText = (LPWSTR)titles[i];
            col.cx = S(hwnd, widths[i]);
            ListView_InsertColumn(h, i, &col);
        }
        FillModules(h, ctx->entry.pid);

        h = CreateWindowExW(0, L"Button", L"打开所在目录",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)D_BTN_LOC, g_hInst, NULL);
        SendMessage(h, WM_SETFONT, (WPARAM)ctx->hFont, TRUE);

        h = CreateWindowExW(0, L"Button", L"结束进程",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)D_BTN_KILL, g_hInst, NULL);
        SendMessage(h, WM_SETFONT, (WPARAM)ctx->hFont, TRUE);

        h = CreateWindowExW(0, L"Button", L"重新加载",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)D_BTN_RELOAD, g_hInst, NULL);
        SendMessage(h, WM_SETFONT, (WPARAM)ctx->hFont, TRUE);

        h = CreateWindowExW(0, L"Button", L"关闭",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)D_BTN_CLOSE, g_hInst, NULL);
        SendMessage(h, WM_SETFONT, (WPARAM)ctx->hFont, TRUE);

        SendMessage(hwnd, WM_SIZE, 0, 0);
        return 0;
    }

    case WM_SIZE:
    {
        RECT rc;
        int w, h, pad, y, bh, bw, x;

        GetClientRect(hwnd, &rc);
        w = rc.right;
        h = rc.bottom;
        pad = S(hwnd, 10);
        bh = S(hwnd, 28);
        bw = S(hwnd, 110);

        MoveCtl(hwnd, D_ST_NAME, pad, pad, w - pad * 2, S(hwnd, 20));
        MoveCtl(hwnd, D_ST_PATH, pad, pad + S(hwnd, 22), w - pad * 2, S(hwnd, 18));
        MoveCtl(hwnd, D_ED_PATH, pad, pad + S(hwnd, 40), w - pad * 2, S(hwnd, 24));
        MoveCtl(hwnd, D_ST_CMD, pad, pad + S(hwnd, 66), w - pad * 2, S(hwnd, 18));
        MoveCtl(hwnd, D_ED_CMD, pad, pad + S(hwnd, 84), w - pad * 2, S(hwnd, 58));
        MoveCtl(hwnd, D_ST_MOD, pad, pad + S(hwnd, 144), w - pad * 2, S(hwnd, 18));

        y = pad + S(hwnd, 162);
        MoveCtl(hwnd, D_LIST_MOD, pad, y, w - pad * 2, h - y - pad - bh - S(hwnd, 8));

        x = w - pad;
        x -= bw;
        MoveCtl(hwnd, D_BTN_CLOSE, x, h - pad - bh, bw, bh);
        x -= bw + S(hwnd, 8);
        MoveCtl(hwnd, D_BTN_RELOAD, x, h - pad - bh, bw, bh);
        x -= bw + S(hwnd, 8);
        MoveCtl(hwnd, D_BTN_KILL, x, h - pad - bh, bw, bh);
        x -= bw + S(hwnd, 8);
        MoveCtl(hwnd, D_BTN_LOC, x, h - pad - bh, bw, bh);
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case D_BTN_LOC:
            if (ctx && ctx->entry.procPath[0]) {
                if (!ProcOpenFileLocation(hwnd, ctx->entry.procPath)) {
                    MessageBoxW(hwnd, L"无法打开该位置。", L"提示", MB_OK | MB_ICONWARNING);
                }
            } else {
                MessageBoxW(hwnd, L"该进程的映像路径不可用（权限不足）。", L"提示", MB_OK | MB_ICONWARNING);
            }
            return 0;

        case D_BTN_KILL:
            if (ctx) {
                DoKill(hwnd, ctx->entry.pid, ctx->entry.procName, FALSE);
            }
            return 0;

        case D_BTN_RELOAD:
            if (ctx) FillModules(GetDlgItem(hwnd, D_LIST_MOD), ctx->entry.pid);
            return 0;

        case D_BTN_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_NOTIFY:
        if (LOWORD(wp) == D_LIST_MOD && ((NMHDR *)lp)->code == NM_DBLCLK) {
            int item = ListView_GetNextItem(GetDlgItem(hwnd, D_LIST_MOD), -1, LVNI_SELECTED);
            if (item >= 0) {
                WCHAR path[MAX_PATH];
                ListView_GetItemText(GetDlgItem(hwnd, D_LIST_MOD), item, 1, path, MAX_PATH);
                ProcOpenFileLocation(hwnd, path);
            }
            return 0;
        }
        break;

    case WM_DESTROY:
        if (ctx) {
            if (ctx->hFont) DeleteObject(ctx->hFont);
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

    hwnd = CreateWindowExW(0, DETAIL_CLASS, L"进程详情",
                           WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                           CW_USEDEFAULT, CW_USEDEFAULT,
                           S(parent, 760), S(parent, 520),
                           parent, NULL, g_hInst, &copy);
    if (!hwnd) return;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
}

/* ---------------------------------------------------------- 主窗口 */

static void LayoutMain(HWND hwnd)
{
    RECT rc, rs;
    int w, h, pad, bh, sbH, x;
    int parts[2];

    if (!g_hList) return;

    GetClientRect(hwnd, &rc);
    w = rc.right;
    h = rc.bottom;
    pad = S(hwnd, 8);
    bh = S(hwnd, 26);

    SendMessage(g_hStatus, WM_SIZE, 0, 0);
    GetWindowRect(g_hStatus, &rs);
    sbH = rs.bottom - rs.top;

    x = pad;
    MoveWindow(g_hEdit, x, S(hwnd, 6), S(hwnd, 260), bh, TRUE);
    x += S(hwnd, 260) + S(hwnd, 6);
    MoveWindow(g_hBtnRefresh, x, S(hwnd, 6), S(hwnd, 86), bh, TRUE);
    x += S(hwnd, 86) + S(hwnd, 8);
    MoveWindow(g_hChkAuto, x, S(hwnd, 9), S(hwnd, 140), bh, TRUE);
    MoveWindow(g_hBtnAdmin, w - pad - S(hwnd, 180), S(hwnd, 6), S(hwnd, 180), bh, TRUE);

    MoveWindow(g_hList, 0, S(hwnd, 38), w, h - S(hwnd, 38) - sbH, TRUE);

    parts[0] = w - S(hwnd, 220);
    if (parts[0] < 120) parts[0] = 120;
    parts[1] = -1;
    SendMessage(g_hStatus, SB_SETPARTS, 2, (LPARAM)parts);
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

        g_hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"Edit", L"",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                  0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_EDIT_FILTER,
                                  g_hInst, NULL);
        SendMessage(g_hEdit, EM_SETCUEBANNER, TRUE,
                    (LPARAM)L"按端口 / PID / 进程名 / 路径过滤…");

        g_hBtnRefresh = CreateWindowExW(0, L"Button", L"刷新 (F5)",
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                        0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_BTN_REFRESH,
                                        g_hInst, NULL);

        g_hChkAuto = CreateWindowExW(0, L"Button", L"自动刷新 (3s)",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                     0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_CHK_AUTO,
                                     g_hInst, NULL);

        g_hBtnAdmin = CreateWindowExW(0, L"Button",
                                      ProcIsElevated() ? L"✓ 已以管理员运行" : L"以管理员身份重启",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                      0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_BTN_ADMIN,
                                      g_hInst, NULL);
        if (ProcIsElevated()) EnableWindow(g_hBtnAdmin, FALSE);

        g_hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                                  LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                  0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_LIST,
                                  g_hInst, NULL);
        SetWindowTheme(g_hList, L"Explorer", NULL);
        ListView_SetExtendedListViewStyle(g_hList,
                                          LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
                                          LVS_EX_LABELTIP);

        ZeroMemory(&col, sizeof(col));
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        for (i = 0; i < COL_COUNT; ++i) {
            col.pszText = (LPWSTR)COL_TITLES[i];
            col.cx = S(hwnd, COL_WIDTHS[i]);
            ListView_InsertColumn(g_hList, i, &col);
        }

        g_hStatus = CreateWindowExW(0, STATUSCLASSNAMEW, L"",
                                    WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                                    0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_STATUS,
                                    g_hInst, NULL);

        EnumChildWindows(hwnd, SetFontProc, (LPARAM)g_hFont);

        LayoutMain(hwnd);
        ReloadAndApply();
        return 0;
    }

    case WM_SIZE:
        LayoutMain(hwnd);
        return 0;

    case WM_DPICHANGED:
    {
        if (g_hFont) DeleteObject(g_hFont);
        g_hFont = CreateUIFont(GetDpiOf(hwnd));
        EnumChildWindows(hwnd, SetFontProc, (LPARAM)g_hFont);
        LayoutMain(hwnd);
        return 0;
    }

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO *mmi = (MINMAXINFO *)lp;
        mmi->ptMinTrackSize.x = S(hwnd, 640);
        mmi->ptMinTrackSize.y = S(hwnd, 380);
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_BTN_REFRESH:
            ReloadAndApply();
            return 0;

        case ID_CHK_AUTO:
            if (HIWORD(wp) == BN_CLICKED) {
                if (SendMessage(g_hChkAuto, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                    SetTimer(hwnd, ID_TIMER, 3000, NULL);
                } else {
                    KillTimer(hwnd, ID_TIMER);
                }
            }
            return 0;

        case ID_BTN_ADMIN:
            if (MessageBoxW(hwnd,
                            L"以管理员身份重启后可以查看并结束系统级进程。\n是否继续？",
                            L"提权", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                if (ProcElevate(hwnd)) {
                    PostMessage(hwnd, WM_CLOSE, 0, 0);
                } else {
                    MessageBoxW(hwnd, L"提权失败或已被取消。", L"提示", MB_OK | MB_ICONWARNING);
                }
            }
            return 0;

        case ID_EDIT_FILTER:
            if (HIWORD(wp) == EN_CHANGE) {
                ApplyView();
            } else if (HIWORD(wp) == 1) { /* Ctrl+F */
                SetFocus(g_hEdit);
                SendMessage(g_hEdit, EM_SETSEL, 0, -1);
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
                    MessageBoxW(hwnd, L"该文件位置不可用（进程路径未知或权限不足）。",
                                L"提示", MB_OK | MB_ICONWARNING);
                }
            }
            return 0;
        }

        case IDM_KILL:
        {
            const PORT_ENTRY *e = SelectedEntry();
            if (e) DoKill(hwnd, e->pid, e->procName, FALSE);
            return 0;
        }

        case IDM_KILL_TREE:
        {
            const PORT_ENTRY *e = SelectedEntry();
            if (e) DoKill(hwnd, e->pid, e->procName, TRUE);
            return 0;
        }

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
                           e->remoteAddr, e->remotePort, e->state,
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
                ApplyView();
            }
            return 0;
        }
        }
        break;

    case WM_NOTIFY:
    {
        NMHDR *hdr = (NMHDR *)lp;
        if (hdr->idFrom == ID_LIST) {
            if (hdr->code == LVN_COLUMNCLICK) {
                int col = ((NMLISTVIEW *)lp)->iSubItem;
                if (col == g_sortCol) {
                    g_sortAsc = !g_sortAsc;
                } else {
                    g_sortCol = col;
                    g_sortAsc = 1;
                }
                ApplyView();
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
        break;
    }

    case WM_TIMER:
        if (wp == ID_TIMER) ReloadAndApply();
        return 0;

    case WM_APP_REFRESH:
        ReloadAndApply();
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, ID_TIMER);
        free(g_all);
        free(g_view);
        g_all = NULL;
        g_view = NULL;
        if (g_hFont) DeleteObject(g_hFont);
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
    ACCEL accels[2];
    UINT dpi;

    g_hInst = hInst;

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = MAIN_CLASS;
    if (!RegisterClassExW(&wc)) return 1;

    ZeroMemory(&wcd, sizeof(wcd));
    wcd.cbSize = sizeof(wcd);
    wcd.style = CS_HREDRAW | CS_VREDRAW;
    wcd.lpfnWndProc = DetailProc;
    wcd.hInstance = hInst;
    wcd.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcd.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcd.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcd.lpszClassName = DETAIL_CLASS;
    if (!RegisterClassExW(&wcd)) return 1;

    dpi = 96;
    {
        typedef UINT (WINAPI *PFN)(void);
        PFN p = (PFN)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForSystem");
        if (p) dpi = p();
    }

    hwnd = CreateWindowExW(0, MAIN_CLASS, L"端口占用查看器 — PortView",
                           WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                           CW_USEDEFAULT, CW_USEDEFAULT,
                           MulDiv(980, (int)dpi, 96), MulDiv(620, (int)dpi, 96),
                           NULL, NULL, hInst, NULL);
    if (!hwnd) return 1;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    accels[0].fVirt = FVIRTKEY;
    accels[0].key = VK_F5;
    accels[0].cmd = ID_BTN_REFRESH;
    accels[1].fVirt = FVIRTKEY | FCONTROL;
    accels[1].key = 'F';
    accels[1].cmd = ID_EDIT_FILTER;
    hAccel = CreateAcceleratorTableW(accels, 2);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!hAccel || !TranslateAcceleratorW(hwnd, hAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (hAccel) DestroyAcceleratorTable(hAccel);
    return (int)msg.wParam;
}
