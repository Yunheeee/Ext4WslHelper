#include "CppUnitTest.h"
#include "../src/core/MountManager.h"
#include "MockCommandExecutor.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// Helper to create a managed test environment
struct MountTestFixture {
    MockCommandExecutor mock;
    UsbipdManager usbipd;
    WslManager wsl;
    MountManager mountMgr;

    MountTestFixture()
        : usbipd(&mock), wsl(&mock), mountMgr(&usbipd, &wsl) {
    }

    void SetupSuccessfulMountFlow() {
        // GetDiskList before attach
        mock.AddExpectation("lsblk -o NAME -d -n", { 0, "sda\nnvme0n1\n", "" });
        // usbipd attach
        mock.AddExpectation("usbipd wsl attach --busid 5-2", { 0, "", "" });
        // DetectBlockDevice (after attach)
        mock.AddExpectation("lsblk -o NAME,FSTYPE,SIZE -d -n", { 0, "sda\nsdb\nnvme0n1\n", "" });
        // mkdir for mount point
        mock.AddExpectation("sudo -S mkdir -p /mnt/", { 0, "", "" });
        // mount EXT4 (partition 1)
        mock.AddExpectation("sudo -S mount -t ext4 /dev/sdb1", { 0, "", "" });
    }

    void SetupUnmountFlow() {
        // Unmount
        mock.AddExpectation("sudo -S umount /mnt/ext4-sd-", { 0, "", "" });
        // usbipd detach
        mock.AddExpectation("usbipd wsl detach --busid 5-2", { 0, "", "" });
    }
};

TEST_CLASS(MountManagerAttachAndMountTests) {
public:
    TEST_METHOD(AttachAndMount_FullSuccess) {
        MountTestFixture f;
        f.SetupSuccessfulMountFlow();

        MountSession session = f.mountMgr.AttachAndMount("5-2", "Ubuntu", "password\n");

        Assert::IsTrue(session.isMounted);
        Assert::AreEqual("5-2", session.busId.c_str());
        Assert::AreEqual("Ubuntu", session.distro.c_str());
        Assert::AreEqual((int)MountSession::Status::Mounted, (int)session.status);
        Assert::IsFalse(session.mountPoint.empty());
        Assert::IsFalse(session.windowsPath.empty());

        // Check Windows path format
        Assert::IsTrue(session.windowsPath.find("wsl.localhost") != std::string::npos);
        Assert::IsTrue(session.windowsPath.find("Ubuntu") != std::string::npos);
    }

    TEST_METHOD(AttachAndMount_UsbipdAttachFails) {
        MountTestFixture f;
        f.mock.AddExpectation("lsblk -o NAME -d -n", { 0, "sda\n", "" });
        f.mock.AddExpectation("usbipd wsl attach --busid 5-2", { 1, "", "device not found" });

        MountSession session = f.mountMgr.AttachAndMount("5-2", "Ubuntu", "password\n");

        Assert::IsFalse(session.isMounted);
        Assert::AreEqual((int)MountSession::Status::Error, (int)session.status);
    }

    TEST_METHOD(AttachAndMount_NoBlockDeviceDetected) {
        MountTestFixture f;
        f.mock.AddExpectation("lsblk -o NAME -d -n", { 0, "sda\nnvme0n1\n", "" });
        f.mock.AddExpectation("usbipd wsl attach --busid 5-2", { 0, "", "" });

        // lsblk after attach returns same as before (no new device)
        f.mock.AddExpectation("lsblk -o NAME,FSTYPE,SIZE -d -n", { 0, "sda\nnvme0n1\n", "" });

        // Retries - all return same (no new device for 10 retries)
        for (int i = 0; i < 9; i++) {
            f.mock.AddExpectation("lsblk -o NAME,FSTYPE,SIZE -d -n", { 0, "sda\nnvme0n1\n", "" });
        }

        // Detach rollback
        f.mock.AddExpectation("usbipd wsl detach --busid 5-2", { 0, "", "" });

        MountSession session = f.mountMgr.AttachAndMount("5-2", "Ubuntu", "password\n");

        Assert::IsFalse(session.isMounted);
        Assert::AreEqual((int)MountSession::Status::Error, (int)session.status);
    }

    TEST_METHOD(AttachAndMount_MountFailsRollback) {
        MountTestFixture f;
        f.mock.AddExpectation("lsblk -o NAME -d -n", { 0, "sda\nnvme0n1\n", "" });
        f.mock.AddExpectation("usbipd wsl attach --busid 5-2", { 0, "", "" });
        f.mock.AddExpectation("lsblk -o NAME,FSTYPE,SIZE -d -n", { 0, "sda\nsdb\nnvme0n1\n", "" });
        f.mock.AddExpectation("sudo -S mkdir -p /mnt/", { 0, "", "" });

        // All mount attempts fail
        f.mock.AddExpectation("sudo -S mount -t ext4 /dev/sdb1", { 32, "", "wrong fs type" });
        f.mock.AddExpectation("sudo -S mount -t ext4 /dev/sdbp1", { 32, "", "no such device" });
        f.mock.AddExpectation("sudo -S mount -t ext4 /dev/sdb", { 32, "", "wrong fs type" });

        // Rollback detach
        f.mock.AddExpectation("usbipd wsl detach --busid 5-2", { 0, "", "" });

        MountSession session = f.mountMgr.AttachAndMount("5-2", "Ubuntu", "password\n");

        Assert::IsFalse(session.isMounted);
        Assert::AreEqual((int)MountSession::Status::Error, (int)session.status);
    }
};

