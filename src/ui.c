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
#define ID_CB_PROTO    1008
#define ID_CHK_LISTEN  1009
#define ID_INFOBAR     1010    /* 底部选中项详情栏 */
#define ID_CHK_SYS     1011    /* 隐藏系统关键进程占用的端口 */

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

/* 菜单栏「筛选」下的命令 */
#define IDM_AUTO       2020
#define IDM_LISTEN     2021
#define IDM_HIDESYS    2022
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

enum {
    COL_PROTO = 0, COL_LADDR, COL_LPORT, COL_RADDR,
    COL_RPORT, COL_STATE, COL_PID, COL_NAME, COL_PATH, COL_COUNT
};

static const WCHAR *COL_TITLES[COL_COUNT] = {
    L"协议", L"本地地址", L"本地端口", L"远程地址", L"远程端口",
    L"状态", L"PID", L"进程", L"映像路径"
};

/*
 * 列宽按信息量分配，不平均分。地址列在通配监听时只剩一个 *，却占着 140px；
 * 而映像路径是判断「谁占了端口」最该看的一列，反而被前面几列挤出可视区。
 * 路径列不写死宽度，由 LayoutColumns 把窗口剩下的宽度全给它。
 */
static const int COL_WIDTHS[COL_COUNT] = {
    56, 104, 76, 104, 76, 88, 64, 150, 240
};

#define COL_PATH_MIN 180   /* 路径列最小宽度，窗口再窄也不低于此 */

static HINSTANCE g_hInst;
static HWND g_hwndMain;
static HWND g_hList;
static HWND g_hEdit;
static HWND g_hBtnRefresh;
static HWND g_hInfoBar;
static HWND g_hStatus;
static HFONT g_hFont;

static PORT_ENTRY *g_all = NULL;
static size_t g_allCount = 0;
static PORT_ENTRY *g_view = NULL;
static size_t g_viewCount = 0;

/* 上一次 PortsEnumerate 是否一张端口表都没读出来 */
static int g_enumFailed = 0;

static int g_sortCol = COL_LPORT;
static int g_sortAsc = 1;

/* UpdateInfoBar 定义在 ApplyView 之后（它要用 SelectedEntry），这里先前置声明 */
static void UpdateInfoBar(void);

/* 顶栏的两个结构化筛选，与文本框是「与」的关系：文本框管模糊匹配，这两个管精确范围 */
static int g_protoFilter = 0;   /* 0=全部 1=仅TCP 2=仅UDP 3=仅IPv4 4=仅IPv6 */
static int g_listenOnly = 0;    /* 1=只看监听端口 */
static int g_hideSystem = 0;    /* 1=隐藏系统关键进程占用的端口 */
static BOOL g_autoOn = TRUE;    /* 自动刷新勾选框状态 */
static BOOL g_hadInitialSelect = FALSE;   /* 首次载入是否已做过默认选中 */

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

/* LVS_OWNERDATA 下按需提供单元格文本，文本在 ports.c 枚举时已格式化好 */
static const WCHAR *CellText(const PORT_ENTRY *e, int col)
{
    switch (col) {
    case COL_PROTO: return e->proto;
    case COL_LADDR: return ShortAddr(e->localAddr);
    case COL_LPORT: return e->portText;
    case COL_RADDR: return ShortAddr(e->remoteAddr);
    case COL_RPORT: return e->rportText;
    case COL_STATE: return e->state;
    case COL_PID:   return e->pidText;
    case COL_NAME:  return e->procName;
    case COL_PATH:  return e->procPath;
    default:        return L"";
    }
}

/*
 * 状态文字配色，让连接状态一眼可辨：监听绿、已建立蓝、终态灰、中间态橙。
 * 空状态（UDP 行）返回 CLR_DEFAULT 走系统默认色。
 */
