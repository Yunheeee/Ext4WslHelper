#pragma once
#include <string>
#include <windows.h>

// Modal password input dialog for sudo. Returns password (empty if cancelled).
std::string ShowPasswordDialog(HWND parent);

// WSL 首次安装时的用户创建对话框。返回 username + password（均非空表示确认，否则为取消）。
// 返回值格式：username 和 password 通过出参返回
bool ShowWslUserSetupDialog(HWND parent,
                            std::string& outUsername,
                            std::string& outPassword);