TEST_CLASS(MountManagerUnmountTests) {
public:
    TEST_METHOD(UnmountAndDetach_Success) {
        MountTestFixture f;
        f.SetupSuccessfulMountFlow();

        // Mount first
        MountSession session = f.mountMgr.AttachAndMount("5-2", "Ubuntu", "password\n");
        Assert::IsTrue(session.isMounted);

        // Clear mock expectations for mount flow, setup unmount
        f.mock.ClearHistory();
        f.mock.AddExpectation("sudo -S umount", { 0, "", "" });
        f.mock.AddExpectation("usbipd wsl detach --busid 5-2", { 0, "", "" });

        bool result = f.mountMgr.UnmountAndDetach("5-2", "password\n");
        Assert::IsTrue(result);

        // Verify session removed
        auto sessions = f.mountMgr.ListActiveSessions();
        Assert::AreEqual((size_t)0, sessions.size());
    }

    TEST_METHOD(UnmountAndDetach_SessionNotFound_DetachOnly) {
        MountTestFixture f;
        f.mock.AddExpectation("usbipd wsl detach --busid 5-2", { 0, "", "" });

        bool result = f.mountMgr.UnmountAndDetach("5-2", "password\n");
        Assert::IsTrue(result);
    }

    TEST_METHOD(UnmountAndDetach_Fails) {
        MountTestFixture f;
        f.mock.AddExpectation("usbipd wsl detach --busid 5-2", { 1, "", "access denied" });

        bool result = f.mountMgr.UnmountAndDetach("5-2", "password\n");
        Assert::IsFalse(result);
    }
};

