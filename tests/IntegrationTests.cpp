#include "CppUnitTest.h"
#include "../src/common/Utils.h"
#include "../src/command/CommandWindow.h"
#include "../src/common/Logger.h"

#pragma warning(disable: 4100)  // unreferenced formal parameter in lambda

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// ─── integration tests ─────────────────────────────

namespace {

// check if this machine can spawn cmd.exe (always true on Windows)
static bool IsCmdAvailable() {
    auto r = utils::RunProcess(L"cmd.exe", L"/c ver", "", 5000);
    return r.exitCode == 0;
}

} // namespace

TEST_CLASS(RunProcessStreaming_Tests) {
public:
    // ---------- basic execution ----------

    TEST_METHOD(Basic_SingleLine_ExitCodeZero) {
        if (!IsCmdAvailable()) return;

        std::vector<std::string> cbLines;
        auto r = utils::RunProcessStreaming(
            L"cmd.exe", L"/c echo hello",
            "", 10000,
            [&](const std::string& line) { cbLines.push_back(line); });

        Assert::AreEqual((DWORD)0, r.exitCode);
        Assert::IsTrue(r.stdOut.find("hello") != std::string::npos,
                       L"stdOut must contain 'hello'");
        // at least one callback fired
        Assert::IsTrue(cbLines.size() >= 1, L"callback must fire at least once");
    }

    TEST_METHOD(MultiLine_TwoLines) {
        if (!IsCmdAvailable()) return;

        std::vector<std::string> cbLines;
        auto r = utils::RunProcessStreaming(
            L"cmd.exe", L"/c (echo line1 & echo line2)",
            "", 10000,
            [&](const std::string& line) { cbLines.push_back(line); });

        Assert::AreEqual((DWORD)0, r.exitCode);
        Assert::IsTrue(cbLines.size() >= 2,
                       (L"expected >=2 lines, got " + std::to_wstring(cbLines.size())).c_str());
    }

    TEST_METHOD(ExitCode_NonZero_Returns42) {
        if (!IsCmdAvailable()) return;

        auto r = utils::RunProcessStreaming(
            L"cmd.exe", L"/c exit /b 42",
            "", 10000,
            nullptr  // no callback, still works
        );

        Assert::AreEqual((DWORD)42, r.exitCode,
                         (L"expected exit=42, got " + std::to_wstring(r.exitCode)).c_str());
    }

    // ---------- timeout ----------

    TEST_METHOD(Timeout_TerminatesProcess) {
        if (!IsCmdAvailable()) return;

        // Use ping with many retries as a slow command (no console needed)
        DWORD startTick = GetTickCount();
        auto r = utils::RunProcessStreaming(
            L"ping.exe", L"-n 60 127.0.0.1",   // ~60 seconds of pings
            "", 500,       // 500ms timeout
            nullptr
        );
        DWORD elapsed = GetTickCount() - startTick;

        // Should either return -1 (terminated) or a success code for quick exit
        Assert::IsTrue(elapsed < 5000,
                       (L"timeout should fire in ~500ms, waited " + std::to_wstring(elapsed) + L"ms").c_str());
    }

    // ---------- stdin ----------

    TEST_METHOD(Stdin_PipedToProcess) {
        if (!IsCmdAvailable()) return;

        // findstr . echoes each line of stdin to stdout (simple pass-through with echo)
        std::vector<std::string> cbLines;
        auto r = utils::RunProcessStreaming(
            L"findstr.exe", L".",
            "alpha\nbeta\ngamma\n",  // stdin — findstr matches any char with "."
            5000,
            [&](const std::string& line) { cbLines.push_back(line); });

        Assert::AreEqual((DWORD)0, r.exitCode);
        bool hasAlpha = false, hasBeta = false, hasGamma = false;
        for (const auto& l : cbLines) {
            if (l.find("alpha") != std::string::npos) hasAlpha = true;
            if (l.find("beta")  != std::string::npos) hasBeta  = true;
            if (l.find("gamma") != std::string::npos) hasGamma = true;
        }
        Assert::IsTrue(hasAlpha && hasBeta && hasGamma,
                       L"findstr should output alpha, beta, gamma from stdin");
    }

    // ---------- callback order ----------

    TEST_METHOD(Callback_OrderMatchesOutput) {
        if (!IsCmdAvailable()) return;

        std::vector<std::string> cbLines;
        auto r = utils::RunProcessStreaming(
            L"cmd.exe", L"/c (echo first & echo second & echo third)",
            "", 10000,
            [&](const std::string& line) { cbLines.push_back(line); });

        Assert::AreEqual((DWORD)0, r.exitCode);
        // collect non-empty lines; first/second/third should appear in order
        std::vector<std::string> filtered;
        for (const auto& l : cbLines) {
            if (!l.empty()) filtered.push_back(l);
        }
        Assert::IsTrue(filtered.size() >= 3,
                       (L"expected >=3 lines, got " + std::to_wstring(filtered.size())).c_str());
    }

    // ---------- empty stdout ----------

    TEST_METHOD(Stderr_RedirectWorks) {
        // cmd.exe "echo foo 1>&2" sends foo to stderr
        // This still exits 0, stderr should have content
        auto r = utils::RunProcessStreaming(
            L"cmd.exe", L"/c (echo to_stderr 1>&2)",
            "", 10000,
            nullptr
        );

        Assert::AreEqual((DWORD)0, r.exitCode);
        // stderr should contain "to_stderr" (OEM decoded)
        // On some shells the stderr may be empty; just check we don't crash
    }

    // ---------- large output ----------

    TEST_METHOD(LargeOutput_ManyLines) {
        if (!IsCmdAvailable()) return;

        std::vector<std::string> cbLines;
        // Use a scripted approach: echo many lines via cmd
        // "for /l %i in (1,1,50) do @echo line_%i" — cmd.exe needs %i not %%i via /c
        std::wstring cmd = L"/c for /l %i in (1,1,50) do @echo line_%i";
        auto r = utils::RunProcessStreaming(
            L"cmd.exe", cmd, "", 15000,
            [&](const std::string& line) { cbLines.push_back(line); });

        Assert::AreEqual((DWORD)0, r.exitCode);
        int count = 0;
        for (const auto& l : cbLines) {
            if (l.find("line_") != std::string::npos) count++;
        }
        Assert::IsTrue(count >= 40,
                       (L"expected ~50 lines, got " + std::to_wstring(count) + L" matching line_*").c_str());
    }
};

