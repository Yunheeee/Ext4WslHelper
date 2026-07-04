#include "CppUnitTest.h"
#include "../src/command/ICommandExecutor.h"
#include "../src/command/PowershellExecutor.h"
#include "../src/command/WslExecutor.h"
#include "../src/common/Logger.h"
#include "MockCommandExecutor.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

TEST_CLASS(PowershellExecutorTests) {
public:
    TEST_METHOD(Execute_SimpleCommand) {
        PowershellExecutor exec(false);
        auto result = exec.Execute("echo hello", "", 10000);
        Assert::AreEqual((DWORD)0, result.exitCode);
        // stdout should contain "hello"
        Assert::IsTrue(result.stdOut.find("hello") != std::string::npos,
                       L"stdout should contain 'hello'");
    }

    TEST_METHOD(Execute_ExitCodeZero) {
        PowershellExecutor exec(false);
        auto result = exec.Execute("exit 0", "", 5000);
        Assert::AreEqual((DWORD)0, result.exitCode);
    }

    TEST_METHOD(Execute_ExitCodeNonZero) {
        PowershellExecutor exec(false);
        auto result = exec.Execute("exit 42", "", 5000);
        Assert::AreEqual((DWORD)42, result.exitCode);
    }

    TEST_METHOD(Execute_Timeout) {
        PowershellExecutor exec(false);
        // Start a command that sleeps longer than our timeout
        auto result = exec.Execute("Start-Sleep -Seconds 10", "", 1000);
        // Should time out
        Assert::AreEqual((DWORD)-1, result.exitCode);
        Assert::IsFalse(result.stdErr.empty());
    }

    TEST_METHOD(Execute_StdoutCapture) {
        PowershellExecutor exec(false);
        auto result = exec.Execute("Write-Output 'test output'", "", 5000);
        Assert::AreEqual((DWORD)0, result.exitCode);
        Assert::IsTrue(result.stdOut.find("test output") != std::string::npos,
                       L"stdout should contain command output");
    }

    TEST_METHOD(Execute_StderrCapture) {
        PowershellExecutor exec(false);
        auto result = exec.Execute("[Console]::Error.WriteLine('test error')", "", 5000);
        // [Console]::Error.WriteLine writes to process stderr without setting exit code
        Assert::AreEqual((DWORD)0, result.exitCode);
        Assert::IsTrue(result.stdErr.find("test error") != std::string::npos);
    }

    TEST_METHOD(Execute_StdinPipe) {
        PowershellExecutor exec(false);
        // Use stdin input echoed back
        auto result = exec.Execute("$input | ForEach-Object { $_ }", "hello from stdin", 5000);
        Assert::AreEqual((DWORD)0, result.exitCode);
        Assert::IsTrue(result.stdOut.find("hello from stdin") != std::string::npos,
                       L"stdout should contain piped stdin content");
    }

    TEST_METHOD(IsAvailable_ShouldReturnTrue) {
        PowershellExecutor exec(false);
        Assert::IsTrue(exec.IsAvailable());
    }

    TEST_METHOD(StartBackground_ThenWait) {
        PowershellExecutor exec(false);
        HANDLE hProcess = exec.StartBackground("echo background_test");
        Assert::AreNotEqual(INVALID_HANDLE_VALUE, hProcess);

        // Wait for completion
        WaitForSingleObject(hProcess, 5000);

        DWORD exitCode = 0;
        GetExitCodeProcess(hProcess, &exitCode);
        Assert::AreEqual((DWORD)0, exitCode);
        CloseHandle(hProcess);
    }
};

