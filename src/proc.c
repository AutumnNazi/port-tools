/* 进程相关：快照、路径、命令行、模块、结束进程、权限 */
#include "portview.h"

#include <psapi.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

typedef LONG(NTAPI *PFN_NTQIP)(HANDLE, LONG, PVOID, ULONG, PULONG);

typedef struct _PV_PBI {
    PVOID Reserved1;
    PVOID PebBaseAddress;
    PVOID Reserved2[2];
    ULONG_PTR UniqueProcessId;
    PVOID Reserved3;
} PV_PBI;

#define PV_ProcessBasicInformation 0
#define PV_ProcessWow64Information 26

/* PEB.ProcessParameters 偏移：x64 = 0x20，x86 = 0x10 */
/* RTL_USER_PROCESS_PARAMETERS.CommandLine 偏移：x64 = 0x70，x86 = 0x40 */

/* ---------------------------------------------------------------- 快照 */

static int CmpProcByPid(const void *a, const void *b)
{
    const PROC_INFO *x = (const PROC_INFO *)a;
    const PROC_INFO *y = (const PROC_INFO *)b;
    if (x->pid < y->pid) return -1;
    if (x->pid > y->pid) return 1;
    return 0;
}

int ProcSnapshot(PROC_INFO **out, size_t *outCount)
{
    HANDLE snap;
    PROCESSENTRY32W pe;
    PROC_INFO *list = NULL;
    size_t n = 0, cap = 256;

    *out = NULL;
    *outCount = 0;

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    list = (PROC_INFO *)malloc(cap * sizeof(PROC_INFO));
    if (!list) { CloseHandle(snap); return 0; }

    ZeroMemory(&pe, sizeof(pe));
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(snap, &pe)) {
        do {
            if (n == cap) {
                PROC_INFO *tmp;
                cap *= 2;
                tmp = (PROC_INFO *)realloc(list, cap * sizeof(PROC_INFO));
                if (!tmp) break;
                list = tmp;
            }
            list[n].pid = pe.th32ProcessID;
            wcsncpy(list[n].name, pe.szExeFile, MAX_PATH - 1);
            list[n].name[MAX_PATH - 1] = 0;
            n++;
        } while (Process32NextW(snap, &pe));
    }

    CloseHandle(snap);

    /* 按 pid 升序：ProcFind 因此可以二分查找，而不是每个端口表的行都扫一遍进程列表 */
    if (n > 1) qsort(list, n, sizeof(PROC_INFO), CmpProcByPid);

    *out = list;
    *outCount = n;
    return 1;
}

void ProcFree(PROC_INFO *list)
{
    free(list);
}

const PROC_INFO *ProcFind(const PROC_INFO *list, size_t count, DWORD pid)
{
    size_t lo = 0, hi = count;

    if (!list) return NULL;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (list[mid].pid < pid) lo = mid + 1;
        else if (list[mid].pid > pid) hi = mid;
        else return &list[mid];
    }
    return NULL;
}

/* ------------------------------------------------------------ 进程路径 */

/* 只查询不走读内存的句柄权限：优先 QUERY_LIMITED_INFORMATION，旧系统退回 QUERY_INFORMATION */
static BOOL OpenQueryHandle(DWORD pid, HANDLE *out)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!h) return FALSE;
    *out = h;
    return TRUE;
}

static BOOL ReadStartTime(HANDLE h, FILETIME *create)
{
    FILETIME start, exitT, kernel, user;

    create->dwHighDateTime = 0;
    create->dwLowDateTime = 0;

    if (!GetProcessTimes(h, &start, &exitT, &kernel, &user)) return FALSE;

    *create = start;
    return start.dwHighDateTime != 0 || start.dwLowDateTime != 0;
}

BOOL ProcGetPath(DWORD pid, WCHAR *buf, DWORD cch)
{
    return ProcGetPathAndStart(pid, buf, cch, NULL);
}

/*
 * 创建时间用于在结束进程时确认 PID 没有被回收后分配给别的进程。
 * 每个进程的创建时间在系统内唯一且不再变化，它比 PID 更适合当作身份凭据。
 */
BOOL ProcGetStartTime(DWORD pid, FILETIME *create)
{
    HANDLE h;

    create->dwHighDateTime = 0;
    create->dwLowDateTime = 0;
    if (pid == 0) return FALSE;

    if (!OpenQueryHandle(pid, &h)) return FALSE;

    {
        BOOL ok = ReadStartTime(h, create);
        CloseHandle(h);
        return ok;
    }
}

