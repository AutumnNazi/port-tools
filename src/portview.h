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
    /* 枚举时刻该进程的创建时间：结束进程前用它确认 PID 仍指向同一个进程 */
    FILETIME procCreate;
    BOOL procCreateValid;        /* FALSE 表示没取到创建时间，此时禁止对该 PID 做终止 */
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
/* 返回 1：至少一张表读取成功（count 可以为 0，表示确实没有端口）；返回 0：四张表全部读取失败 */
int  PortsEnumerate(PORT_ENTRY **entries, size_t *count);
void PortsFree(PORT_ENTRY *entries);
const WCHAR *PortsStateText(DWORD state);

/* 结束进程的结果：界面要能区分「PID 已被复用」这种危险情况和普通的权限失败 */
typedef enum {
    PROC_KILL_OK = 0,       /* 已发出终止请求 */
    PROC_KILL_REUSED,       /* PID 已被其它进程接管，为避免误杀未执行 */
    PROC_KILL_PARTIAL,      /* 进程树：部分子进程未能结束 */
    PROC_KILL_SELF,         /* 目标包含本工具自身，为避免操作中断已取消 */
    PROC_KILL_FAILED        /* 进程不存在、权限不足或其它错误；GetLastError() 有细节 */
} PROC_KILL_RESULT;

/* ---- proc.c ---- */
/* 产出的列表按 pid 升序排列，供 ProcFind 二分查找 */
int  ProcSnapshot(PROC_INFO **list, size_t *count);
void ProcFree(PROC_INFO *list);
/* 前置条件：list 必须是 ProcSnapshot 产出的（已按 pid 升序） */
const PROC_INFO *ProcFind(const PROC_INFO *list, size_t count, DWORD pid);
BOOL ProcGetPath(DWORD pid, WCHAR *buf, DWORD cch);
/* 一次打开句柄同时取映像路径与创建时间，列举时每个 PID 只付一次 OpenProcess */
BOOL ProcGetPathAndStart(DWORD pid, WCHAR *buf, DWORD cch, FILETIME *create);
BOOL ProcGetStartTime(DWORD pid, FILETIME *create);
BOOL ProcGetCommandLine(DWORD pid, WCHAR *buf, DWORD cch);
int  ProcEnumModules(DWORD pid, MODULE_INFO **list, size_t *count);
void ProcFreeModules(MODULE_INFO *list);
/* expectCreate 是选择某一行时记录的进程创建时间（PORT_ENTRY.procCreate）；
 * 两者不一致说明 PID 已被复用，此时不会执行终止。传 NULL 表示无法校验身份，同样拒绝。 */
PROC_KILL_RESULT ProcTerminate(DWORD pid, const FILETIME *expectCreate);
PROC_KILL_RESULT ProcTerminateTree(DWORD pid, const FILETIME *expectCreate);
BOOL ProcOpenFileLocation(HWND hwnd, const WCHAR *path);
BOOL ProcIsElevated(void);
BOOL ProcElevate(HWND hwnd);
BOOL ProcEnableDebugPriv(void);

/* ---- ui.c ---- */
int  UiRun(HINSTANCE hInst, int nCmdShow);

#endif /* PORTVIEW_H */
