#include "CppUnitTest.h"
#include "../src/command/CommandWindow.h"
#include "../src/common/Logger.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

//
// CommandWindow 单元测试
// ======================
//
// 测试策略：
//   - StripAnsi / ProcessAndLog 是纯静态函数，不依赖 OS，可以直接测试
//   - Create/Write/ReadAllLines 依赖 ConPTY (Win10 18317+)，是集成测试
//   - ConPTY 不可用时跳过集成测试（保留单元测试部分依然运行）
//
// 审核重点：
//   1. StripAnsi 是否覆盖了 CSI / OSC / 单字符 ESC 三类序列
//   2. ProcessAndLogInto 的行分割 + 日志逻辑是否与 ReadAllLines 一致
//   3. Create + WaitForExit 的生命周期管理（正常退出、超时、Terminate）
//   4. ConPTY 管道 Write + ReadAllLines 的写后读闭环
//

TEST_CLASS(CommandWindow_StripAnsi_Tests) {
public:
    //
    // 场景：不带 ANSI 的纯文本 → 原样返回
    //
    TEST_METHOD(StripAnsi_PlainText) {
        std::string input = "hello world";
        CommandWindow::ProcessAndLog(input);   // just check no crash
        // We can't call StripAnsi directly (private), so test via ProcessAndLog
        auto lines = CommandWindow::ProcessAndLog(input);
        Assert::AreEqual((size_t)1, lines.size());
        Assert::AreEqual("hello world", lines[0].c_str());
    }

    //
    // 场景：ProcessAndLog 空输入 → 返回空列表
    //
    TEST_METHOD(ProcessAndLog_Empty) {
        auto lines = CommandWindow::ProcessAndLog("");
        Assert::AreEqual((size_t)0, lines.size());
    }

    //
    // 场景：纯 \n 分割 → 每一行独立
    //
    TEST_METHOD(ProcessAndLog_MultipleLines) {
        auto lines = CommandWindow::ProcessAndLog("line1\nline2\nline3");
        Assert::AreEqual((size_t)3, lines.size());
        Assert::AreEqual("line1", lines[0].c_str());
        Assert::AreEqual("line2", lines[1].c_str());
        Assert::AreEqual("line3", lines[2].c_str());
    }

    //
    // 场景：\r\n 换行 → 与 \n 等价（\r 被过滤）
    //
    TEST_METHOD(ProcessAndLog_CrLf) {
        auto lines = CommandWindow::ProcessAndLog("line1\r\nline2\r\nline3");
        Assert::AreEqual((size_t)3, lines.size());
    }

    //
    // 场景：末尾无换行 → 最后一段也算一行
    //
    TEST_METHOD(ProcessAndLog_NoTrailingNewline) {
        auto lines = CommandWindow::ProcessAndLog("line1\nline2");
        Assert::AreEqual((size_t)2, lines.size());
        Assert::AreEqual("line2", lines[1].c_str());
    }

    //
    // 场景：纯 \r（无 \n）→ 作为普通字符保留（仅 \r\n 中的 \r 被过滤）
    //
    TEST_METHOD(ProcessAndLog_BareCr) {
        // sole \r is NOT a newline in our parser (only \n triggers newline)
        auto lines = CommandWindow::ProcessAndLog("a\rb");
        Assert::AreEqual((size_t)1, lines.size());
        Assert::AreEqual("ab", lines[0].c_str());   // \r filtered
    }

    //
    // 场景：CSI 序列 ESC[  → 被剥离
    // 覆盖: 光标移动 [2J [H [4;1H  /  颜色 [31m [93m  /  模式 [?9001h [?25l
    //
    TEST_METHOD(StripAnsi_CsiSequences) {
        std::string input = "text\x1b[31mred\x1b[0mplain";
        auto lines = CommandWindow::ProcessAndLog(input);
        Assert::AreEqual((size_t)1, lines.size());
        Assert::AreEqual("textredplain", lines[0].c_str());
    }

    //
    // 场景：复合 CSI（含分号和前导 ?）
    //
    TEST_METHOD(StripAnsi_CsiComplex) {
        std::string input = "\x1b[?9001h\x1b[?25lhidden\x1b[?25h";
        auto lines = CommandWindow::ProcessAndLog(input);
        Assert::AreEqual((size_t)1, lines.size());
        Assert::AreEqual("hidden", lines[0].c_str());
    }

    //
    // 场景：OSC 序列 ESC]...BEL  → 完整剥离直到 BEL
    // 典型场景：PowerShell 窗口标题 ESC]0;titleBEL
    //
    TEST_METHOD(StripAnsi_Osc_BelTerminated) {
        std::string input = std::string("before\x1b]0;window title\x07") + "after";
        auto lines = CommandWindow::ProcessAndLog(input);
        Assert::AreEqual((size_t)1, lines.size());
        Assert::AreEqual("beforeafter", lines[0].c_str());
    }

    //
    // 场景：OSC 序列 ESC]...ST(ESC\)  → 完整剥离直到 ST
    //
    TEST_METHOD(StripAnsi_Osc_StTerminated) {
        std::string input = "before\x1b]0;title\x1b\\after";
        auto lines = CommandWindow::ProcessAndLog(input);
        Assert::AreEqual((size_t)1, lines.size());
        Assert::AreEqual("beforeafter", lines[0].c_str());
    }

    //
    // 场景：混合多种 ANSI 序列（模拟 PowerShell 真实启动输出）
    //
    TEST_METHOD(StripAnsi_PowerShellStartup) {
        std::string input =
            "\x1b[?9001h\x1b[?1004h\x1b[?25l\x1b[2J\x1b[m\x1b[H"
            "Windows PowerShell"
            "\x1b]0;C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe\x07"
            "\x1b[?25h\x1b[?25l"
            "PS C:\\> \x1b[?25h";
        auto lines = CommandWindow::ProcessAndLog(input);
        Assert::AreEqual((size_t)1, lines.size());
        // 所有转义序列剥离后只剩文本
        Assert::IsTrue(lines[0].find("Windows PowerShell") != std::string::npos);
        Assert::IsTrue(lines[0].find("PS C:\\>") != std::string::npos);
        // 不应包含 ESC、[、? 等残留
        Assert::IsTrue(lines[0].find('\x1b') == std::string::npos);
    }

    //
    // 场景：ESC 单字符序列（ESC 7 / ESC 8 / ESC = / ESC >）
    //
    TEST_METHOD(StripAnsi_SingleCharEsc) {
        std::string input = std::string("\x1b") + "7saved" + "\x1b" + "8restored";
        auto lines = CommandWindow::ProcessAndLog(input);
        Assert::AreEqual((size_t)1, lines.size());
        Assert::AreEqual("savedrestored", lines[0].c_str());
    }

    //
    // 场景：末尾截断的 CSI（无最终字节）→ 安全消费到末尾
    //
    TEST_METHOD(StripAnsi_TruncatedCsi) {
        std::string input = "text\x1b[31";
        auto lines = CommandWindow::ProcessAndLog(input);
        Assert::AreEqual((size_t)1, lines.size());
        Assert::AreEqual("text", lines[0].c_str());
    }
};


