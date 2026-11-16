#include "CppUnitTest.h"
#include "../src/services/EnvironmentService.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

TEST_CLASS(EnvironmentServiceKernelVersionTests) {
public:
    TEST_METHOD(Kernel_OldBundledVersion_NotSupported) {
        // 旧版 wsl_update_x64.msi 内置内核，低于 usbipd 要求
        Assert::IsFalse(EnvironmentService::IsKernelVersionSupported("5.10.16"));
    }

    TEST_METHOD(Kernel_ExactMinimum_Supported) {
        Assert::IsTrue(EnvironmentService::IsKernelVersionSupported("5.10.60.1"));
    }

    TEST_METHOD(Kernel_BelowMinimum_NotSupported) {
        Assert::IsFalse(EnvironmentService::IsKernelVersionSupported("5.10.60.0"));
    }

    TEST_METHOD(Kernel_NewVersion_Supported) {
        Assert::IsTrue(EnvironmentService::IsKernelVersionSupported("6.6.87.2-1"));
    }

    TEST_METHOD(Kernel_MicrosoftSuffix_Supported) {
        Assert::IsTrue(EnvironmentService::IsKernelVersionSupported(
            "5.15.133.1-microsoft-standard-WSL2"));
    }

    TEST_METHOD(Kernel_Empty_NotSupported) {
        Assert::IsFalse(EnvironmentService::IsKernelVersionSupported(""));
    }
};

TEST_CLASS(EnvironmentServiceWslVersionTests) {
public:
    TEST_METHOD(Wsl1_NotSupported) {
        Assert::IsFalse(EnvironmentService::IsWslVersionSupported("1.0"));
    }

    TEST_METHOD(Wsl2_Supported) {
        Assert::IsTrue(EnvironmentService::IsWslVersionSupported("2.0.9.0"));
    }

    TEST_METHOD(Wsl3_Supported) {
        Assert::IsTrue(EnvironmentService::IsWslVersionSupported("3.0.5"));
    }

    TEST_METHOD(Empty_NotSupported) {
        Assert::IsFalse(EnvironmentService::IsWslVersionSupported(""));
    }
};

TEST_CLASS(EnvironmentServiceDismStateTests) {
public:
    TEST_METHOD(ChineseEnabled) {
        Assert::IsTrue(EnvironmentService::ParseDismFeatureState(
            "\xe7\x8a\xb6\xe6\x80\x81 : \xe5\xb7\xb2\xe5\x90\xaf\xe7\x94\xa8") ==
            DismFeatureState::Enabled);
    }

    TEST_METHOD(ChineseEnablePending) {
        Assert::IsTrue(EnvironmentService::ParseDismFeatureState(
            "\xe7\x8a\xb6\xe6\x80\x81 : \xe5\x90\xaf\xe7\x94\xa8\xe6\x8c\x82\xe8\xb5\xb7") ==
            DismFeatureState::EnablePending);
    }

    TEST_METHOD(EnglishEnabled) {
        Assert::IsTrue(EnvironmentService::ParseDismFeatureState(
            "State : Enabled") == DismFeatureState::Enabled);
    }

    TEST_METHOD(EnglishEnablePending) {
        Assert::IsTrue(EnvironmentService::ParseDismFeatureState(
            "State : Enable Pending") == DismFeatureState::EnablePending);
    }

    TEST_METHOD(EnglishDisabled) {
        Assert::IsTrue(EnvironmentService::ParseDismFeatureState(
            "State : Disabled") == DismFeatureState::Disabled);
    }

    TEST_METHOD(RestartPossibleButEnabled_IsNotPending) {
        // "需要重新启动 : Possible" 只是说明可能需要重启，状态仍是已启用
        std::string output =
            "\xe9\x9c\x80\xe8\xa6\x81\xe9\x87\x8d\xe6\x96\xb0\xe5\x90\xaf\xe5\x8a\xa8 : Possible\r\n"
            "\xe7\x8a\xb6\xe6\x80\x81 : \xe5\xb7\xb2\xe5\x90\xaf\xe7\x94\xa8\r\n";
        Assert::IsTrue(EnvironmentService::ParseDismFeatureState(output) ==
                       DismFeatureState::Enabled);
    }

    TEST_METHOD(EmptyOrUnknown) {
        Assert::IsTrue(EnvironmentService::ParseDismFeatureState("") ==
                       DismFeatureState::Unknown);
    }
};

TEST_CLASS(EnvironmentServiceHelpOutputTests) {
public:
    TEST_METHOD(OldStubHelpPage_Detected) {
        std::string output =
            "Usage: wsl.exe [Argument] [Options...]\r\n"
            "    --install <Distro>\r\n"
            "    --list, -l [Options]\r\n";
        Assert::IsTrue(EnvironmentService::LooksLikeWslHelpOutput(output));
    }

    TEST_METHOD(ChineseHelpPage_Detected) {
        std::string output =
            "\xe7\x94\xa8\xe6\xb3\x95: wsl.exe [Argument]\r\n"
            "    --install <Distro>\r\n";
        Assert::IsTrue(EnvironmentService::LooksLikeWslHelpOutput(output));
    }

    TEST_METHOD(VersionOutput_NotHelp) {
        std::string output =
            "WSL version: 2.0.9.0\r\n"
            "Kernel version: 5.15.133.1-1\r\n"
            "WSLg version: 1.0.47\r\n";
        Assert::IsFalse(EnvironmentService::LooksLikeWslHelpOutput(output));
    }

    TEST_METHOD(EmptyOutput_NotHelp) {
        Assert::IsFalse(EnvironmentService::LooksLikeWslHelpOutput(""));
    }
};
