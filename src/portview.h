#ifndef PORTVIEW_H
#define PORTVIEW_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <commctrl.h>

/* 一条端口/连接记录 */
typedef struct {
    WCHAR proto[8];              /* TCP / UDP */
    WCHAR localAddr[64];
    DWORD localPort;
    WCHAR remoteAddr[64];
    DWORD remotePort;
    WCHAR state[24];             /* 监听 / 已建立 / ... UDP 为空 */
    DWORD pid;
    WCHAR procName[64];
    WCHAR procPath[MAX_PATH];
    /* 数值列的文本在枚举时一次性格式化好，虚拟列表按需取文本时无需再转换 */
    WCHAR portText[16];
    WCHAR rportText[16];
    WCHAR pidText[16];
} PORT_ENTRY;

/* 进程快照项（仅名字，取自 Toolhelp，开销极低） */
typedef struct {
    DWORD pid;
    WCHAR name[MAX_PATH];
} PROC_INFO;

/* 进程加载的模块（关联文件） */
typedef struct {
    WCHAR path[MAX_PATH];
    ULONGLONG base;
    DWORD size;
} MODULE_INFO;

/* ---- ports.c ---- */
int  PortsEnumerate(PORT_ENTRY **entries, size_t *count);
void PortsFree(PORT_ENTRY *entries);
const WCHAR *PortsStateText(DWORD state);

/* ---- proc.c ---- */
int  ProcSnapshot(PROC_INFO **list, size_t *count);
void ProcFree(PROC_INFO *list);
const PROC_INFO *ProcFind(const PROC_INFO *list, size_t count, DWORD pid);
BOOL ProcGetPath(DWORD pid, WCHAR *buf, DWORD cch);
BOOL ProcGetCommandLine(DWORD pid, WCHAR *buf, DWORD cch);
int  ProcEnumModules(DWORD pid, MODULE_INFO **list, size_t *count);
void ProcFreeModules(MODULE_INFO *list);
BOOL ProcTerminate(DWORD pid);
BOOL ProcTerminateTree(DWORD pid);
BOOL ProcOpenFileLocation(HWND hwnd, const WCHAR *path);
BOOL ProcIsElevated(void);
BOOL ProcElevate(HWND hwnd);
BOOL ProcEnableDebugPriv(void);

/* ---- ui.c ---- */
int  UiRun(HINSTANCE hInst, int nCmdShow);

#endif /* PORTVIEW_H */