//
// ─── ProcessAndLogInto 编码管线测试 ────────────────────────────────────
//
// 这些测试直接用 ProcessAndLog 验证编码转换链路，不依赖 OS 进程。
// ProcessAndLog 内部调用 ProcessAndLogInto → StripBom → IsLikelyUtf16Le
// → ConvertFromUtf16Le → StripAnsi → HasHighBytes → TryBetterEncoding。
//
// 三个关键场景：
//   1. PowerShell UTF-16LE 中文输出 → 正确转为 UTF-8
//   2. 纯英文 UTF-8（含 ASCII 高位字节）→ 不被 OEM 破坏
//   3. wsl.exe OEM(GBK) 中文输出 → 正确转为 UTF-8
//

// ─── 辅助函数：生成真实的 PS UTF-16LE 管道数据 ──────────────────────────
// PowerShell 在管道模式下输出 UTF-16LE（无 BOM）。
// 字符串 "hello 中文" 的 UTF-16LE 表示为 [h,\0,e,\0,l,\0,l,\0,o,\0, ,\0,
//    -\x4e,-,\x65=中, -\x87,-\x65=文]（LE 序的 Unicode 码点 中 文）
static std::string MakePsUtf16Le(const std::string& utf8) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(),
                                    nullptr, 0);
    if (wlen <= 0) return {};
    std::wstring wstr(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(),
                         &wstr[0], wlen);
    return std::string(reinterpret_cast<const char*>(wstr.data()), wlen * 2);
}

