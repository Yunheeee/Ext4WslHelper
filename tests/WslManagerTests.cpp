#include "CppUnitTest.h"
#include "../src/core/WslManager.h"
#include "MockCommandExecutor.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

TEST_CLASS(WslManagerListDistributionsTests) {
public:
    TEST_METHOD(ListDistributions_ParsesOutput) {
        MockCommandExecutor mock;
        mock.AddExpectation("wsl --list --verbose", { 0,
            "  NAME            STATE           VERSION\r\n"
            "* Ubuntu          Running         2\r\n"
            "  Debian          Stopped         1\r\n",
            "" });

        WslManager mgr(&mock);
        auto distros = mgr.ListDistributions();

        Assert::AreEqual((size_t)2, distros.size());
        Assert::AreEqual("Ubuntu", distros[0].name.c_str());
        Assert::IsTrue(distros[0].isDefault);
        Assert::IsTrue(distros[0].isRunning);
        Assert::AreEqual("2", distros[0].version.c_str());
        Assert::IsTrue(distros[0].IsWsl2());

        Assert::AreEqual("Debian", distros[1].name.c_str());
        Assert::IsFalse(distros[1].isDefault);
        Assert::IsFalse(distros[1].isRunning);
        Assert::AreEqual("1", distros[1].version.c_str());
        Assert::IsFalse(distros[1].IsWsl2());
    }

    TEST_METHOD(ListDistributions_NoDistributions) {
        MockCommandExecutor mock;
        mock.AddExpectation("wsl --list --verbose", { 0,
            "  NAME            STATE           VERSION\r\n",
            "" });

        WslManager mgr(&mock);
        auto distros = mgr.ListDistributions();
        Assert::AreEqual((size_t)0, distros.size());
    }

    TEST_METHOD(ListDistributions_WslNotInstalled) {
        MockCommandExecutor mock;
        mock.AddExpectation("wsl --list --verbose",
                            { (DWORD)-1, "", "wsl.exe not found" });

        WslManager mgr(&mock);
        auto distros = mgr.ListDistributions();
        Assert::AreEqual((size_t)0, distros.size());
    }

    TEST_METHOD(GetDefaultDistribution_ReturnsDefault) {
        MockCommandExecutor mock;
        mock.AddExpectation("wsl --list --verbose", { 0,
            "  NAME            STATE           VERSION\r\n"
            "* Ubuntu          Running         2\r\n"
            "  Debian          Stopped         1\r\n",
            "" });

        WslManager mgr(&mock);
        auto defaultDistro = mgr.GetDefaultDistribution();
        Assert::AreEqual("Ubuntu", defaultDistro.name.c_str());
        Assert::IsTrue(defaultDistro.isDefault);
    }

    TEST_METHOD(GetDefaultDistribution_NoDefault_ReturnsFirst) {
        MockCommandExecutor mock;
        mock.AddExpectation("wsl --list --verbose", { 0,
            "  NAME            STATE           VERSION\r\n"
            "  Ubuntu          Running         2\r\n",
            "" });

        WslManager mgr(&mock);
        auto defaultDistro = mgr.GetDefaultDistribution();
        Assert::AreEqual("Ubuntu", defaultDistro.name.c_str());
    }

    TEST_METHOD(GetDefaultDistribution_Empty_ReturnsEmpty) {
        MockCommandExecutor mock;
        mock.AddExpectation("wsl --list --verbose", { 0,
            "  NAME            STATE           VERSION\r\n",
            "" });

        WslManager mgr(&mock);
        auto distro = mgr.GetDefaultDistribution();
        Assert::AreEqual("", distro.name.c_str());
    }
};

