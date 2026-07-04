#pragma once

// 图标
#define IDI_MAIN_ICON                   101

// 控件 ID
#define IDC_DEVICE_COMBO                1001
#define IDC_LOG_EDIT                    1005
#define IDC_TOGGLE_LOG_BUTTON           1009
#define IDC_DETECT_BUTTON               1012
#define IDC_EXIT_BUTTON                 1008

// ── 环境检测指示灯（区域 1） ──────────────────────────────────────────
// 5 项：虚拟化 → WSL 功能 → WSL 内核 → Alpine → usbipd
#define IDC_ENV_VIRT_ICON               1014
#define IDC_ENV_VIRT_TEXT               1015
#define IDC_ENV_WSL_FEATURE_ICON        1016
#define IDC_ENV_WSL_FEATURE_TEXT        1017
#define IDC_ENV_WSL_ICON                1018
#define IDC_ENV_WSL_TEXT                1019
#define IDC_ENV_ALPINE_ICON             1020
#define IDC_ENV_ALPINE_TEXT             1021
#define IDC_ENV_USBIPD_ICON             1022
#define IDC_ENV_USBIPD_TEXT             1023

// 环境操作按钮
#define IDC_ENV_INIT_BUTTON             1024

// ── 挂载操作（区域 3） ────────────────────────────────────────────────
#define IDC_WINDOWS_ACCESS_EDIT         1026
#define IDC_MOUNT_STATUS_TEXT           1027
#define IDC_MOUNT_BUTTON                1028
#define IDC_UNMOUNT_BUTTON              1029

// 消息
#define WM_USER_LOG_UPDATE              (WM_USER + 1)
#define WM_USER_DEVICE_CHANGE           (WM_USER + 2)
#define WM_USER_STATUS_UPDATE           (WM_USER + 3)