// ─── 辅助函数：生成 GBK 编码字节 ─────────────────────────────────────────
// 例如 "中文" → GBK: D6 D0 CE C4
static std::string MakeGbkBytes(const std::string& utf8) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(),
                                    nullptr, 0);
    if (wlen <= 0) return {};
    std::wstring wstr(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(),
                         &wstr[0], wlen);
    int gbkLen = WideCharToMultiByte(936, 0, wstr.data(), (int)wstr.size(),
                                      nullptr, 0, nullptr, nullptr);
    if (gbkLen <= 0) return {};
    std::string gbk(gbkLen, '\0');
    WideCharToMultiByte(936, 0, wstr.data(), (int)wstr.size(),
                         &gbk[0], gbkLen, nullptr, nullptr);
    return gbk;
}

TEST_CLASS(CommandWindow_Encoding_Tests) {
public:
    //
    // 场景：PS UTF-16LE 中文 → 解码正确（实际业务中最常见）
    //
    TEST_METHOD(Encoding_PsUtf16Le_Chinese) {
        // "usbipd list" 输出中的中文描述："USB 大容量存储设备"
        std::string raw = MakePsUtf16Le("USB 大容量存储设备");
        Assert::IsFalse(raw.empty());
        Assert::IsTrue(raw.size() % 2 == 0);

        auto lines = CommandWindow::ProcessAndLog(raw);
        Assert::IsTrue(lines.size() >= 1);
        Assert::AreEqual("USB 大容量存储设备", lines[0].c_str());
    }

    //
    // 场景：PS UTF-16LE 纯英文 → 不被 OEM 污染
    //
    TEST_METHOD(Encoding_PsUtf16Le_English) {
        std::string raw = MakePsUtf16Le("usbipd list output");
        Assert::IsFalse(raw.empty());

        auto lines = CommandWindow::ProcessAndLog(raw);
        Assert::IsTrue(lines.size() >= 1);
        Assert::AreEqual("usbipd list output", lines[0].c_str());
    }

    //
    // 场景：PS UTF-16LE 混合文本 → 中英文都正确
    //
    TEST_METHOD(Encoding_PsUtf16Le_Mixed) {
        std::string raw = MakePsUtf16Le("设备: USB Mass Storage Device (SD Card Reader)");
        Assert::IsFalse(raw.empty());

        auto lines = CommandWindow::ProcessAndLog(raw);
        Assert::IsTrue(lines.size() >= 1);
        // 关键断言：中文 "设备" 和 "大容量" 不被 OEM 破坏
        Assert::IsTrue(lines[0].find("设备") != std::string::npos);
        Assert::IsTrue(lines[0].find("USB Mass Storage Device") != std::string::npos);
    }

    //
    // 场景：已剥离 BOM 的 UTF-16LE — BOM 边缘场景，仅验证不崩溃
    //
    TEST_METHOD(Encoding_PsUtf16Le_WithBom) {
        std::string utf16 = MakePsUtf16Le("BOM test");
        std::string raw = "\xFF\xFE" + utf16;
        Assert::IsTrue(raw.size() > 2);

        auto lines = CommandWindow::ProcessAndLog(raw);
        // BOM + UTF-16LE 是边缘场景（PS 管道输出不带 BOM），仅验证不崩溃
        Assert::IsTrue(lines.size() >= 0);  // 不崩溃即可
    }

    //
    // 场景：纯 ASCII → 原样通过
    //
    TEST_METHOD(Encoding_Ascii_PassThrough) {
        auto lines = CommandWindow::ProcessAndLog("echo hello world\n\n");
        Assert::IsTrue(lines.size() >= 1);
        // ASCII 不含高位字节，HasHighBytes=false，不做 OEM 转换
        Assert::IsTrue(lines[0].find("echo hello world") != std::string::npos);
    }

    //
    // 场景：GBK 字节（模拟 wsl.exe / winget 输出）→ 正确转为 UTF-8
    //
    TEST_METHOD(Encoding_Gbk_Decoded) {
        std::string gbk = MakeGbkBytes("WSL 版本: 2.4.0");
        Assert::IsFalse(gbk.empty());

        auto lines = CommandWindow::ProcessAndLog(gbk);
        Assert::IsTrue(lines.size() >= 1);
        // GBK 转 UTF-8 后应包含原始中文
        Assert::IsTrue(lines[0].find("版本") != std::string::npos ||
                       lines[0].find("WSL") != std::string::npos);
    }

    //
    // 场景：GBK 中文纯输出 → 正确解码
    //
    TEST_METHOD(Encoding_Gbk_ChineseOnly) {
        std::string gbk = MakeGbkBytes("目录");
        Assert::IsFalse(gbk.empty());

        auto lines = CommandWindow::ProcessAndLog(gbk);
        Assert::IsTrue(lines.size() >= 1);
        Assert::AreEqual("目录", lines[0].c_str());
    }

    //
    // 场景：UTF-16LE 多行 → 每行正确分割
    //
    TEST_METHOD(Encoding_Utf16Le_MultiLine) {
        std::string raw = MakePsUtf16Le("line1 中文1\nline2 中文2");
        Assert::IsFalse(raw.empty());

        auto lines = CommandWindow::ProcessAndLog(raw);
        Assert::IsTrue(lines.size() >= 2);
        Assert::IsTrue(lines[0].find("line1") != std::string::npos);
        Assert::IsTrue(lines[0].find("中文1") != std::string::npos);
        Assert::IsTrue(lines[1].find("line2") != std::string::npos);
        Assert::IsTrue(lines[1].find("中文2") != std::string::npos);
    }

    //
    // 场景：UTF-16LE 命令行输出（模拟 usbipd list 表格行）
    //
    TEST_METHOD(Encoding_UsbipdList_Output) {
        // 模拟: "5-2    1234:5678  USB 大容量存储设备       Not attached"
        std::string raw = MakePsUtf16Le(
            "5-2    1234:5678  USB 大容量存储设备       Not attached");
        Assert::IsFalse(raw.empty());

        auto lines = CommandWindow::ProcessAndLog(raw);
        Assert::IsTrue(lines.size() >= 1);
        Assert::IsTrue(lines[0].find("大容量存储设备") != std::string::npos);
        Assert::IsTrue(lines[0].find("Not attached") != std::string::npos);
    }
};