/* 一次打开句柄同时取映像路径与创建时间：列举端口时每行都解析进程信息，
 * 合并成一次 OpenProcess 可以避免每个 PID 付两次系统调用 */
BOOL ProcGetPathAndStart(DWORD pid, WCHAR *buf, DWORD cch, FILETIME *create)
{
    HANDLE h;
    DWORD size = cch;
    BOOL gotPath = FALSE;

    buf[0] = 0;
    if (create) { create->dwHighDateTime = 0; create->dwLowDateTime = 0; }
    if (pid == 0) return FALSE;

    if (!OpenQueryHandle(pid, &h)) return FALSE;

    if (QueryFullProcessImageNameW(h, 0, buf, &size)) {
        gotPath = TRUE;
    } else {
        buf[0] = 0;
    }

    /* 创建时间取不到时保持全零，调用方据此判定身份不可核验 */
    if (create) ReadStartTime(h, create);
    CloseHandle(h);
    return gotPath;
}

/* ---------------------------------------------------------- 命令行读取 */

BOOL ProcGetCommandLine(DWORD pid, WCHAR *buf, DWORD cch)
{
    HANDLE h = NULL;
    PFN_NTQIP pNtQIP = NULL;
    PV_PBI pbi;
    PVOID peb = NULL;
    BOOL wow64 = FALSE, ret = FALSE;
    SIZE_T ptrSize, paramsOff, cmdOff, rd = 0;
    ULONG_PTR params = 0;
    USHORT cmdLen = 0;
    PVOID cmdPtr = NULL;

    buf[0] = 0;
    if (pid == 0 || pid == 4) return FALSE;

    pNtQIP = (PFN_NTQIP)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
                                       "NtQueryInformationProcess");
    if (!pNtQIP) return FALSE;

    h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!h) h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!h) return FALSE;

    ZeroMemory(&pbi, sizeof(pbi));
    if (pNtQIP(h, PV_ProcessBasicInformation, &pbi, (ULONG)sizeof(pbi), NULL) != 0 ||
        !pbi.PebBaseAddress) {
        CloseHandle(h);
        return FALSE;
    }

    {
        typedef BOOL (WINAPI *PFN_IW64)(HANDLE, PBOOL);
        PFN_IW64 pIsWow64 = (PFN_IW64)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                                     "IsWow64Process");
        if (pIsWow64) pIsWow64(h, &wow64);
    }

    peb = pbi.PebBaseAddress;
    if (wow64) {
        ULONG_PTR peb32 = 0;
        if (pNtQIP(h, PV_ProcessWow64Information, &peb32, (ULONG)sizeof(peb32), NULL) == 0 &&
            peb32) {
            peb = (PVOID)peb32;
        }
    }

    ptrSize = wow64 ? 4 : 8;
    paramsOff = wow64 ? 0x10 : 0x20;
    cmdOff = wow64 ? 0x40 : 0x70;

    if (!ReadProcessMemory(h, (PBYTE)peb + paramsOff, &params, ptrSize, &rd) || !params) {
        CloseHandle(h);
        return FALSE;
    }

    if (!ReadProcessMemory(h, (PBYTE)params + cmdOff, &cmdLen, sizeof(cmdLen), &rd) ||
        !ReadProcessMemory(h, (PBYTE)params + cmdOff + ptrSize, &cmdPtr, ptrSize, &rd) ||
        !cmdPtr || cmdLen == 0) {
        CloseHandle(h);
        return FALSE;
    }

    if (cmdLen < (cch - 1) * sizeof(WCHAR)) {
        if (ReadProcessMemory(h, cmdPtr, buf, cmdLen, &rd) && rd >= sizeof(WCHAR)) {
            size_t chars = rd / sizeof(WCHAR);
            buf[chars] = 0;
            ret = TRUE;
        }
    }

    CloseHandle(h);
    return ret;
}

/* -------------------------------------------------------- 模块（DLL） */