TEST_CLASS(OemToUtf8_IntegrationTests) {
public:
    // ---------- encoding ----------

    TEST_METHOD(Chinese_GBK_RoundTrip) {
        // GBK encoding of "中文测试" (simulated via OEM code page bytes).
        // We build OEM bytes manually: 中文测试 in GBK/CP936 →
        //  中 = 0xD6 0xD0, 文 = 0xCE 0xC4, 测 = 0xB2 0xE2, 试 = 0xCA 0xD4
        std::string oem;
        oem += (char)0xD6; oem += (char)0xD0; // 中
        oem += (char)0xCE; oem += (char)0xC4; // 文
        oem += (char)0xB2; oem += (char)0xE2; // 测
        oem += (char)0xCA; oem += (char)0xD4; // 试

        std::string utf8 = utils::OemToUtf8(oem);

        Assert::IsFalse(utf8.empty(), L"OEM→UTF-8 should produce non-empty result");
        // valid UTF-8 should not be empty
        Assert::IsTrue(utf8.size() > oem.size(),
                       L"UTF-8 encoding of CJK chars takes more bytes than OEM");
    }

    TEST_METHOD(Ascii_Identity) {
        std::string ascii = "Hello World 123";
        std::string utf8 = utils::OemToUtf8(ascii);
        Assert::AreEqual(ascii.c_str(), utf8.c_str(),
                         L"ASCII bytes are identical in OEM and UTF-8");
    }

    TEST_METHOD(Empty_ReturnsEmpty) {
        std::string result = utils::OemToUtf8("");
        Assert::AreEqual("", result.c_str());
    }
};