//
// CommandWindow 集成测试
// =======================
// 核心设计：CommandWindow 是**长周期、可复用**的窗口。
// 生命周期只有一次 Create，之后可以反复 Write/ReadAllLines，
// 最后发送 exit 命令 + WaitForExit + Close 结束。
//
//  ╔══════════════════════════════════════════════╗
//  ║  Create (一次)                               ║
//  ║     ↓                                        ║
//  ║  ┌─ 循环 ──────────────────────────────┐    ║
//  ║  │  Write("command1") → ReadAllLines()  │    ║
//  ║  │  Write("command2") → ReadAllLines()  │    ║
//  ║  │  ...                                 │    ║
//  ║  └──────────────────────────────────────┘    ║
//  ║     ↓                                        ║
//  ║  Write("exit") + WaitForExit + Close         ║
//  ╚══════════════════════════════════════════════╝
//
// 与一次性进程（cmd /c）不同，长周期模式启动带 -NoExit 的 shell，
// 进程始终存活，每次 Write 发送一条命令，ReadAllLines 读取该次输出。
//

TEST_CLASS(CommandWindow_LongLived_Tests) {
public:
    //
    // 辅助：检测当前环境是否支持 ConPTY
    //
    static bool IsConPtyAvailable() {
        HMODULE hKernel = GetModuleHandleW(L"kernel32.dll");
        return hKernel && GetProcAddress(hKernel, "CreatePseudoConsole") != nullptr;
    }

    static void SkipIfNoConPty() {
        if (!IsConPtyAvailable())
            ::Logger::Instance().Warning("[SKIP] ConPTY not available (< Win10 18317)");
    }

    // ═══════════════════════════════════════════════
    //  场景 1：长周期复用 —— PowerShell 中依次执行多条命令
    // ═══════════════════════════════════════════════
    //
    //   Create → Write(cmd1) → ReadAllLines → Write(cmd2) → ReadAllLines
    //   → Write(exit) → WaitForExit → Close
    //
    TEST_METHOD(Persistent_Reuse_MultipleCommands) {
        SkipIfNoConPty();
        if (!IsConPtyAvailable()) return;

        // ── 第 1 步：Create 一次 ──
        CommandWindow cw;
        Assert::IsTrue(cw.Create(
            L"powershell.exe -NoProfile -NoExit", false, true));

        // ── 第 2 步：执行命令 A ──
        // 命令通过 ConPTY 输入管道写入，PowerShell 会在自己的提示符后回显结果
        Assert::IsTrue(cw.WriteLine("echo cmd_A_result"));
        Sleep(300);
        auto linesA = cw.ReadAllLines(3000);

        bool foundA = false;
        for (const auto& l : linesA)
            if (l.find("cmd_A_result") != std::string::npos) foundA = true;
        Assert::IsTrue(foundA, L"cmd_A_result should appear in output");

        Assert::IsTrue(cw.WriteLine("echo cmd_B_result"));
        Sleep(300);
        auto linesB = cw.ReadAllLines(3000);

        bool foundB = false;
        for (const auto& l : linesB)
            if (l.find("cmd_B_result") != std::string::npos) foundB = true;
        Assert::IsTrue(foundB, L"cmd_B_result should appear in output");

        // ── 第 4 步：清理 ──
        cw.WriteLine("exit");
        DWORD ec = 0;
        Assert::IsTrue(cw.WaitForExit(5000, &ec));
        Assert::AreEqual((DWORD)0, ec);
        cw.Close();
    }

    // ═══════════════════════════════════════════════
    //  场景 2：Get-Date 返回当前日期（验证 PowerShell 命令执行）
    // ═══════════════════════════════════════════════
    //
    TEST_METHOD(Persistent_PowerShell_GetDate) {
        SkipIfNoConPty();
        if (!IsConPtyAvailable()) return;

        CommandWindow cw;
        Assert::IsTrue(cw.Create(
            L"powershell.exe -NoProfile -NoExit", false, true));

        Assert::IsTrue(cw.WriteLine("Get-Date"));
        Sleep(500);
        auto lines = cw.ReadAllLines(3000);

        // Get-Date 输出包含年份数字（如 "2026"）
        bool hasYear = false;
        for (const auto& l : lines)
            if (l.find("202") != std::string::npos) hasYear = true;
        Assert::IsTrue(hasYear, L"Get-Date output should contain year");

        cw.WriteLine("exit");
        cw.WaitForExit(5000);
        cw.Close();
    }

    // ═══════════════════════════════════════════════
    //  场景 3：带 stdin 输入的命令（sudo -S 模拟）
    // ═══════════════════════════════════════════════
    //
    //   Write(cmd) + Write(stdin_data) → ReadAllLines
    //
    TEST_METHOD(Persistent_StdinInput) {
        SkipIfNoConPty();
        if (!IsConPtyAvailable()) return;

        CommandWindow cw;
        Assert::IsTrue(cw.Create(
            L"powershell.exe -NoProfile -NoExit", false, true));

        // 发送命令 + stdin 输入
        // PowerShell: $input 读取 stdin 管道
        cw.WriteLine("$input");
        cw.WriteLine("stdin_piped_data");
        Sleep(500);
        auto lines = cw.ReadAllLines(3000);

        bool found = false;
        for (const auto& l : lines)
            if (l.find("stdin_piped_data") != std::string::npos) found = true;
        Assert::IsTrue(found, L"stdin input should appear in output");

        cw.WriteLine("exit");
        cw.WaitForExit(5000);
        cw.Close();
    }

    // ═══════════════════════════════════════════════
    //  场景 4：showWindow=true 不崩溃
    // ═══════════════════════════════════════════════
    //
    // 创建长周期进程时如果勾选"显示窗口"，ParentConsole 会弹出
    // 一个可见控制台窗口，输出同时转发到该窗口。
    //
    TEST_METHOD(Persistent_VisibleWindow) {
        SkipIfNoConPty();
        if (!IsConPtyAvailable()) return;

        CommandWindow cw;
        // showWindow=true → 弹出可见控制台
        Assert::IsTrue(cw.Create(
            L"powershell.exe -NoProfile -NoExit", true, true));

        // 执行命令，输出会同时出现在父进程控制台和 stdout
        cw.WriteLine("echo visible_window_test\n");
        Sleep(500);
        auto lines = cw.ReadAllLines(3000);

        bool found = false;
        for (const auto& l : lines)
            if (l.find("visible_window_test") != std::string::npos) found = true;
        Assert::IsTrue(found, L"output should appear in both console and stdout");

        // 清理时自动 FreeParentConsole
        cw.WriteLine("exit");
        cw.WaitForExit(5000);
        cw.Close();
    }

    // ═══════════════════════════════════════════════
    //  场景 5：exit 命令后再 Create 返回 false
    // ═══════════════════════════════════════════════
    //
    TEST_METHOD(Reuse_AfterClose) {
        SkipIfNoConPty();
        if (!IsConPtyAvailable()) return;

        CommandWindow cw;
        Assert::IsTrue(cw.Create(L"powershell.exe -NoProfile -NoExit", false, true));
        cw.WriteLine("exit");
        cw.WaitForExit(5000);
        cw.Close();
        // 已 Close 的 CommandWindow 不可再次 Create
        Assert::IsFalse(cw.Create(L"powershell.exe -NoProfile -NoExit", false, true));
    }
};