int ProcEnumModules(DWORD pid, MODULE_INFO **out, size_t *outCount)
{
    HANDLE h;
    HMODULE *mods = NULL;
    DWORD cap = 256, needed = 0;
    MODULE_INFO *list = NULL;
    size_t n = 0, i;

    *out = NULL;
    *outCount = 0;

    h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!h) return 0;

    for (;;) {
        mods = (HMODULE *)malloc(cap * sizeof(HMODULE));
        if (!mods) { CloseHandle(h); return 0; }
        needed = 0;
        if (!EnumProcessModulesEx(h, mods, cap * sizeof(HMODULE), &needed, LIST_MODULES_ALL)) {
            free(mods);
            CloseHandle(h);
            return 0;
        }
        if (needed <= cap * sizeof(HMODULE)) break;
        free(mods);
        cap = needed / sizeof(HMODULE) + 16;
    }

    n = needed / sizeof(HMODULE);
    list = (MODULE_INFO *)calloc(n ? n : 1, sizeof(MODULE_INFO));
    if (!list) { free(mods); CloseHandle(h); return 0; }

    for (i = 0; i < n; ++i) {
        MODULEINFO mi;
        GetModuleFileNameExW(h, mods[i], list[i].path, MAX_PATH);
        ZeroMemory(&mi, sizeof(mi));
        if (GetModuleInformation(h, mods[i], &mi, sizeof(mi))) {
            list[i].base = (ULONGLONG)(ULONG_PTR)mi.lpBaseOfDll;
            list[i].size = mi.SizeOfImage;
        }
    }

    free(mods);
    CloseHandle(h);
    *out = list;
    *outCount = n;
    return 1;
}

void ProcFreeModules(MODULE_INFO *list)
{
    free(list);
}

/* ------------------------------------------------------------ 结束进程 */

/*
 * PID 复用的防护：列表数据是几秒前的快照，期间目标进程可能已退出、PID 被分配给无关
 * 进程，只按 PID 终止就会误杀别人。这里在同一个句柄上先核验进程创建时间，再下发
 * TerminateProcess —— 用同一个句柄是为了避免「校验完、手上句柄被换掉」的空档。
 * 无法核验身份（没带创建时间、或读不到创建时间）时一律拒绝执行：宁可不杀，不可错杀。
 */
static PROC_KILL_RESULT OpenVerified(DWORD pid, const FILETIME *expectCreate, HANDLE *out)
{
    HANDLE h;
    FILETIME start;

    *out = NULL;
    if (!expectCreate ||
        (expectCreate->dwHighDateTime == 0 && expectCreate->dwLowDateTime == 0)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return PROC_KILL_FAILED;
    }

    h = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) h = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!h) return PROC_KILL_FAILED;

    if (!ReadStartTime(h, &start)) { CloseHandle(h); return PROC_KILL_FAILED; }

    if (start.dwHighDateTime != expectCreate->dwHighDateTime ||
        start.dwLowDateTime != expectCreate->dwLowDateTime) {
        CloseHandle(h);
        return PROC_KILL_REUSED;
    }

    *out = h;
    return PROC_KILL_OK;
}

PROC_KILL_RESULT ProcTerminate(DWORD pid, const FILETIME *expectCreate)
{
    HANDLE h = NULL;
    PROC_KILL_RESULT r;

    if (pid == 0 || pid == 4) {
        SetLastError(ERROR_ACCESS_DENIED);
        return PROC_KILL_FAILED;
    }
    if (pid == GetCurrentProcessId()) return PROC_KILL_SELF;

    r = OpenVerified(pid, expectCreate, &h);
    if (r != PROC_KILL_OK) return r;

    if (!TerminateProcess(h, 1)) {
        CloseHandle(h);
        return PROC_KILL_FAILED;
    }
    CloseHandle(h);
    return PROC_KILL_OK;
}

typedef struct { DWORD pid; DWORD parent; } PP;
typedef struct { DWORD pid; FILETIME create; } TG;

/*
 * 认亲不能只看 PPID：Windows 的 PPID 只是「创建者当时的 PID」，父进程退出后这个号会被
 * 回收再分配，于是某个无关进程的 PPID 可能恰好等于目标 PID。真正的子进程一定创建在父
 * 进程之后，加上这条时间因果，撞号的无关进程就自动排除。
 * 创建时间读不到时按「不认」处理：宁可漏掉树成员，也不能杀掉不相干的进程。
 */
static BOOL CreatedAfter(const FILETIME *child, const FILETIME *parent)
{
    ULONGLONG c, p;

    if (!child || !parent) return FALSE;
    if ((child->dwHighDateTime | child->dwLowDateTime) == 0) return FALSE;
    if ((parent->dwHighDateTime | parent->dwLowDateTime) == 0) return FALSE;

    c = ((ULONGLONG)child->dwHighDateTime << 32) | child->dwLowDateTime;
    p = ((ULONGLONG)parent->dwHighDateTime << 32) | parent->dwLowDateTime;
    return c > p;
}