TEST_CLASS(MountManagerSessionTests) {
public:
    TEST_METHOD(ListActiveSessions_InitiallyEmpty) {
        MountTestFixture f;
        auto sessions = f.mountMgr.ListActiveSessions();
        Assert::AreEqual((size_t)0, sessions.size());
    }

    TEST_METHOD(ListActiveSessions_AfterMount_ContainsSession) {
        MountTestFixture f;
        f.SetupSuccessfulMountFlow();
        f.mountMgr.AttachAndMount("5-2", "Ubuntu", "password\n");

        auto sessions = f.mountMgr.ListActiveSessions();
        Assert::AreEqual((size_t)1, sessions.size());
        Assert::AreEqual("5-2", sessions[0].busId.c_str());
        Assert::IsTrue(sessions[0].isMounted);
    }

    TEST_METHOD(GetSession_FindsByBusId) {
        MountTestFixture f;
        f.SetupSuccessfulMountFlow();
        f.mountMgr.AttachAndMount("5-2", "Ubuntu", "password\n");

        MountSession session = f.mountMgr.GetSession("5-2");
        Assert::AreEqual("5-2", session.busId.c_str());
        Assert::IsTrue(session.isMounted);
    }

    TEST_METHOD(GetSession_NotFound_ReturnsEmpty) {
        MountTestFixture f;
        MountSession session = f.mountMgr.GetSession("nonexistent");
        Assert::AreEqual("", session.busId.c_str());
        Assert::IsFalse(session.isMounted);
    }

    TEST_METHOD(NotificationCallback_InvokedOnMount) {
        MountTestFixture f;
        f.SetupSuccessfulMountFlow();

        int callbackCount = 0;
        MountSession capturedSession;
        f.mountMgr.SetNotificationCallback([&](const MountSession& s) {
            callbackCount++;
            capturedSession = s;
        });

        f.mountMgr.AttachAndMount("5-2", "Ubuntu", "password\n");

        Assert::AreEqual(1, callbackCount);
        Assert::AreEqual("5-2", capturedSession.busId.c_str());
    }

    TEST_METHOD(MultipleMounts_TracksSeparateSessions) {
        MountTestFixture fHello;

        // First mount
        fHello.SetupSuccessfulMountFlow();
        auto s1 = fHello.mountMgr.AttachAndMount("5-2", "Ubuntu", "password\n");
        Assert::IsTrue(s1.isMounted);

        // Second mount with different bus ID
        // Need separate fixture because mock expectations would conflict
        MountTestFixture f2;
        f2.mock.AddExpectation("lsblk -o NAME -d -n", { 0, "sda\n", "" });
        f2.mock.AddExpectation("usbipd wsl attach --busid 5-3", { 0, "", "" });
        f2.mock.AddExpectation("lsblk -o NAME,FSTYPE,SIZE -d -n", { 0, "sda\nsdc\n", "" });
        f2.mock.AddExpectation("sudo -S mkdir -p /mnt/", { 0, "", "" });
        f2.mock.AddExpectation("sudo -S mount -t ext4 /dev/sdc1", { 0, "", "" });

        MountManager mgr2(&f2.usbipd, &f2.wsl);
        auto s2 = mgr2.AttachAndMount("5-3", "Ubuntu", "password\n");
        Assert::IsTrue(s2.isMounted);
    }
};

TEST_CLASS(MountManagerCleanupTests) {
public:
    TEST_METHOD(CleanupDisconnected_RemovesMissingDevices) {
        MountTestFixture f;
        f.SetupSuccessfulMountFlow();
        f.mountMgr.AttachAndMount("5-2", "Ubuntu", "password\n");

        // Mark the session as mounted
        auto sessions = f.mountMgr.ListActiveSessions();
        Assert::AreEqual((size_t)1, sessions.size());

        // Cleanup: device 5-2 is no longer present
        f.mountMgr.CleanupDisconnected([]() -> std::vector<std::string> {
            return {}; // no devices present
        });

        // Should still be tracked but not active
        auto activeSessions = f.mountMgr.ListActiveSessions();
        Assert::AreEqual((size_t)0, activeSessions.size());

        // Session still accessible via GetSession but marked as error
        MountSession session = f.mountMgr.GetSession("5-2");
        Assert::AreEqual("5-2", session.busId.c_str());
        Assert::IsFalse(session.isMounted);
        Assert::AreEqual((int)MountSession::Status::Error, (int)session.status);
    }

    TEST_METHOD(CleanupDisconnected_KeepsPresentDevices) {
        MountTestFixture f;
        f.SetupSuccessfulMountFlow();
        f.mountMgr.AttachAndMount("5-2", "Ubuntu", "password\n");

        // Cleanup: device still present
        f.mountMgr.CleanupDisconnected([]() -> std::vector<std::string> {
            return { "5-2" };
        });

        auto sessions = f.mountMgr.ListActiveSessions();
        Assert::AreEqual((size_t)1, sessions.size());
        Assert::IsTrue(sessions[0].isMounted);
    }
};