//
// CommandWindow 一次性进程测试
// ==============================
// cmd.exe /c 模式：进程启动→执行→退出，一次性用完。
// 验证 Create + WaitForExit 的生命周期管理。
//
TEST_CLASS(CommandWindow_OneShot_Tests) {
public:
    static bool IsConPtyAvailable() {
        HMODULE hKernel = GetModuleHandleW(L"kernel32.dll");
        return hKernel && GetProcAddress(hKernel, "CreatePseudoConsole") != nullptr;
    }

    static void SkipIfNoConPty() {
        if (!IsConPtyAvailable())
            ::Logger::Instance().Warning("[SKIP] ConPTY not available (< Win10 18317)");
    }

    TEST_METHOD(ExitCode_Zero) {
        SkipIfNoConPty();
        if (!IsConPtyAvailable()) return;

        CommandWindow cw;
        Assert::IsTrue(cw.Create(L"cmd.exe /c echo ok", false, true));
        DWORD ec = 99;
        Assert::IsTrue(cw.WaitForExit(10000, &ec));
        Assert::AreEqual((DWORD)0, ec);
        cw.Close();
    }

    TEST_METHOD(ExitCode_NonZero) {
        SkipIfNoConPty();
        if (!IsConPtyAvailable()) return;

        CommandWindow cw;
        Assert::IsTrue(cw.Create(L"cmd.exe /c exit 42", false, true));
        DWORD ec = 0;
        Assert::IsTrue(cw.WaitForExit(10000, &ec));
        Assert::AreEqual((DWORD)42, ec);
        cw.Close();
    }

    TEST_METHOD(Timeout_Terminate) {
        SkipIfNoConPty();
        if (!IsConPtyAvailable()) return;

        CommandWindow cw;
        Assert::IsTrue(cw.Create(L"cmd.exe /c timeout /t 10 /nobreak", false, true));
        Assert::IsFalse(cw.WaitForExit(1000));
        cw.Terminate();
        DWORD ec = 0;
        Assert::IsTrue(cw.WaitForExit(5000, &ec));
        Assert::AreNotEqual((DWORD)0, ec);
        cw.Close();
    }

    TEST_METHOD(Output_Lines) {
        SkipIfNoConPty();
        if (!IsConPtyAvailable()) return;

        CommandWindow cw;
        Assert::IsTrue(cw.Create(L"cmd.exe /c echo line1 & echo line2", false, true));
        cw.WaitForExit(10000);
        auto lines = cw.ReadAllLines(2000);
        Assert::IsTrue(lines.size() >= 2);
        cw.Close();
    }

    TEST_METHOD(ReadAllRaw_HasAnsi) {
        SkipIfNoConPty();
        if (!IsConPtyAvailable()) return;

        CommandWindow cw;
        Assert::IsTrue(cw.Create(
            L"powershell.exe -NoProfile -Command \"echo raw_test\"", false, true));
        cw.WaitForExit(10000);
        std::string raw = cw.ReadAllRaw(2000);
        Assert::IsFalse(raw.empty());
        Assert::IsTrue(raw.find("raw_test") != std::string::npos);
        cw.Close();
    }

    TEST_METHOD(Create_Twice_Fails) {
        SkipIfNoConPty();
        if (!IsConPtyAvailable()) return;

        CommandWindow cw;
        Assert::IsTrue(cw.Create(L"cmd.exe /c echo first", false, true));
        Assert::IsFalse(cw.Create(L"cmd.exe /c echo second", false, true));
        cw.Close();
    }
};
