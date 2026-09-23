/* 端口/连接枚举：TCP、UDP，IPv4 与 IPv6，含占用进程 PID */
#include "portview.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

typedef struct {
    PORT_ENTRY *items;
    size_t count;
    size_t cap;
} PORT_VEC;

typedef struct {
    DWORD pid;
    WCHAR name[MAX_PATH];
    WCHAR path[MAX_PATH];
    FILETIME create;
} PROC_CACHE;

/* 进程信息缓存，只在一次 PortsEnumerate 期间存在。枚举目前始终在 UI 线程同步执行，
 * 没有并发访问所以不加锁；若将来把枚举挪到后台线程，这三个变量必须一起迁走 */
static PROC_CACHE *g_cache = NULL;
static size_t g_cacheN = 0;
static size_t g_cacheCap = 0;

static int VecPush(PORT_VEC *v, const PORT_ENTRY *e)
{
    if (v->count == v->cap) {
        size_t nc = v->cap ? v->cap * 2 : 256;
        PORT_ENTRY *p = (PORT_ENTRY *)realloc(v->items, nc * sizeof(PORT_ENTRY));
        if (!p) return 0;
        v->items = p;
        v->cap = nc;
    }
    v->items[v->count++] = *e;
    return 1;
}

const WCHAR *PortsStateText(DWORD state)
{
    switch (state) {
    case MIB_TCP_STATE_CLOSED:     return L"已关闭";
    case MIB_TCP_STATE_LISTEN:     return L"监听";
    case MIB_TCP_STATE_SYN_SENT:   return L"SYN 已发送";
    case MIB_TCP_STATE_SYN_RCVD:   return L"SYN 已接收";
    case MIB_TCP_STATE_ESTAB:      return L"已建立";
    case MIB_TCP_STATE_FIN_WAIT1:  return L"FIN 等待 1";
    case MIB_TCP_STATE_FIN_WAIT2:  return L"FIN 等待 2";
    case MIB_TCP_STATE_CLOSE_WAIT: return L"关闭等待";
    case MIB_TCP_STATE_CLOSING:    return L"正在关闭";
    case MIB_TCP_STATE_LAST_ACK:   return L"最后确认";
    case MIB_TCP_STATE_TIME_WAIT:  return L"时间等待";
    case MIB_TCP_STATE_DELETE_TCB: return L"已删除";
    default:                       return L"未知";
    }
}

static void FmtAddr(int family, void *addr, DWORD scope, WCHAR *buf, size_t cch)
{
    WCHAR tmp[64];
    tmp[0] = 0;

    InetNtopW(family, (PVOID)addr, tmp, 64);
    tmp[63] = 0;

    if (family == AF_INET6 && scope) {
        _snwprintf(buf, cch, L"%s%%%u", tmp, scope);
    } else {
        wcsncpy(buf, tmp, cch - 1);
    }
    buf[cch - 1] = 0;
}

/* 数值列的文本在枚举时一次性格式化好，虚拟列表按需取文本时无需再转换 */
static void FillDerived(PORT_ENTRY *e)
{
    _snwprintf(e->portText, 16, L"%u", e->localPort);
    e->portText[15] = 0;

    if (e->remotePort || e->remoteAddr[0]) {
        _snwprintf(e->rportText, 16, L"%u", e->remotePort);
    } else {
        e->rportText[0] = 0;
    }
    e->rportText[15] = 0;

    _snwprintf(e->pidText, 16, L"%u", e->pid);
    e->pidText[15] = 0;
}

/* ------------------------------------------------------------ 进程缓存 */

static void CacheReset(void)
{
    free(g_cache);
    g_cache = NULL;
    g_cacheN = 0;
    g_cacheCap = 0;
}

/* g_cache 始终按 pid 升序维护，查找走二分：每一条连接的枚举都要查一次缓存，
 * 改成线性扫描时是「连接数 × 缓存条目数」次跨大结构体的地址跳跃 */