static COLORREF StateTextColor(const WCHAR *state)
{
    if (!state || !state[0]) return CLR_DEFAULT;
    if (_wcsicmp(state, L"监听") == 0) return RGB(16, 124, 16);
    if (_wcsicmp(state, L"已建立") == 0) return RGB(0, 102, 204);
    if (_wcsicmp(state, L"时间等待") == 0 || _wcsicmp(state, L"已关闭") == 0)
        return RGB(130, 130, 130);
    return RGB(200, 110, 0);   /* 关闭等待 / FIN / SYN / 正在关闭 等中间态 */
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

/* 逐字段匹配，避免为每行拼一个临时大字符串 */
static BOOL MatchFilter(const PORT_ENTRY *e, const WCHAR *key)
{
    if (!key || !key[0]) return TRUE;

    return ContainsI(e->proto, key) ||
           ContainsI(e->localAddr, key) ||
           ContainsI(e->portText, key) ||
           ContainsI(e->remoteAddr, key) ||
           ContainsI(e->rportText, key) ||
           ContainsI(e->state, key) ||
           ContainsI(e->pidText, key) ||
           ContainsI(e->procName, key) ||
           ContainsI(e->procPath, key);
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

/*
 * 顶部菜单栏。
 *
 * 筛选条件放菜单而不是工具栏，是因为 Win32 的下拉式 ComboBox 拒绝高于字体算出的
 * 自然高度（实测 CBS_DROPDOWNLIST / CBS_DROPDOWN / CBS_NOINTEGRALHEIGHT 全部无效，
 * CB_SETITEMHEIGHT 也不生效），硬留在工具栏里就永远和旁边的按钮差一截。
 * 勾选项做成菜单项天生没有高度问题，勾选标记也比复选框更醒目。
 */
static HMENU BuildMainMenu(void)
{
    HMENU bar, filter, proto;

    filter = CreatePopupMenu();
    AppendMenuW(filter, MF_STRING, IDM_AUTO, L"自动刷新(&A)\tCtrl+Shift+R");
    AppendMenuW(filter, MF_STRING, IDM_LISTEN, L"仅监听端口(&L)");
    AppendMenuW(filter, MF_STRING, IDM_HIDESYS, L"隐藏系统端口(&S)");
    AppendMenuW(filter, MF_SEPARATOR, 0, NULL);
    AppendMenuW(filter, MF_STRING, IDM_CLEAR, L"清除全部筛选(&C)\tEsc");

    proto = CreatePopupMenu();
    AppendMenuW(proto, MF_STRING, IDM_PROTO_ALL, L"全部协议(&A)");
    AppendMenuW(proto, MF_STRING, IDM_PROTO_TCP, L"仅 TCP(&T)");
    AppendMenuW(proto, MF_STRING, IDM_PROTO_UDP, L"仅 UDP(&U)");
    AppendMenuW(proto, MF_STRING, IDM_PROTO_V4,  L"仅 IPv4(&4)");
    AppendMenuW(proto, MF_STRING, IDM_PROTO_V6,  L"仅 IPv6(&6)");
    AppendMenuW(filter, MF_POPUP, (UINT_PTR)proto, L"协议(&P)");

    bar = CreateMenu();
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)filter, L"筛选(&F)");
    return bar;
}

