/* 入口：初始化公共控件、申请调试权限、进入主界面 */
#include "portview.h"

#pragma comment(lib, "comctl32.lib")

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, PWSTR lpCmdLine, int nCmdShow)
{
    INITCOMMONCONTROLSEX icc;

    (void)hPrev;
    (void)lpCmdLine;

    ZeroMemory(&icc, sizeof(icc));
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    /* 开启 SeDebugPrivilege，便于读取/结束其它用户的进程 */
    ProcEnableDebugPriv();

    return UiRun(hInst, nCmdShow);
}