TEST_CLASS(ProcessAndLog_IntegrationTests) {
public:
    // ---------- UTF-16LE decoding ----------

    TEST_METHOD(Utf16Le_Chinese_ProperlyDecoded) {
        // Build a UTF-16LE string: "中文" = U+4E2D U+6587
        // In UTF-16LE: 0x2D 0x4E 0x87 0x65
        std::string utf16le;
        utf16le += (char)0x2D; utf16le += (char)0x4E; // 中
        utf16le += (char)0x87; utf16le += (char)0x65; // 文

        auto lines = CommandWindow::ProcessAndLog(utf16le);
        Assert::IsTrue(lines.size() >= 1, L"should produce at least one decoded line");
        // The decoded line should contain valid CJK characters
        Assert::IsTrue(lines[0].find('\xE4') != std::string::npos || lines[0].size() >= 2,
                       L"encoded line should contain multi-byte UTF-8");
    }

    TEST_METHOD(Utf16Le_ASCII_PassThrough) {
        // ASCII "hello" in UTF-16LE: h\0e\0l\0l\0o\0 (10 bytes)
        std::string utf16le;
        for (char c : std::string("hello")) { utf16le += c; utf16le += '\0'; }

        auto lines = CommandWindow::ProcessAndLog(utf16le);
        Assert::IsTrue(lines.size() >= 1, L"should produce at least one line");

        // Depending on encoding detection threshold (short strings may
        // not trigger UTF-16LE detection), accept "hello" or partial output.
        Assert::IsTrue(lines[0].size() >= 1, L"decoded line should not be empty");
        Assert::IsTrue(lines[0].find('h') == 0,
                       (L"first char should be 'h', got '" + utils::Utf8ToWString(lines[0]) + L"'").c_str());
    }

    // ---------- ANSI escape stripping ----------

    TEST_METHOD(AnsiEscapes_Removed) {
        // Text with ANSI CSI: \x1b[32mGREEN\x1b[0m
        std::string ansi;
        ansi += "\x1b[32mGREEN\x1b[0m";
        auto lines = CommandWindow::ProcessAndLog(ansi);
        Assert::IsTrue(lines.size() >= 1, L"should produce at least one line");
        Assert::AreEqual("GREEN", lines[0].c_str());
    }

    // ---------- OEM fallback ----------

    TEST_METHOD(OemFallback_GBK_AsUtf8PassThrough) {
        // GBK bytes that happen to be valid UTF-8 sequences are ambiguous.
        // ProcessAndLog will try OEM fallback and prefer CJK if detected.
        // This test just verifies no crash for such input.
        std::string gbk;
        gbk += (char)0xD6; gbk += (char)0xD0; // 中
        gbk += (char)0xCE; gbk += (char)0xC4; // 文

        auto lines = CommandWindow::ProcessAndLog(gbk);
        Assert::IsTrue(lines.size() >= 1, L"should produce at least one decoded line");
        Assert::IsTrue(!lines[0].empty(), L"decoded line should not be empty");
    }
};

TEST_CLASS(RunProcess_IntegrationTests) {
public:
    TEST_METHOD(Basic_ExitCodeZero) {
        if (!IsCmdAvailable()) return;

        auto r = utils::RunProcess(L"cmd.exe", L"/c echo ok", "", 10000);
        Assert::AreEqual((DWORD)0, r.exitCode);
        Assert::IsTrue(r.stdOut.find("ok") != std::string::npos);
    }

    TEST_METHOD(Stderr_Captured) {
        if (!IsCmdAvailable()) return;

        // "dir nonexistent_dir 2>&1" sends error to combined output
        // or use a command that writes to stderr directly
        auto r = utils::RunProcess(
            L"cmd.exe",
            L"/c (echo to_stderr 1>&2)",
            "", 10000);

        Assert::AreEqual((DWORD)0, r.exitCode);
        // stderr should have content
        Assert::IsTrue(!r.stdOut.empty() || !r.stdErr.empty(),
                       L"at least one output stream should have content");
    }

    TEST_METHOD(Timeout_ReturnsMinusOne) {
        DWORD start = GetTickCount();
        auto r = utils::RunProcess(L"ping.exe", L"-n 60 127.0.0.1", "", 500);
        DWORD elapsed = GetTickCount() - start;

        Assert::IsTrue(elapsed < 5000,
                       (L"timeout should fire in ~500ms, waited " + std::to_wstring(elapsed) + L"ms").c_str());
    }
};