TEST_CLASS(WslExecutorTests) {
public:
    // Helper: check if WSL distro is unavailable (either CreateProcess failed or distro not found)
    static bool IsWslUnavailable(const CommandResult& result) {
        if (result.exitCode == (DWORD)-1 && result.stdErr.find("CreateProcess") != std::string::npos)
            return true;
        // wsl.exe -d <distro> exits with 1 when the distro doesn't exist
        if (result.exitCode == 1)
            return true;
        return false;
    }

    TEST_METHOD(Execute_SimpleCommand) {
        WslExecutor exec("Ubuntu", false);
        auto result = exec.Execute("echo hello_wsl", "", 15000);
        if (IsWslUnavailable(result)) {
            ::Logger::Instance().Warning("[SKIP] WSL Ubuntu not available");
            return;
        }
        Assert::AreEqual((DWORD)0, result.exitCode);
        Assert::IsTrue(result.stdOut.find("hello_wsl") != std::string::npos,
                       L"stdout should contain 'hello_wsl'");
    }

    TEST_METHOD(Execute_WithStdinInput) {
        WslExecutor exec("Ubuntu", false);
        // Test stdin pipe: cat reads from stdin
        auto result = exec.Execute("cat", "test_stdin_input", 15000);
        if (IsWslUnavailable(result)) {
            ::Logger::Instance().Warning("[SKIP] WSL Ubuntu not available");
            return;
        }
        Assert::AreEqual((DWORD)0, result.exitCode);
        Assert::IsTrue(result.stdOut.find("test_stdin_input") != std::string::npos,
                       L"stdout should contain piped stdin content");
    }

    TEST_METHOD(IsAvailable_NoDistro_ReturnsFalse) {
        WslExecutor exec("NonExistentDistro12345", false);
        Assert::IsFalse(exec.IsAvailable());
    }

    TEST_METHOD(Execute_CustomExitCode) {
        WslExecutor exec("Ubuntu", false);
        auto result = exec.Execute("exit 42", "", 10000);
        if (IsWslUnavailable(result)) {
            ::Logger::Instance().Warning("[SKIP] WSL Ubuntu not available");
            return;
        }
        Assert::AreEqual((DWORD)42, result.exitCode);
    }

    TEST_METHOD(Execute_Timeout) {
        WslExecutor exec("Ubuntu", false);
        auto result = exec.Execute("sleep 10", "", 1000);
        if (IsWslUnavailable(result)) {
            ::Logger::Instance().Warning("[SKIP] WSL Ubuntu not available");
            return;
        }
        Assert::AreEqual((DWORD)-1, result.exitCode);
    }
};

TEST_CLASS(MockCommandExecutorTests) {
public:
    TEST_METHOD(Mock_ReturnsExpectedResult) {
        MockCommandExecutor mock;
        mock.AddExpectation("echo", { 0, "hello", "" });

        auto result = mock.Execute("echo hello");
        Assert::AreEqual((DWORD)0, result.exitCode);
        Assert::AreEqual("hello", result.stdOut.c_str());
    }

    TEST_METHOD(Mock_UnrecognizedCommand) {
        MockCommandExecutor mock;

        auto result = mock.Execute("unknown_cmd");
        Assert::AreEqual((DWORD)-1, result.exitCode);
    }

    TEST_METHOD(Mock_TracksExecutedCommands) {
        MockCommandExecutor mock;
        mock.AddExpectation("test", { 0, "", "" });

        mock.Execute("test_cmd_1");
        mock.Execute("test_cmd_2");

        Assert::AreEqual((size_t)2, mock.GetExecutedCommands().size());
        Assert::AreEqual("test_cmd_1", mock.GetExecutedCommands()[0].commandLine.c_str());
        Assert::AreEqual("test_cmd_2", mock.GetExecutedCommands()[1].commandLine.c_str());
    }

    TEST_METHOD(Mock_StdinInputIsRecorded) {
        MockCommandExecutor mock;

        mock.Execute("sudo mount", "password123\n");
        Assert::AreEqual((size_t)1, mock.GetExecutedCommands().size());
        Assert::AreEqual("password123\n", mock.GetExecutedCommands()[0].stdinInput.c_str());
    }

    TEST_METHOD(Mock_ExactMatchTakesPriority) {
        MockCommandExecutor mock;
        mock.AddExpectation("mount", { 0, "mounted", "" });
        mock.AddExactExpectation("mount fail", { 1, "", "failed" });

        auto result = mock.Execute("mount something");
        Assert::AreEqual((DWORD)0, result.exitCode);

        result = mock.Execute("mount fail");
        Assert::AreEqual((DWORD)1, result.exitCode);
    }
};
