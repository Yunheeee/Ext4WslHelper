#include "CppUnitTest.h"
#include "../src/core/UsbipdManager.h"
#include "MockCommandExecutor.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

TEST_CLASS(UsbipdManagerListTests) {
public:
    TEST_METHOD(List_ParsesConnectedDevices) {
        MockCommandExecutor mock;
        mock.AddExpectation("usbipd list", { 0,
            "Connected:\r\n"
            "BUSID  VID:PID    DEVICE                         STATE\r\n"
            "5-2    1234:5678  USB Mass Storage Device        Not attached\r\n"
            "5-3    8765:4321  USB Input Device               Not attached\r\n"
            "\r\n"
            "Persisted:\r\n"
            "BUSID  VID:PID    DEVICE                         STATE\r\n",
            "" });

        UsbipdManager mgr(&mock);
        auto devices = mgr.List();

        Assert::AreEqual((size_t)2, devices.size());
        Assert::AreEqual("5-2", devices[0].busId.c_str());
        Assert::AreEqual("1234:5678", devices[0].vidPid.c_str());
        Assert::IsTrue(devices[0].description.find("USB Mass Storage") != std::string::npos);
        Assert::AreEqual("Not attached", devices[0].state.c_str());
        Assert::IsFalse(devices[0].IsAttached());
    }

    TEST_METHOD(List_ParsesAttachedDevices) {
        MockCommandExecutor mock;
        mock.AddExpectation("usbipd list", { 0,
            "Connected:\r\n"
            "BUSID  VID:PID    DEVICE                         STATE\r\n"
            "5-2    1234:5678  USB Mass Storage Device        Attached - Ubuntu\r\n",
            "" });

        UsbipdManager mgr(&mock);
        auto devices = mgr.List();

        Assert::AreEqual((size_t)1, devices.size());
        Assert::AreEqual("5-2", devices[0].busId.c_str());
        Assert::IsTrue(devices[0].IsAttached());
        Assert::AreEqual("Attached - Ubuntu", devices[0].state.c_str());
        Assert::AreEqual("Ubuntu", devices[0].wslDistro.c_str());
    }

    TEST_METHOD(List_NoDevices) {
        MockCommandExecutor mock;
        mock.AddExpectation("usbipd list", { 0,
            "Connected:\r\n"
            "BUSID  VID:PID    DEVICE                         STATE\r\n"
            "\r\n"
            "Persisted:\r\n"
            "BUSID  VID:PID    DEVICE                         STATE\r\n",
            "" });

        UsbipdManager mgr(&mock);
        auto devices = mgr.List();
        Assert::AreEqual((size_t)0, devices.size());
    }

    TEST_METHOD(List_CommandFails) {
        MockCommandExecutor mock;
        mock.AddExpectation("usbipd list", { 1, "", "usbipd not found" });

        UsbipdManager mgr(&mock);
        auto devices = mgr.List();
        Assert::AreEqual((size_t)0, devices.size());
    }

    TEST_METHOD(List_FindsDeviceByBusId) {
        MockCommandExecutor mock;
        mock.AddExpectation("usbipd list", { 0,
            "Connected:\r\n"
            "BUSID  VID:PID    DEVICE                         STATE\r\n"
            "5-2    1234:5678  My SD Card Reader              Not attached\r\n",
            "" });

        UsbipdManager mgr(&mock);
        auto devices = mgr.List();

        Assert::AreEqual((size_t)1, devices.size());
        Assert::AreEqual("My SD Card Reader", devices[0].description.c_str());
    }
};

TEST_CLASS(UsbipdManagerAttachDetachTests) {
public:
    TEST_METHOD(Attach_Success) {
        MockCommandExecutor mock;
        mock.AddExpectation("usbipd wsl attach --busid 5-2", { 0, "success", "" });

        UsbipdManager mgr(&mock);
        Assert::IsTrue(mgr.Attach("5-2"));
    }

    TEST_METHOD(Attach_WithDistro) {
        MockCommandExecutor mock;
        mock.AddExactExpectation("usbipd wsl attach --busid 5-2 --distribution Ubuntu",
                                 { 0, "success", "" });

        UsbipdManager mgr(&mock);
        Assert::IsTrue(mgr.Attach("5-2", "Ubuntu"));
    }

    TEST_METHOD(Attach_Failure) {
        MockCommandExecutor mock;
        mock.AddExpectation("usbipd wsl attach", { 1, "", "already attached" });

        UsbipdManager mgr(&mock);
        Assert::IsFalse(mgr.Attach("5-2"));
    }

    TEST_METHOD(Detach_Success) {
        MockCommandExecutor mock;
        mock.AddExpectation("usbipd wsl detach --busid 5-2", { 0, "detached", "" });

        UsbipdManager mgr(&mock);
        Assert::IsTrue(mgr.Detach("5-2"));
    }

    TEST_METHOD(Detach_AlreadyDetachedIdempotent) {
        MockCommandExecutor mock;
        mock.AddExpectation("usbipd wsl detach --busid 5-2",
                            { 1, "not attached anymore", "" });

        UsbipdManager mgr(&mock);
        // Should return true even though exit code is 1, because "not attached" means already clean
        Assert::IsTrue(mgr.Detach("5-2"));
    }

    TEST_METHOD(Detach_Failure) {
        MockCommandExecutor mock;
        mock.AddExpectation("usbipd wsl detach --busid 5-2",
                            { 1, "", "access denied" });

        UsbipdManager mgr(&mock);
        Assert::IsFalse(mgr.Detach("5-2"));
    }

    TEST_METHOD(Bind_Success) {
        MockCommandExecutor mock;
        mock.AddExpectation("usbipd bind --busid 5-2", { 0, "", "" });

        UsbipdManager mgr(&mock);
        Assert::IsTrue(mgr.Bind("5-2"));
    }

    TEST_METHOD(Unbind_Success) {
        MockCommandExecutor mock;
        mock.AddExpectation("usbipd unbind --busid 5-2", { 0, "", "" });

        UsbipdManager mgr(&mock);
        Assert::IsTrue(mgr.Unbind("5-2"));
    }
};

TEST_CLASS(UsbipdManagerDeviceInfoParsingTests) {
public:
    TEST_METHOD(IsAttached_NotAttached_ReturnsFalse) {
        MockCommandExecutor mock;
        mock.AddExpectation("usbipd list", { 0,
            "Connected:\r\n"
            "BUSID  VID:PID    DEVICE                         STATE\r\n"
            "5-2    1234:5678  Some Device                    Not attached\r\n",
            "" });

        UsbipdManager mgr(&mock);
        auto devices = mgr.List();
        Assert::IsFalse(devices[0].IsAttached());
    }

    TEST_METHOD(IsAttached_Attached_ReturnsTrue) {
        MockCommandExecutor mock;
        mock.AddExpectation("usbipd list", { 0,
            "Connected:\r\n"
            "BUSID  VID:PID    DEVICE                         STATE\r\n"
            "5-2    1234:5678  Some Device                    Attached - Ubuntu-24.04\r\n",
            "" });

        UsbipdManager mgr(&mock);
        auto devices = mgr.List();
        Assert::IsTrue(devices[0].IsAttached());
        Assert::AreEqual("Ubuntu-24.04", devices[0].wslDistro.c_str());
    }
};