/* 把当前筛选状态回写到菜单勾选标记上。所有改状态的地方都要走这里，别各写各的。 */
static void SyncFilterMenu(void)
{
    HMENU bar = GetMenu(g_hwndMain);
    HMENU filter, proto;

    if (!bar) return;
    filter = GetSubMenu(bar, 0);
    if (!filter) return;

    CheckMenuItem(filter, IDM_AUTO, MF_BYCOMMAND | (g_autoOn ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(filter, IDM_LISTEN, MF_BYCOMMAND | (g_listenOnly ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(filter, IDM_HIDESYS, MF_BYCOMMAND | (g_hideSystem ? MF_CHECKED : MF_UNCHECKED));

    proto = GetSubMenu(filter, 5);
    if (proto) {
        CheckMenuRadioItem(proto, IDM_PROTO_ALL, IDM_PROTO_V6,
                           IDM_PROTO_ALL + g_protoFilter, MF_BYCOMMAND);
    }
}

static void UpdateStatus(void)
{
    WCHAR text[320];
    WCHAR cond[160];
    SYSTEMTIME st;
    size_t listen = 0, i;

    for (i = 0; i < g_allCount; ++i) {
        if (_wcsicmp(g_all[i].state, L"监听") == 0) listen++;
    }

    GetLocalTime(&st);

    /*
     * 生效中的筛选条件直接拼进状态栏。勾选项都搬进菜单后，不打开菜单就看不见
     * 当前到底滤了什么，列表突然变短会让人以为程序出问题了。
     */
    {
        static const WCHAR *const PROTO_TEXT[5] = {
            L"", L"仅 TCP · ", L"仅 UDP · ", L"仅 IPv4 · ", L"仅 IPv6 · "
        };
        size_t k = 0;
        cond[0] = 0;
        if (g_protoFilter) k = (size_t)_snwprintf(cond, 160, L"%s", PROTO_TEXT[g_protoFilter]);
        if (g_listenOnly && k < 150) k += (size_t)_snwprintf(cond + k, 160 - k, L"仅监听 · ");
        if (g_hideSystem && k < 150) k += (size_t)_snwprintf(cond + k, 160 - k, L"已隐藏系统端口 · ");
        if (g_autoOn && k < 150) k += (size_t)_snwprintf(cond + k, 160 - k, L"自动刷新 · ");
    }

    if (g_enumFailed) {
        _snwprintf(text, 320,
                   L"读取端口表失败 · 显示的是上一次的结果 · %02d:%02d:%02d",
                   st.wHour, st.wMinute, st.wSecond);
    } else {
        _snwprintf(text, 320, L"共 %u 条连接 · 监听端口 %u 个 · 显示 %u 条 · %s%02d:%02d:%02d",
                   (unsigned)g_allCount, (unsigned)listen, (unsigned)g_viewCount,
                   cond, st.wHour, st.wMinute, st.wSecond);
    }
    text[319] = 0;

    SendMessageW(g_hStatus, SB_SETTEXTW, 0, (LPARAM)text);

    /*
     * 右端这一格同时承担两件事：显示当前权限，以及作为提权入口。
     * 未提权时显示成「标准用户（点击提权）」，鼠标移上去有下划线提示可点；
     * 已提权则只是纯状态，不可点。
     */
    if (ProcIsElevated()) {
        SendMessageW(g_hStatus, SB_SETTEXTW, 1, (LPARAM)L"管理员");
    } else {
        SendMessageW(g_hStatus, SB_SETTEXTW, 1, (LPARAM)L"标准用户 · 点击提权");
    }
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
    if (g_allCount == 0) return L"当前没有检测到端口占用";
    if (g_hideSystem)
        return L"没有符合当前条件的端口\n当前已隐藏系统关键进程占用的端口\n试试取消「隐藏系统端口」";
    if (g_listenOnly || g_protoFilter != 0)
        return L"没有符合当前筛选条件的端口\n试试取消「仅监听端口」或切换协议";
    if (g_allCount > 0)
        return L"没有匹配的端口\n试试清空筛选框";
    return L"当前没有检测到端口占用";
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
    /* 避开列头区域，让提示落在数据区中央 */
    if (rc.bottom > S(g_hwndMain, 24)) rc.top += S(g_hwndMain, 24);

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

/* 交替行底色；选中/拖放高亮行保留系统配色不动 */
static void ApplyZebraBand(NMLVCUSTOMDRAW *cd)
{
    if (!(cd->nmcd.uItemState & (CDIS_SELECTED | CDIS_DROPHILITED)) &&
        (cd->nmcd.dwItemSpec & 1)) {
        cd->clrTextBk = RGB(245, 246, 249);
    }
}

/*
 * 列表采用 LVS_OWNERDATA（虚拟列表）：这里只负责重算 g_view、同步行数并重绘，
 * 行文本由 LVN_GETDISPINFO 按需提供。因此刷新不再 DeleteAllItems + 逐行 InsertItem，
 * 也不会为每行做 8 次 SetItemText。
 */
static void ApplyView(void)
{
    WCHAR filter[256];
    PORT_ENTRY selEntry;
    size_t i, n = 0, oldCount;
    int sel = -1, top = 0, newSel = -1, haveSel = 0;

    /* 记住当前选中项与滚动位置 */
    oldCount = g_viewCount;
    top = ListView_GetTopIndex(g_hList);
    sel = ListView_GetNextItem(g_hList, -1, LVNI_SELECTED);
    if (sel >= 0 && (size_t)sel < g_viewCount) {
        selEntry = g_view[sel];   /* 结构体拷贝，无需格式化成字符串 */
        haveSel = 1;
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
                const PORT_ENTRY *e = &g_all[i];
                BOOL listening = (_wcsicmp(e->state, L"监听") == 0);

                /* 四个条件是「与」：文本框模糊匹配 + 协议范围 + 仅监听 + 屏蔽系统端口
                 * UDP 没有连接状态，本身就是常驻端口，所以不参与「仅监听」判定，
                 * 否则勾上之后 UDP 会整片消失。 */
                if (g_listenOnly && !e->state[0]) continue;
                if (g_listenOnly && !listening) continue;
                if (g_hideSystem && IsSystemOwner(e)) continue;
                if (!MatchProto(e, g_protoFilter)) continue;
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
        ListView_EnsureVisible(g_hList, newSel, FALSE);
    } else if (top > 0 && g_viewCount) {
        if ((size_t)top >= g_viewCount) top = (int)g_viewCount - 1;
        ListView_EnsureVisible(g_hList, top, TRUE);
    } else if (g_viewCount && !haveSel && !g_hadInitialSelect) {
        /*
         * 首次载入且没有可恢复的选中项时，默认选中第一行。
         * 之前首屏一行都没选，底部详情是空的，用户要先点一下才知道选中了什么。
         */
        g_hadInitialSelect = TRUE;
        ListView_SetItemState(g_hList, 0, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
    }

    InvalidateRect(g_hList, NULL, FALSE);
    UpdateInfoBar();
    UpdateStatus();
    UpdateSortMark();
}

static void ReloadAndApply(void)
{
    PORT_ENTRY *list = NULL;
    size_t count = 0;

    if (PortsEnumerate(&list, &count)) {
        free(g_all);
        g_all = list;
        g_allCount = count;
        g_enumFailed = 0;
    } else {
        /* 读取失败时保留上一次的数据并标注出来，不能把界面清成空列表还显示成功 */
        PortsFree(list);
        g_enumFailed = 1;
    }

    ApplyView();
}

static const PORT_ENTRY *SelectedEntry(void)
{
    int i = ListView_GetNextItem(g_hList, -1, LVNI_SELECTED);
    if (i < 0 || (size_t)i >= g_viewCount) return NULL;
    return &g_view[i];
}

/*
 * 刷新底部详情栏。选中即更新，不用双击就能看到「进程 · PID · 路径」。
 * 路径过长时中间省略，保留开头的盘符和结尾的 exe 名——这两段才是定位用的。
 */
static void UpdateInfoBar(void)
{
    const PORT_ENTRY *e = SelectedEntry();
    WCHAR text[512];

    if (!g_hInfoBar) return;

    if (!e) {
        SetWindowTextW(g_hInfoBar, L"　选中一行查看占用详情");
        return;
    }

    if (e->procPath[0]) {
        _snwprintf(text, 512, L"　%s　PID %u　%s",
                   e->procName[0] ? e->procName : L"(未知进程)",
                   e->pid,
                   e->procPath);
    } else {
        /* 无路径多是权限不足，明确说出来，免得以为程序没取到 */
        _snwprintf(text, 512,
                   L"　%s　PID %u　（映像路径不可用，可能需要管理员权限）",
                   e->procName[0] ? e->procName : L"(未知进程)", e->pid);
    }
    text[511] = 0;

    SetWindowTextW(g_hInfoBar, text);
}

/* ------------------------------------------------------------ 操作 */

/*
 * 提权确认：状态栏右端点击、或后续快捷键都走这里，统一提示文案与失败处理。
 * 提权成功后关闭当前实例，避免出现两个窗口同时枚举端口。
 */
static BOOL ConfirmElevate(HWND hwnd)
{
    if (MessageBoxW(hwnd,
                    L"以管理员身份重启后可以查看并结束系统级进程。\n是否继续？",
                    L"提权", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return FALSE;
    }

    if (ProcElevate(hwnd)) {
        PostMessage(g_hwndMain, WM_CLOSE, 0, 0);
        return TRUE;
    }

    MessageBoxW(hwnd, L"提权失败或已被取消。", L"提示", MB_OK | MB_ICONWARNING);
    return FALSE;
}

/* 点在状态栏右端那一格（权限提示）上？命中则返回 TRUE。 */
static BOOL HitAdminCell(HWND hwnd)
{
    POINT cursor, origin;
    RECT rc;
    int rightBound;

    if (!g_hStatus || ProcIsElevated()) return FALSE;

    if (!GetCursorPos(&cursor)) return FALSE;
    if (!GetWindowRect(g_hStatus, &rc)) return FALSE;

    /* 状态栏右下角转成主窗口客户区坐标 */
    origin.x = rc.right;
    origin.y = rc.bottom;
    ScreenToClient(hwnd, &origin);

    /* 右端那一格宽度取 220，与 LayoutMain 里 parts[0] 的划分一致 */
    rightBound = origin.x - S(hwnd, 220);

    return cursor.x >= rightBound && cursor.x <= origin.x
        && cursor.y >= origin.y - S(hwnd, 24) && cursor.y <= origin.y;
}

static void DoKill(HWND hwnd, const PORT_ENTRY *e, BOOL tree)
{
    WCHAR msg[512], title[128];
    PORT_ENTRY target;
    PROC_KILL_RESULT r;

    if (!e) return;

    /* 拷到本地：下面的确认框会阻塞消息循环，期间定时刷新可能已经释放掉 g_view */
    target = *e;

    if (target.pid == 0 || target.pid == 4) {
        MessageBoxW(hwnd, L"该系统进程无法结束。", L"无法结束", MB_OK | MB_ICONWARNING);
        return;
    }

    if (!target.procCreateValid) {
        MessageBoxW(hwnd,
                    L"无法确认该进程的身份：它可能已经退出，也可能权限不足读不到它的启动时间。\n"
                    L"为避免误杀已被系统复用了同一 PID 的其它进程，本次操作已取消。",
                    L"未能结束", MB_OK | MB_ICONWARNING);
        PostMessage(g_hwndMain, WM_APP_REFRESH, 0, 0);
        return;
    }

    wcsncpy(title, tree ? L"结束进程树" : L"结束进程", 127);
    title[127] = 0;

    _snwprintf(msg, 512,
               tree ? L"确定结束进程 %s（PID %u）及其所有子进程吗？\n未保存的数据将丢失。"
                    : L"确定结束进程 %s（PID %u）吗？\n未保存的数据将丢失。",
               target.procName, target.pid);

    if (MessageBoxW(hwnd, msg, title, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;

    /* 带上选择时的进程创建时间：列表可能落后数秒，期间同一 PID 可能已经换成别的进程 */
    r = tree ? ProcTerminateTree(target.pid, &target.procCreate)
             : ProcTerminate(target.pid, &target.procCreate);

    switch (r) {
    case PROC_KILL_OK:
        break;

    case PROC_KILL_REUSED:
        MessageBoxW(hwnd,
                    L"该 PID 已不是你选择的那个进程（原进程期间已退出，PID 被系统重新分配）。\n"
                    L"为避免误杀无关进程，本次没有执行结束操作。\n请确认列表上的进程后再试。",
                    L"已中止", MB_OK | MB_ICONWARNING);
        break;

    case PROC_KILL_PARTIAL:
        MessageBoxW(hwnd,
                    L"进程树已处理，但有部分成员没有结束：它们可能已经退出、PID 已被复用，\n"
                    L"或权限不足——读不到创建时间的进程无法确认它是否属于这棵树，已跳过。",
                    L"部分完成", MB_OK | MB_ICONWARNING);
        break;

    case PROC_KILL_SELF:
        MessageBoxW(hwnd,
                    L"要结束的范围里包含本工具自己（你选中的就是它，或者它在那棵进程树里，\n"
                    L"例如本工具是从你要结束的那个命令行启动的）。\n"
                    L"为避免操作进行到一半工具自己消失，本次没有执行。",
                    L"已中止", MB_OK | MB_ICONWARNING);
        break;

    case PROC_KILL_FAILED:
    default:
    {
        DWORD err = GetLastError();
        _snwprintf(msg, 512, L"结束失败（错误 %u）。\n如果是系统或其他用户的进程，请以管理员身份运行本工具。",
                   err);
        MessageBoxW(hwnd, msg, L"失败", MB_OK | MB_ICONERROR);
        break;
    }
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
    /* x/y 已是屏幕坐标（GetCursorPos / WM_CONTEXTMENU 传入），不要再转换一次 */
    pt.x = x;
    pt.y = y;

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
    HICON icon;      /* 进程图标，WM_DESTROY 时 DestroyIcon */
} DETAIL_CTX;

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

        ctx = (DETAIL_CTX *)calloc(1, sizeof(DETAIL_CTX));
        if (!ctx) return -1;
        if (!pe) { free(ctx); return -1; }
        ctx->entry = *pe;

        /* 先挂到窗口上：即便下面某步失败导致创建中止，WM_DESTROY 也能释放 ctx 与字体 */
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)ctx);
        ctx->hFont = CreateUIFont(GetDpiOf(hwnd));
        ctx->icon = NULL;

        /*
         * 进程图标：优先用 exe 自带的大图标，抽不到再退到系统按扩展名给的通用图标。
         * 只用 ExtractIconEx 会留下大片空白——svchost/lsass/System 这些系统二进制
         * 本身就没有图标资源，而列表里恰恰大半是它们。
         * 连完整路径都读不到时（权限不足）拿进程名兜底：SHGFI_USEFILEATTRIBUTES
         * 只看扩展名，不要求文件真的存在，所以「svchost.exe」「System」也能出图。
         * 列表里不放图标是因为绝大多数行图标几乎全一样，详情窗口看的才是具体那个进程。
         */
        if (pe->procPath[0]) {
            HICON hIcon = NULL;
            /* iIconIndex=0 取第一个图标（通常是 256/48/32 的最大尺寸）；
             * SS_ICON 会自行缩放到控件大小，不必挑特定尺寸。 */
            if (ExtractIconExW(pe->procPath, 0, &hIcon, NULL, 1) != 0)
                ctx->icon = hIcon;
        }
        if (!ctx->icon) {
            SHFILEINFOW sfi;
            const WCHAR *key = pe->procPath[0] ? pe->procPath : pe->procName;
            ZeroMemory(&sfi, sizeof(sfi));
            if (SHGetFileInfoW(key, FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                               SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES))
                ctx->icon = sfi.hIcon;
        }

        if (ctx->icon) {
            h = CreateWindowExW(0, L"Static", L"",
                                WS_CHILD | WS_VISIBLE | SS_ICON | SS_CENTERIMAGE,
                                0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)D_ICO, g_hInst, NULL);
            SendMessage(h, STM_SETICON, ICON_SMALL, (LPARAM)ctx->icon);
        }

        _snwprintf(buf, 512, L"%s  (PID %u)", ctx->entry.procName, ctx->entry.pid);
        h = CreateWindowExW(0, L"Static", buf, WS_CHILD | WS_VISIBLE | SS_LEFT,
                            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)D_ST_NAME, g_hInst, NULL);
        SetCtlFont(h, ctx->hFont);

        h = CreateWindowExW(0, L"Static", L"映像路径：", WS_CHILD | WS_VISIBLE | SS_LEFT,
                            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)D_ST_PATH, g_hInst, NULL);
        SetCtlFont(h, ctx->hFont);

        h = CreateWindowExW(WS_EX_CLIENTEDGE, L"Edit", ctx->entry.procPath,
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_READONLY | ES_AUTOHSCROLL,
                            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)D_ED_PATH, g_hInst, NULL);
        SetCtlFont(h, ctx->hFont);

        h = CreateWindowExW(0, L"Static", L"命令行：", WS_CHILD | WS_VISIBLE | SS_LEFT,
                            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)D_ST_CMD, g_hInst, NULL);
        SetCtlFont(h, ctx->hFont);

        buf[0] = 0;
        if (!ProcGetCommandLine(ctx->entry.pid, buf, 512)) {
            wcsncpy(buf, L"（不可用）", 511);
            buf[511] = 0;
        }
        h = CreateWindowExW(WS_EX_CLIENTEDGE, L"Edit", buf,
                            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP |
                            ES_READONLY | ES_MULTILINE | ES_AUTOVSCROLL,
                            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)D_ED_CMD, g_hInst, NULL);
        SetCtlFont(h, ctx->hFont);

        h = CreateWindowExW(0, L"Static", L"已加载模块（关联文件）：",
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
            FillModules(h, ctx->entry.pid);
        }

        h = CreateWindowExW(0, L"Button", L"打开所在目录",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)D_BTN_LOC, g_hInst, NULL);
        SetCtlFont(h, ctx->hFont);

        h = CreateWindowExW(0, L"Button", L"结束进程",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)D_BTN_KILL, g_hInst, NULL);
        SetCtlFont(h, ctx->hFont);

        h = CreateWindowExW(0, L"Button", L"重新加载",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)D_BTN_RELOAD, g_hInst, NULL);
        SetCtlFont(h, ctx->hFont);

        h = CreateWindowExW(0, L"Button", L"关闭",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)D_BTN_CLOSE, g_hInst, NULL);
        SetCtlFont(h, ctx->hFont);

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

        /* 有图标时给标题让出左侧空间；没有图标时标题回到最左，不留空位 */
        {
            int iconW = GetDlgItem(hwnd, D_ICO) ? S(hwnd, 34) : 0;
            MoveCtl(hwnd, D_ICO, pad, pad, S(hwnd, 30), S(hwnd, 30));
            MoveCtl(hwnd, D_ST_NAME, pad + iconW, pad, w - pad * 2 - iconW, S(hwnd, 20));
        }
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
            if (ctx) DoKill(hwnd, &ctx->entry, FALSE);
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
            if (ctx->icon) DestroyIcon(ctx->icon);
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
        int cx = S(hwnd, COL_WIDTHS[i]);
        ListView_SetColumnWidth(g_hList, i, cx);
        fixed += cx;
    }

    /* 留一点余量，避免正好卡在临界值上凭空多出一条横向滚动条 */
    pathW = avail - fixed - S(hwnd, 2);
    if (pathW < S(hwnd, COL_PATH_MIN)) pathW = S(hwnd, COL_PATH_MIN);
    ListView_SetColumnWidth(g_hList, COL_PATH, pathW);
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
    bh = S(hwnd, 26);
    toolbarY = S(hwnd, 6);

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
     * 工具栏只剩筛选框与刷新按钮两个控件，高度都由 bh 统一给，
     * 不会再出现某一类控件比旁边矮一截的情况。
     * 筛选条件全在顶部菜单栏里，这里不再需要折叠逻辑。
     */
    {
        int gap = S(hwnd, 8);
        int wRefresh = S(hwnd, 86);
        int wEdit = w - pad * 2 - gap - wRefresh;
        int x = pad;

        /* 筛选框吃剩余宽度，但别窄到看不清占位提示 */
        if (wEdit > S(hwnd, 520)) wEdit = S(hwnd, 520);
        if (wEdit < S(hwnd, 110)) wEdit = S(hwnd, 110);

        MoveWindow(g_hEdit, x, toolbarY, wEdit, bh, TRUE);
        x += wEdit + gap;
        MoveWindow(g_hBtnRefresh, x, toolbarY, wRefresh, bh, TRUE);
    }

    MoveWindow(g_hList, 0, S(hwnd, 38), w, h - S(hwnd, 38) - sbH - S(hwnd, 26), TRUE);
    MoveWindow(g_hInfoBar, 0, h - sbH - S(hwnd, 26), w, S(hwnd, 26), TRUE);
    LayoutColumns(hwnd);

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

        /*
         * g_hInst, NULL);

        /* LVS_OWNERDATA：虚拟列表，行文本按需提供，刷新时不必逐行重建 */
        g_hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                                  LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS |
                                  LVS_OWNERDATA,
                                  0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_LIST,
                                  g_hInst, NULL);
        SetWindowTheme(g_hList, L"Explorer", NULL);
        ListView_SetExtendedListViewStyle(g_hList,
                                          LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
                                          LVS_EX_LABELTIP);
        SetWindowSubclass(g_hList, ListSubclassProc, 1, 0);

        ZeroMemory(&col, sizeof(col));
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        for (i = 0; i < COL_COUNT; ++i) {
            col.pszText = (LPWSTR)COL_TITLES[i];
            col.cx = S(hwnd, COL_WIDTHS[i]);
            ListView_InsertColumn(g_hList, i, &col);
        }

        /*
         * 底部详情栏：选中行就能看到「进程 · PID · 路径」，不必双击开窗口。
         * 这个工具的主场景是「谁占了这个端口」，答案就在这一行里，
         * 藏在双击后面等于多绕一次。
         */
        g_hInfoBar = CreateWindowExW(WS_EX_CLIENTEDGE, L"Static", L"",
                                     WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
                                     0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_INFOBAR,
                                     g_hInst, NULL);

        g_hStatus = CreateWindowExW(0, STATUSCLASSNAMEW, L"",
                                    WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                                    0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_STATUS,
                                    g_hInst, NULL);

        EnumChildWindows(hwnd, SetFontProc, (LPARAM)g_hFont);

        /* 默认开启自动刷新，保持端口/进程实时可见 */
        g_autoOn = TRUE;
        SetTimer(hwnd, ID_TIMER, REFRESH_MS, NULL);

        LayoutMain(hwnd);
        ReloadAndApply();
        return 0;
    }

    case WM_SIZE:
        LayoutMain(hwnd);
        return 0;

    /*
     * 静态控件与列表的标签统一用窗口背景。
     * 勾选框是 BS_OWNERDRAW，WM_PAINT 根本不走这里，所以不需要 WM_CTLCOLORBTN。
     */
    case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)wp, RGB(0, 0, 0));
        SetBkColor((HDC)wp, GetSysColor(COLOR_WINDOW));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);

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
            ApplyView();
            return 0;

        case IDM_HIDESYS:
            g_hideSystem = !g_hideSystem;
            SyncFilterMenu();
            ApplyView();
            return 0;

        case IDM_PROTO_ALL:
        case IDM_PROTO_TCP:
        case IDM_PROTO_UDP:
        case IDM_PROTO_V4:
        case IDM_PROTO_V6:
            /* 协议子菜单的 5 项连续编号，偏移即 MatchProto 的取值 */
            g_protoFilter = LOWORD(wp) - IDM_PROTO_ALL;
            SyncFilterMenu();
            ApplyView();
            return 0;

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
                ApplyView();
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
            SetWindowTextW(g_hEdit, L"");
            g_protoFilter = 0;
            g_listenOnly = 0;
            g_hideSystem = 0;
            SyncFilterMenu();
            ApplyView();
            SetFocus(g_hList);
            return 0;

        case IDM_ELEVATE:
            if (!ProcIsElevated()) ConfirmElevate(hwnd);
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

    case WM_LBUTTONDOWN:
        if (HitAdminCell(hwnd)) {
            ConfirmElevate(hwnd);
            return 0;
        }
        return 0;

    case WM_NOTIFY:
    {
        NMHDR *hdr = (NMHDR *)lp;
        if (hdr->idFrom == ID_LIST) {
            if (hdr->code == NM_CUSTOMDRAW) {
                NMLVCUSTOMDRAW *cd = (NMLVCUSTOMDRAW *)lp;

                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;

                if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    ApplyZebraBand(cd);
                    return CDRF_NOTIFYITEMDRAW;   /* 继续申请 subitem 通知，给状态列上色 */
                }

                if (cd->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
                    ApplyZebraBand(cd);
                    /* 非状态列必须显式恢复默认色：CDRF_NEWFONT 的颜色会串到后面的子项 */
                    cd->clrText = CLR_DEFAULT;
                    if (cd->iSubItem == COL_STATE &&
                        (size_t)cd->nmcd.dwItemSpec < g_viewCount) {
                        cd->clrText = StateTextColor(g_view[cd->nmcd.dwItemSpec].state);
                    }
                    return CDRF_NEWFONT;
                }
                return CDRF_DODEFAULT;
            }
            if (hdr->code == LVN_GETDISPINFO) {
                NMLVDISPINFOW *di = (NMLVDISPINFOW *)lp;
                int item = di->item.iItem;

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
            if (hdr->code == LVN_ITEMCHANGED) {
                NMLISTVIEW *nv = (NMLISTVIEW *)lp;
                if ((nv->uChanged & LVIF_STATE) &&
                    (nv->uNewState ^ nv->uOldState) & (LVIS_SELECTED | LVIS_FOCUSED)) {
                    UpdateInfoBar();
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
        g_hInfoBar = NULL;
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
    ACCEL accels[10];
    HMENU menu;
    UINT dpi;

    g_hInst = hInst;

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

    /* 菜单必须在建窗前建好：hMenu 参数只在创建时生效，之后只能 SetMenu */
    menu = BuildMainMenu();
    if (!menu) return 1;

    hwnd = CreateWindowExW(0, MAIN_CLASS, L"端口占用查看器 — PortView",
                           WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                           CW_USEDEFAULT, CW_USEDEFAULT,
                           MulDiv(980, (int)dpi, 96), MulDiv(620, (int)dpi, 96),
                           NULL, menu, hInst, NULL);
    if (!hwnd) { DestroyMenu(menu); return 1; }

    SyncFilterMenu();

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    accels[0].fVirt = FVIRTKEY;
    accels[0].key = VK_F5;
    accels[0].cmd = ID_BTN_REFRESH;
    accels[1].fVirt = FVIRTKEY | FCONTROL;
    accels[1].key = 'F';
    accels[1].cmd = ID_EDIT_FILTER;

    /* 工具类软件的常用操作交给键盘：查看详情、结束进程、复制行都要能一按到底 */
    accels[2].fVirt = FVIRTKEY;
    accels[2].key = VK_RETURN;
    accels[2].cmd = IDM_DETAIL;

    accels[3].fVirt = FVIRTKEY;
    accels[3].key = VK_DELETE;
    accels[3].cmd = IDM_KILL;

    accels[4].fVirt = FVIRTKEY | FCONTROL;
    accels[4].key = 'C';
    accels[4].cmd = IDM_COPY_ROW;

    accels[5].fVirt = FVIRTKEY | FCONTROL;
    accels[5].key = 'A';
    accels[5].cmd = IDM_COPY_PATH;

    accels[6].fVirt = FVIRTKEY;
    accels[6].key = VK_ESCAPE;
    accels[6].cmd = IDM_CLEAR;

    accels[7].fVirt = FVIRTKEY | FCONTROL | FSHIFT;
    accels[7].key = 'E';
    accels[7].cmd = IDM_ELEVATE;

    accels[8].fVirt = FVIRTKEY | FSHIFT;
    accels[8].key = VK_TAB;
    accels[8].cmd = IDM_KILL_TREE;

    accels[9].fVirt = FVIRTKEY | FCONTROL | FSHIFT;
    accels[9].key = 'R';
    accels[9].cmd = IDM_AUTO;

    hAccel = CreateAcceleratorTableW(accels, 9);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!hAccel || !TranslateAcceleratorW(hwnd, hAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (hAccel) DestroyAcceleratorTable(hAccel);
    return (int)msg.wParam;
}
