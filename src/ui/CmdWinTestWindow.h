#pragma once
#include <windows.h>
#include <string>
class CommandWindow;

struct CmdWinHandles {
    CommandWindow* ps = nullptr;
    CommandWindow* wsl = nullptr;
    HWND hwndPs = nullptr;
    HANDLE psReady = nullptr;
    HANDLE wslReady = nullptr;
};

CmdWinHandles ShowCmdWinTestWindow(HWND parent);

// 向已有的 CmdWin 窗口追加一行文本（带 [HH:MM:SS] 时间戳，线程安全）
void CmdWinAppendText(HWND hwnd, const std::string& line);