/*
 * 先用 PPID 筛出候选，再对候选单独取创建时间——系统里几百个进程里通常只有几个命中，
 * 全量取时间既白付几百次 OpenProcess，又会因为「打不开的进程时间全零」而把它们一律
 * 排除掉（这些其实是别的用户的子进程，属于权限问题而不是不是子进程）。
 * unreadable 统计「因为读不到创建时间而放弃」的候选——这类是真的可能漏掉了树成员，
 * 必须让上层报成 PARTIAL；而创建时间早于父进程的候选是撞号的无关进程，本来就该排除，
 * 属于正常结果，不该给用户弹警告。
 */
static void CollectTree(const PP *arr, size_t n, DWORD parent,
                        const FILETIME *parentCreate,
                        TG *out, size_t *cnt, size_t cap, size_t *unreadable)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        FILETIME create;

        if (arr[i].parent != parent || arr[i].pid == 0) continue;
        if (*cnt >= cap) return;

        if (!ProcGetStartTime(arr[i].pid, &create)) {
            (*unreadable)++;   /* 读不到创建时间：无法证明辈分，只能放弃，但要上报 */
            continue;
        }
        if (!CreatedAfter(&create, parentCreate)) {
            continue;   /* 创建早于父进程：PPID 撞号的无关进程，正确排除 */
        }

        out[*cnt].pid = arr[i].pid;
        out[*cnt].create = create;
        (*cnt)++;
        CollectTree(arr, n, arr[i].pid, &create, out, cnt, cap, unreadable);
    }
}

/*
 * 结束进程树。两层防护缺一不可：
 * 1. 认亲要带时间因果——PPID 只是创建者当时的 PID，父进程退出后号码会被回收再分配，
 *    只看 PPID 会把「PPID 恰好等于目标 PID」的无关进程当成子进程；真正的子进程必定
 *    创建在父进程之后，用创建时间就能把撞号的排除掉。
 * 2. 收进来的每个成员仍要各自带创建时间再核验一次——它们的 PID 也可能在快照到终止
 *    之间被回收复用，身份校验只证明「PID 没换人」，证明不了「它是这棵树的成员」。
 */
PROC_KILL_RESULT ProcTerminateTree(DWORD pid, const FILETIME *expectCreate)
{
    HANDLE root = NULL, snap;
    PROCESSENTRY32W pe;
    PP *arr = NULL;
    size_t n = 0, cap = 256, i;
    TG *targets = NULL;
    size_t cnt = 0;
    size_t unreadable = 0;
    int partial = 0;
    PROC_KILL_RESULT r;

    if (pid == GetCurrentProcessId()) return PROC_KILL_SELF;

    r = OpenVerified(pid, expectCreate, &root);
    if (r != PROC_KILL_OK) return r;

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        /* 拿不到进程快照就只处理已经核验过的根进程 */
        BOOL ok = TerminateProcess(root, 1);
        CloseHandle(root);
        return ok ? PROC_KILL_OK : PROC_KILL_FAILED;
    }

    arr = (PP *)malloc(cap * sizeof(PP));
    if (!arr) { CloseHandle(snap); CloseHandle(root); return PROC_KILL_FAILED; }

    ZeroMemory(&pe, sizeof(pe));
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (n == cap) {
                PP *tmp;
                cap *= 2;
                tmp = (PP *)realloc(arr, cap * sizeof(PP));
                if (!tmp) break;
                arr = tmp;
            }
            arr[n].pid = pe.th32ProcessID;
            arr[n].parent = pe.th32ParentProcessID;
            n++;
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    targets = (TG *)malloc((n + 1) * sizeof(TG));
    if (!targets) { CloseHandle(root); free(arr); return PROC_KILL_FAILED; }

    CollectTree(arr, n, pid, expectCreate, targets, &cnt, n + 1, &unreadable);
    free(arr);

    /* 因为读不到创建时间而放弃的候选必须让用户知道：否则界面显示「成功」，实际漏了子进程 */
    if (unreadable) partial = 1;

    /*
     * 本工具自己落在这棵树里是常见情形：从某个 shell 启动本工具，而用户在结束那个 shell
     * 的进程树。此时整体停手并交给界面提示，否则工具会在操作进行到一半时被自己杀掉。
     */
    for (i = 0; i < cnt; ++i) {
        if (targets[i].pid == GetCurrentProcessId()) {
            CloseHandle(root);
            free(targets);
            return PROC_KILL_SELF;
        }
    }

    /* 先结束子进程，再结束父进程 */
    for (i = cnt; i > 0; --i) {
        if (ProcTerminate(targets[i - 1].pid, &targets[i - 1].create) != PROC_KILL_OK) {
            partial = 1;
        }
    }
    free(targets);

    /*
     * 根进程可能在处理子进程期间自己退出了：句柄还在（内核对象被引用着），但进程已经没了。
     * 这时 TerminateProcess 可能返回 ACCESS_DENIED，而用户的目标其实已经达成，不该报错。
     * 判据用退出码而不是 WaitForSingleObject——后者在进程终止后要过一小会儿才 signaled，
     * 刚退出的瞬间会误判成还活着。
     */
    {
        DWORD code = 0;
        if (GetExitCodeProcess(root, &code) && code != STILL_ACTIVE) {
            CloseHandle(root);
            return partial ? PROC_KILL_PARTIAL : PROC_KILL_OK;
        }
    }

    if (!TerminateProcess(root, 1)) {
        /* 退出码仍是 STILL_ACTIVE 却终止失败：再按等待状态复查一次再报失败 */
        if (WaitForSingleObject(root, 0) == WAIT_OBJECT_0) {
            CloseHandle(root);
            return partial ? PROC_KILL_PARTIAL : PROC_KILL_OK;
        }
        CloseHandle(root);
        return PROC_KILL_FAILED;
    }
    CloseHandle(root);

    return partial ? PROC_KILL_PARTIAL : PROC_KILL_OK;
}