static size_t CacheLowerBound(DWORD pid)
{
    size_t lo = 0, hi = g_cacheN;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (g_cache[mid].pid < pid) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

static const PROC_CACHE *CacheLookup(DWORD pid)
{
    size_t pos = CacheLowerBound(pid);
    if (pos < g_cacheN && g_cache[pos].pid == pid) return &g_cache[pos];
    return NULL;
}

static void CacheStore(DWORD pid, const WCHAR *name, const WCHAR *path, const FILETIME *create)
{
    size_t pos;
    PROC_CACHE c;

    if (g_cacheN == g_cacheCap) {
        size_t nc = g_cacheCap ? g_cacheCap * 2 : 64;
        PROC_CACHE *p = (PROC_CACHE *)realloc(g_cache, nc * sizeof(PROC_CACHE));
        /* 扩容失败就放弃写入：当前这条记录的字段已经填好，只是下次遇到同一 PID 要重新解析一遍，
         * 属于内存紧张时的降级，不需要让调用方改变行为 */
        if (!p) return;
        g_cache = p;
        g_cacheCap = nc;
    }

    pos = CacheLowerBound(pid);
    if (pos < g_cacheN) {
        memmove(&g_cache[pos + 1], &g_cache[pos], (g_cacheN - pos) * sizeof(PROC_CACHE));
    }

    c.pid = pid;
    wcsncpy(c.name, name, MAX_PATH - 1);
    c.name[MAX_PATH - 1] = 0;
    wcsncpy(c.path, path, MAX_PATH - 1);
    c.path[MAX_PATH - 1] = 0;
    c.create = *create;
    g_cache[pos] = c;
    g_cacheN++;
}

static void ResolveProc(PORT_ENTRY *e, const PROC_INFO *procs, size_t procCount)
{
    const PROC_CACHE *hit;
    WCHAR name[MAX_PATH], path[MAX_PATH];
    FILETIME create;
    const PROC_INFO *pi;
    const WCHAR *base, *q;

    ZeroMemory(&create, sizeof(create));

    hit = CacheLookup(e->pid);
    if (hit) {
        wcsncpy(e->procName, hit->name, 63);
        e->procName[63] = 0;
        wcsncpy(e->procPath, hit->path, MAX_PATH - 1);
        e->procPath[MAX_PATH - 1] = 0;
        e->procCreate = hit->create;
        e->procCreateValid = (hit->create.dwHighDateTime != 0 || hit->create.dwLowDateTime != 0);
        return;
    }

    name[0] = 0;
    path[0] = 0;

    if (e->pid == 0) {
        wcsncpy(name, L"System Idle Process", MAX_PATH - 1);
    } else if (e->pid == 4 && !ProcGetPathAndStart(4, path, MAX_PATH, &create)) {
        wcsncpy(name, L"System", MAX_PATH - 1);
    } else {
        pi = ProcFind(procs, procCount, e->pid);
        if (pi) wcsncpy(name, pi->name, MAX_PATH - 1);
        ProcGetPathAndStart(e->pid, path, MAX_PATH, &create);
        if (!name[0] && path[0]) {
            base = path;
            for (q = path; *q; ++q) {
                if (*q == L'\\' || *q == L'/') base = q + 1;
            }
            wcsncpy(name, base, MAX_PATH - 1);
        }
    }
    name[MAX_PATH - 1] = 0;
    path[MAX_PATH - 1] = 0;

    if (!name[0]) _snwprintf(name, MAX_PATH, L"PID %u", e->pid);

    CacheStore(e->pid, name, path, &create);
    wcsncpy(e->procName, name, 63);
    e->procName[63] = 0;
    wcsncpy(e->procPath, path, MAX_PATH - 1);
    e->procPath[MAX_PATH - 1] = 0;
    e->procCreate = create;
    e->procCreateValid = (create.dwHighDateTime != 0 || create.dwLowDateTime != 0);
}

/* ------------------------------------------------------------ 各类表 */

static BOOL AddTcp4(PORT_VEC *v, const PROC_INFO *procs, size_t procCount)
{
    PMIB_TCPTABLE_OWNER_PID table = NULL;
    DWORD size = 0, r, i;

    r = GetExtendedTcpTable(NULL, &size, TRUE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    if (r != ERROR_INSUFFICIENT_BUFFER || size == 0) return FALSE;

    table = (PMIB_TCPTABLE_OWNER_PID)malloc(size);
    if (!table) return FALSE;

    r = GetExtendedTcpTable(table, &size, TRUE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    if (r != NO_ERROR) { free(table); return FALSE; }

    for (i = 0; i < table->dwNumEntries; ++i) {
        MIB_TCPROW_OWNER_PID *row = &table->table[i];
        PORT_ENTRY e;
        IN_ADDR a;

        ZeroMemory(&e, sizeof(e));
        wcsncpy(e.proto, L"TCP", 7);

        a.S_un.S_addr = row->dwLocalAddr;
        FmtAddr(AF_INET, &a, 0, e.localAddr, 64);
        e.localPort = ntohs((u_short)row->dwLocalPort);

        /* 监听行没有对端，内核给的是 0.0.0.0:0；照抄会显示出一个并不存在的远端地址，
         * 而且 "0.0.0.0" 非空会让 FillDerived 把远端端口也格式化成 0，与 UDP 分支保持一致 */
        if (row->dwState == MIB_TCP_STATE_LISTEN) {
            e.remoteAddr[0] = 0;
            e.remotePort = 0;
        } else {
            a.S_un.S_addr = row->dwRemoteAddr;
            FmtAddr(AF_INET, &a, 0, e.remoteAddr, 64);
            e.remotePort = ntohs((u_short)row->dwRemotePort);
        }

        wcsncpy(e.state, PortsStateText(row->dwState), 23);
        e.pid = row->dwOwningPid;
        ResolveProc(&e, procs, procCount);

        FillDerived(&e);

        if (!VecPush(v, &e)) break;
    }
    free(table);
    return TRUE;
}

static BOOL AddTcp6(PORT_VEC *v, const PROC_INFO *procs, size_t procCount)
{
    PMIB_TCP6TABLE_OWNER_PID table = NULL;
    DWORD size = 0, r, i;

    r = GetExtendedTcpTable(NULL, &size, TRUE, AF_INET6, TCP_TABLE_OWNER_PID_ALL, 0);
    if (r != ERROR_INSUFFICIENT_BUFFER || size == 0) return FALSE;

    table = (PMIB_TCP6TABLE_OWNER_PID)malloc(size);
    if (!table) return FALSE;

    r = GetExtendedTcpTable(table, &size, TRUE, AF_INET6, TCP_TABLE_OWNER_PID_ALL, 0);
    if (r != NO_ERROR) { free(table); return FALSE; }

    for (i = 0; i < table->dwNumEntries; ++i) {
        MIB_TCP6ROW_OWNER_PID *row = &table->table[i];
        PORT_ENTRY e;

        ZeroMemory(&e, sizeof(e));
        wcsncpy(e.proto, L"TCP6", 7);

        FmtAddr(AF_INET6, row->ucLocalAddr, row->dwLocalScopeId, e.localAddr, 64);
        e.localPort = ntohs((u_short)row->dwLocalPort);
        if (row->dwState == MIB_TCP_STATE_LISTEN) {
            e.remoteAddr[0] = 0;
            e.remotePort = 0;
        } else {
            FmtAddr(AF_INET6, row->ucRemoteAddr, row->dwRemoteScopeId, e.remoteAddr, 64);
            e.remotePort = ntohs((u_short)row->dwRemotePort);
        }

        wcsncpy(e.state, PortsStateText(row->dwState), 23);
        e.pid = row->dwOwningPid;
        ResolveProc(&e, procs, procCount);

        FillDerived(&e);

        if (!VecPush(v, &e)) break;
    }
    free(table);
    return TRUE;
}

static BOOL AddUdp4(PORT_VEC *v, const PROC_INFO *procs, size_t procCount)
{
    PMIB_UDPTABLE_OWNER_PID table = NULL;
    DWORD size = 0, r, i;

    r = GetExtendedUdpTable(NULL, &size, TRUE, AF_INET, UDP_TABLE_OWNER_PID, 0);
    if (r != ERROR_INSUFFICIENT_BUFFER || size == 0) return FALSE;

    table = (PMIB_UDPTABLE_OWNER_PID)malloc(size);
    if (!table) return FALSE;

    r = GetExtendedUdpTable(table, &size, TRUE, AF_INET, UDP_TABLE_OWNER_PID, 0);
    if (r != NO_ERROR) { free(table); return FALSE; }

    for (i = 0; i < table->dwNumEntries; ++i) {
        MIB_UDPROW_OWNER_PID *row = &table->table[i];
        PORT_ENTRY e;
        IN_ADDR a;

        ZeroMemory(&e, sizeof(e));
        wcsncpy(e.proto, L"UDP", 7);

        a.S_un.S_addr = row->dwLocalAddr;
        FmtAddr(AF_INET, &a, 0, e.localAddr, 64);
        e.localPort = ntohs((u_short)row->dwLocalPort);
        e.remoteAddr[0] = 0;
        e.remotePort = 0;
        e.state[0] = 0;

        e.pid = row->dwOwningPid;
        ResolveProc(&e, procs, procCount);

        FillDerived(&e);

        if (!VecPush(v, &e)) break;
    }
    free(table);
    return TRUE;
}

static BOOL AddUdp6(PORT_VEC *v, const PROC_INFO *procs, size_t procCount)
{
    PMIB_UDP6TABLE_OWNER_PID table = NULL;
    DWORD size = 0, r, i;

    r = GetExtendedUdpTable(NULL, &size, TRUE, AF_INET6, UDP_TABLE_OWNER_PID, 0);
    if (r != ERROR_INSUFFICIENT_BUFFER || size == 0) return FALSE;

    table = (PMIB_UDP6TABLE_OWNER_PID)malloc(size);
    if (!table) return FALSE;

    r = GetExtendedUdpTable(table, &size, TRUE, AF_INET6, UDP_TABLE_OWNER_PID, 0);
    if (r != NO_ERROR) { free(table); return FALSE; }

    for (i = 0; i < table->dwNumEntries; ++i) {
        MIB_UDP6ROW_OWNER_PID *row = &table->table[i];
        PORT_ENTRY e;

        ZeroMemory(&e, sizeof(e));
        wcsncpy(e.proto, L"UDP6", 7);

        FmtAddr(AF_INET6, &row->ucLocalAddr, row->dwLocalScopeId, e.localAddr, 64);
        e.localPort = ntohs((u_short)row->dwLocalPort);
        e.remoteAddr[0] = 0;
        e.remotePort = 0;
        e.state[0] = 0;

        e.pid = row->dwOwningPid;
        ResolveProc(&e, procs, procCount);

        FillDerived(&e);

        if (!VecPush(v, &e)) break;
    }
    free(table);
    return TRUE;
}

void PortsFree(PORT_ENTRY *entries)
{
    free(entries);
}

int PortsEnumerate(PORT_ENTRY **entries, size_t *count)
{
    PORT_VEC v;
    PROC_INFO *procs = NULL;
    size_t procCount = 0;
    BOOL anyTable = FALSE;

    *entries = NULL;
    *count = 0;

    v.items = NULL;
    v.count = 0;
    v.cap = 0;

    ProcSnapshot(&procs, &procCount);
    CacheReset();

    anyTable |= AddTcp4(&v, procs, procCount);
    anyTable |= AddTcp6(&v, procs, procCount);
    anyTable |= AddUdp4(&v, procs, procCount);
    anyTable |= AddUdp6(&v, procs, procCount);

    ProcFree(procs);
    CacheReset();

    /* 四张表一张都没读出来时用返回值告诉界面是读取失败，而不是「当前没有端口」 */
    if (!anyTable) {
        free(v.items);
        return 0;
    }

    *entries = v.items;
    *count = v.count;
    return 1;
}
