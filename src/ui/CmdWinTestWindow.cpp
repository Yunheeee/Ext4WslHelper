// Debug display window — receives executor output via CmdWinAppendText
#include "CmdWinTestWindow.h"

#ifdef _DEBUG

#include "../common/Utils.h"
#include "../common/Logger.h"
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")
#include <string>
#include <vector>
#include <sstream>
#include <ctime>
#include <iomanip>

#define IDC_OUTPUT  4001
#define WM_APPEND   (WM_USER + 60)

static const wchar_t* WC = L"CmdWinDebug";
static HWND g_debugHwnd = nullptr;

static std::string ts() { auto t = std::time(nullptr); std::tm tm; localtime_s(&tm, &t); std::ostringstream ss; ss << std::put_time(&tm, "%H:%M:%S"); return ss.str(); }

void CmdWinAppendText(HWND hwnd, const std::string& line) {
    if (!hwnd) hwnd = g_debugHwnd;
    if (!hwnd) return;
    std::wstring w = utils::Utf8ToWString("[" + ts() + "] " + line + "\r\n");
    wchar_t* p = new wchar_t[w.size() + 1];
    wcscpy_s(p, w.size() + 1, w.c_str());
    PostMessageW(hwnd, WM_APPEND, (WPARAM)GetDlgItem(hwnd, IDC_OUTPUT), (LPARAM)p);
}

static LRESULT CALLBACK WP(HWND hwnd, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE: {
        HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                         WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,
                         4,4,500,300, hwnd,(HMENU)IDC_OUTPUT,nullptr,nullptr);
        HFONT f = CreateFontW(-13,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,FIXED_PITCH|FF_MODERN,L"Consolas");
        SendMessageW(h,WM_SETFONT,(WPARAM)f,TRUE); SendMessageW(h,EM_SETLIMITTEXT,0x7FFFFFFF,0);
        return 0;
    }
    case WM_SIZE: { RECT rc; GetClientRect(hwnd,&rc); SetWindowPos(GetDlgItem(hwnd,IDC_OUTPUT),nullptr,4,4,rc.right-8,rc.bottom>8?rc.bottom-8:100,SWP_NOZORDER); return 0; }
    case WM_GETMINMAXINFO: ((MINMAXINFO*)lp)->ptMinTrackSize={500,250}; return 0;
    case WM_APPEND: {
        wchar_t* t=(wchar_t*)lp; HWND h=(HWND)wp;
        if(t&&h){int n=GetWindowTextLengthW(h);SendMessageW(h,EM_SETSEL,n,n);SendMessageW(h,EM_REPLACESEL,FALSE,(LPARAM)t);SendMessageW(h,EM_SCROLLCARET,0,0);delete[]t;}
        return 0;
    }
    case WM_DESTROY: { g_debugHwnd=nullptr; return 0; }
    }
    return DefWindowProcW(hwnd,m,wp,lp);
}

CmdWinHandles ShowCmdWinTestWindow(HWND parent) {
    static bool reg=false; if(!reg){static bool icc=false;if(!icc){INITCOMMONCONTROLSEX x={sizeof(x)};x.dwICC=ICC_STANDARD_CLASSES;InitCommonControlsEx(&x);icc=true;}WNDCLASSEXW wc={};wc.cbSize=sizeof(wc);wc.style=CS_HREDRAW|CS_VREDRAW;wc.lpfnWndProc=WP;wc.hInstance=GetModuleHandleW(nullptr);wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);wc.lpszClassName=WC;RegisterClassExW(&wc);reg=true;}
    int ww=610,wh=430,x=CW_USEDEFAULT,y=CW_USEDEFAULT;
    if(parent&&IsWindow(parent)){RECT r;GetWindowRect(parent,&r);x=r.left+(r.right-r.left-ww)/2;y=r.top+(r.bottom-r.top-wh)/2;if(x<0)x=0;if(y<0)y=0;}

    CmdWinHandles h{};
    HANDLE hEvt=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    h.hwndPs=CreateWindowExW(0,WC,L"CmdWin — PowerShell",WS_CAPTION|WS_SYSMENU|WS_VISIBLE|WS_POPUP|WS_THICKFRAME|WS_MAXIMIZEBOX,x,y,ww,wh,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(h.hwndPs){g_debugHwnd=h.hwndPs;ShowWindow(h.hwndPs,SW_SHOW);UpdateWindow(h.hwndPs);h.psReady=hEvt;SetEvent(h.psReady);}
    else CloseHandle(hEvt);
    return h;
}
#endif