TEST_CLASS(WslManagerExt4SupportTests) {
public:
    TEST_METHOD(IsExt4Supported_Wsl2_ReturnsTrue) {
        MockCommandExecutor mock;
        mock.AddExpectation("wsl --list --verbose", { 0,
            "  NAME            STATE           VERSION\r\n"
            "* Ubuntu          Running         2\r\n",
            "" });

        WslManager mgr(&mock);
        Assert::IsTrue(mgr.IsExt4Supported("Ubuntu"));
    }

    TEST_METHOD(IsExt4Supported_Wsl1_ReturnsFalse) {
        MockCommandExecutor mock;
        mock.AddExpectation("wsl --list --verbose", { 0,
            "  NAME            STATE           VERSION\r\n"
            "  Debian          Stopped         1\r\n",
            "" });

        WslManager mgr(&mock);
        Assert::IsFalse(mgr.IsExt4Supported("Debian"));
    }

    TEST_METHOD(IsExt4Supported_UnknownDistro_ReturnsFalse) {
        MockCommandExecutor mock;
        mock.AddExpectation("wsl --list --verbose", { 0,
            "  NAME            STATE           VERSION\r\n"
            "* Ubuntu          Running         2\r\n",
            "" });

        WslManager mgr(&mock);
        Assert::IsFalse(mgr.IsExt4Supported("NonExistent"));
    }
};

TEST_CLASS(WslManagerBlockDeviceTests) {
public:
    TEST_METHOD(DetectBlockDevice_FindsNewDevice) {
        MockCommandExecutor mock;
        mock.AddExpectation("lsblk -o NAME,FSTYPE,SIZE -d -n", { 0,
            "sda\r\n"
            "sdb\r\n"
            "nvme0n1\r\n",
            "" });

        WslManager mgr(&mock);
        std::string previousDisks = "sda\r\nnvme0n1\r\n";
        std::string newDevice = mgr.DetectBlockDevice(previousDisks);

        Assert::AreEqual("sdb", newDevice.c_str());
    }

    TEST_METHOD(DetectBlockDevice_NoNewDevice) {
        MockCommandExecutor mock;
        mock.AddExpectation("lsblk -o NAME,FSTYPE,SIZE -d -n", { 0,
            "sda\r\nnvme0n1\r\n",
            "" });

        WslManager mgr(&mock);
        std::string previousDisks = "sda\r\nnvme0n1\r\n";
        std::string newDevice = mgr.DetectBlockDevice(previousDisks);

        Assert::AreEqual("", newDevice.c_str());
    }

    TEST_METHOD(DetectBlockDevice_SkipsLoopDevices) {
        MockCommandExecutor mock;
        mock.AddExpectation("lsblk -o NAME,FSTYPE,SIZE -d -n", { 0,
            "sda\r\n"
            "loop0\r\n"
            "loop1\r\n"
            "sdb\r\n",
            "" });

        WslManager mgr(&mock);
        std::string previousDisks = "sda\r\n";
        std::string newDevice = mgr.DetectBlockDevice(previousDisks);

        Assert::AreEqual("sdb", newDevice.c_str());
    }

    TEST_METHOD(DetectBlockDevice_LsblkFails_ReturnsEmpty) {
        MockCommandExecutor mock;
        mock.AddExpectation("lsblk -o NAME,FSTYPE,SIZE -d -n", { 1, "", "lsblk not found" });

        WslManager mgr(&mock);
        std::string newDevice = mgr.DetectBlockDevice("");
        Assert::AreEqual("", newDevice.c_str());
    }
};