/* ------------------------------------------------------------ 文件定位 */

BOOL ProcOpenFileLocation(HWND hwnd, const WCHAR *path)
{
    WCHAR *arg;
    size_t len;
    HINSTANCE r;

    if (!path || !*path) return FALSE;

    /* 按实际长度分配，避免长路径（\\?\ 前缀，可超过 MAX_PATH）被截断成无效路径 */
    len = wcslen(path) + 16;
    arg = (WCHAR *)malloc(len * sizeof(WCHAR));
    if (!arg) return FALSE;

    wcscpy(arg, L"/select,\"");
    wcscat(arg, path);
    wcscat(arg, L"\"");

    r = ShellExecuteW(hwnd, L"open", L"explorer.exe", arg, NULL, SW_SHOWNORMAL);
    free(arg);
    return (INT_PTR)r > 32;
}

/* -------------------------------------------------------------- 权限 */

BOOL ProcIsElevated(void)
{
    BOOL elevated = FALSE;
    HANDLE tok = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
    PSID adminSid = NULL;

    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        TOKEN_ELEVATION te;
        DWORD len = 0;
        if (GetTokenInformation(tok, TokenElevation, &te, sizeof(te), &len)) {
            elevated = te.TokenIsElevated;
        } else {
            elevated = TRUE; /* 无 UAC 的系统 */
        }
        CloseHandle(tok);
    }

    if (!elevated &&
        AllocateAndInitializeSid(&ntAuth, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminSid)) {
        BOOL member = FALSE;
        if (CheckTokenMembership(NULL, adminSid, &member) && member) elevated = TRUE;
        FreeSid(adminSid);
    }

    return elevated;
}

BOOL ProcElevate(HWND hwnd)
{
    WCHAR exe[MAX_PATH];
    HINSTANCE r;

    if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) return FALSE;

    r = ShellExecuteW(hwnd, L"runas", exe, NULL, NULL, SW_SHOWNORMAL);
    return (INT_PTR)r > 32;
}

BOOL ProcEnableDebugPriv(void)
{
    HANDLE tok = NULL;
    TOKEN_PRIVILEGES tp;
    BOOL ok = FALSE;

    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) {
        return FALSE;
    }

    ZeroMemory(&tp, sizeof(tp));
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    if (LookupPrivilegeValueW(NULL, SE_DEBUG_NAME, &tp.Privileges[0].Luid)) {
        ok = AdjustTokenPrivileges(tok, FALSE, &tp, sizeof(tp), NULL, NULL) &&
             GetLastError() != ERROR_NOT_ALL_ASSIGNED;
    }

    CloseHandle(tok);
    return ok;
}