TEST_CLASS(WslManagerMountTests) {
public:
    TEST_METHOD(MountExt4_Success) {
        MockCommandExecutor mock;
        mock.AddExpectation("sudo -S echo ok", { 0, "", "" });
        mock.AddExpectation("sudo -S mkdir -p", { 0, "", "" });
        mock.AddExpectation("sudo -S mount -t ext4", { 0, "", "" });

        WslManager mgr(&mock);
        bool authError = false;
        std::string err = mgr.MountExt4("sda1", "/mnt/ext4-sd", "password\n", authError);
        Assert::IsTrue(err.empty());
        Assert::IsFalse(authError);
    }

    TEST_METHOD(MountExt4_MkdirFails) {
        MockCommandExecutor mock;
        mock.AddExpectation("sudo -S echo ok", { 0, "", "" });
        mock.AddExpectation("sudo -S mkdir -p", { 1, "", "permission denied" });

        WslManager mgr(&mock);
        bool authError = false;
        std::string err = mgr.MountExt4("sda1", "/mnt/ext4-sd", "password\n", authError);
        Assert::IsFalse(err.empty());
        Assert::IsFalse(authError);
    }

    TEST_METHOD(MountExt4_MountFailsThenReadOnlySucceeds) {
        MockCommandExecutor mock;
        mock.AddExpectation("sudo -S echo ok", { 0, "", "" });
        mock.AddExpectation("sudo -S mkdir -p", { 0, "", "" });
        mock.AddExpectation("sudo -S mount -t ext4 /dev/sda1", { 32, "", "mount: wrong fs type" });
        mock.AddExpectation("sudo -S mount -t ext4 -o ro", { 0, "", "" });

        WslManager mgr(&mock);
        bool authError = false;
        std::string err = mgr.MountExt4("sda1", "/mnt/ext4-sd", "password\n", authError);
        Assert::IsTrue(err.empty());
        Assert::IsFalse(authError);
    }

    TEST_METHOD(MountExt4_BothMountAttemptsFail) {
        MockCommandExecutor mock;
        mock.AddExpectation("sudo -S echo ok", { 0, "", "" });
        mock.AddExpectation("sudo -S mkdir -p", { 0, "", "" });
        mock.AddExpectation("sudo -S mount -t ext4 /dev/sda1", { 32, "", "mount: wrong fs type" });
        mock.AddExpectation("sudo -S mount -t ext4 -o ro", { 32, "", "mount: wrong fs type" });

        WslManager mgr(&mock);
        bool authError = false;
        std::string err = mgr.MountExt4("sda1", "/mnt/ext4-sd", "password\n", authError);
        Assert::IsFalse(err.empty());
        Assert::IsFalse(authError);
    }

    TEST_METHOD(Unmount_Success) {
        MockCommandExecutor mock;
        mock.AddExpectation("sudo -S umount /mnt/ext4-sd", { 0, "", "" });

        WslManager mgr(&mock);
        Assert::IsTrue(mgr.Unmount("/mnt/ext4-sd", "password\n"));
    }

    TEST_METHOD(Unmount_NotMounted_Idempotent) {
        MockCommandExecutor mock;
        mock.AddExpectation("sudo -S umount /mnt/ext4-sd",
                            { 1, "not mounted", "" });

        WslManager mgr(&mock);
        Assert::IsTrue(mgr.Unmount("/mnt/ext4-sd", "password\n"));
    }

    TEST_METHOD(Unmount_FailsThenLazySucceeds) {
        MockCommandExecutor mock;
        mock.AddExpectation("sudo -S umount /mnt/ext4-sd", { 1, "target is busy", "" });
        mock.AddExpectation("sudo -S umount -l /mnt/ext4-sd", { 0, "", "" });

        WslManager mgr(&mock);
        Assert::IsTrue(mgr.Unmount("/mnt/ext4-sd", "password\n"));
    }
};

TEST_CLASS(WslManagerWindowsPathTests) {
public:
    TEST_METHOD(MakeWindowsPath_StandardMountPoint) {
        std::string path = WslManager::MakeWindowsPath("Ubuntu", "/mnt/ext4-sd-card");
        Assert::AreEqual("\\\\wsl.localhost\\Ubuntu\\mnt\\ext4-sd-card", path.c_str());
    }

    TEST_METHOD(MakeWindowsPath_EmptyMountPoint) {
        std::string path = WslManager::MakeWindowsPath("Ubuntu", "/");
        Assert::AreEqual("\\\\wsl.localhost\\Ubuntu\\", path.c_str());
    }
};
