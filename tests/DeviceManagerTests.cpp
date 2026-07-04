#include "CppUnitTest.h"
#include "../src/core/DeviceManager.h"
#include "MockCommandExecutor.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

TEST_CLASS(DeviceManagerEnumerationTests) {
public:
    TEST_METHOD(EnumerateDevices_ParsesOutput) {
        MockCommandExecutor mock;
        mock.AddExpectation("usbipd list", { 0,
            "Connected:\r\n"
            "BUSID  VID:PID    DEVICE                         STATE\r\n"
            "5-1    1234:5678  USB Mass Storage Device        Not attached\r\n"
            "5-2    abcd:1234  USB Input Device               Attached - Ubuntu\r\n"
            "\r\n"
            "Persisted:\r\n"
            "BUSID  VID:PID    DEVICE                         STATE\r\n",
            "" });

        DeviceManager dm(&mock);
        auto devices = dm.EnumerateDevices();

        Assert::AreEqual((size_t)2, devices.size());

        Assert::AreEqual("5-1", devices[0].busId.c_str());
        Assert::AreEqual("1234:5678", devices[0].vidPid.c_str());
        Assert::IsFalse(devices[0].IsAttached());

        Assert::AreEqual("5-2", devices[1].busId.c_str());
        Assert::AreEqual("abcd:1234", devices[1].vidPid.c_str());
        Assert::IsTrue(devices[1].IsAttached());
        Assert::AreEqual("Ubuntu", devices[1].wslDistro.c_str());
    }

    TEST_METHOD(EnumerateDevices_NoDevices) {
        MockCommandExecutor mock;
        mock.AddExpectation("usbipd list", { 0,
            "Connected:\r\n"
            "BUSID  VID:PID    DEVICE                         STATE\r\n"
            "\r\n"
            "Persisted:\r\n"
            "BUSID  VID:PID    DEVICE                         STATE\r\n",
            "" });

        DeviceManager dm(&mock);
        auto devices = dm.EnumerateDevices();
        Assert::AreEqual((size_t)0, devices.size());
    }

    TEST_METHOD(EnumerateDevices_CommandFails_ReturnsEmpty) {
        MockCommandExecutor mock;
        mock.AddExpectation("usbipd list", { (DWORD)-1, "", "command not found" });

        DeviceManager dm(&mock);
        auto devices = dm.EnumerateDevices();
        Assert::AreEqual((size_t)0, devices.size());
    }
};

TEST_CLASS(DeviceManagerRefreshTests) {
public:
    TEST_METHOD(Refresh_DetectsNewDevice) {
        MockCommandExecutor mock;
        // First call: one device
        mock.AddExpectation("usbipd list", { 0,
            "Connected:\r\n"
            "BUSID  VID:PID    DEVICE                         STATE\r\n"
            "5-1    1234:5678  USB Device                     Not attached\r\n",
            "" });

        DeviceManager dm(&mock);
        bool changed = dm.Refresh();
        Assert::IsFalse(changed); // first refresh doesn't signal change
        Assert::AreEqual((size_t)0, dm.GetNewDevices().size());
        Assert::AreEqual((size_t)0, dm.GetRemovedDevices().size());

        // Second call: new device appeared
        mock.ClearHistory();
        mock.AddExpectation("usbipd list", { 0,
            "Connected:\r\n"
            "BUSID  VID:PID    DEVICE                         STATE\r\n"
            "5-1    1234:5678  USB Device                     Not attached\r\n"
            "5-2    abcd:1234  New USB Device                 Not attached\r\n",
            "" });

        changed = dm.Refresh();
        Assert::IsTrue(changed);
        Assert::AreEqual((size_t)1, dm.GetNewDevices().size());
        Assert::AreEqual("5-2", dm.GetNewDevices()[0].busId.c_str());
        Assert::AreEqual((size_t)0, dm.GetRemovedDevices().size());
    }

    TEST_METHOD(Refresh_DetectsRemovedDevice) {
        MockCommandExecutor mock;
        mock.AddExpectation("usbipd list", { 0,
            "Connected:\r\n"
            "BUSID  VID:PID    DEVICE                         STATE\r\n"
            "5-1    1234:5678  USB Device                     Not attached\r\n"
            "5-2    abcd:1234  Other USB Device               Not attached\r\n",
            "" });

        DeviceManager dm(&mock);
        dm.Refresh();

        mock.ClearHistory();
        mock.AddExpectation("usbipd list", { 0,
            "Connected:\r\n"
            "BUSID  VID:PID    DEVICE                         STATE\r\n"
            "5-1    1234:5678  USB Device                     Not attached\r\n",
            "" });

        bool changed = dm.Refresh();
        Assert::IsTrue(changed);
        Assert::AreEqual((size_t)0, dm.GetNewDevices().size());
        Assert::AreEqual((size_t)1, dm.GetRemovedDevices().size());
        Assert::AreEqual("5-2", dm.GetRemovedDevices()[0].busId.c_str());
    }

    TEST_METHOD(Refresh_NoChange_ReturnsFalse) {
        MockCommandExecutor mock;
        mock.AddExpectation("usbipd list", { 0,
            "Connected:\r\n"
            "BUSID  VID:PID    DEVICE                         STATE\r\n"
            "5-1    1234:5678  USB Device                     Not attached\r\n",
            "" });

        DeviceManager dm(&mock);
        dm.Refresh();

        mock.ClearHistory();
        mock.AddExpectation("usbipd list", { 0,
            "Connected:\r\n"
            "BUSID  VID:PID    DEVICE                         STATE\r\n"
            "5-1    1234:5678  USB Device                     Not attached\r\n",
            "" });

        bool changed = dm.Refresh();
        Assert::IsFalse(changed);
    }

    TEST_METHOD(Refresh_CallbackInvokedOnChange) {
        MockCommandExecutor mock;
        mock.AddExpectation("usbipd list", { 0,
            "Connected:\r\n"
            "BUSID  VID:PID    DEVICE                         STATE\r\n"
            "5-1    1234:5678  USB Device                     Not attached\r\n",
            "" });

        DeviceManager dm(&mock);
        dm.Refresh();

        int callbackCount = 0;
        dm.SetChangeCallback([&]() { callbackCount++; });

        mock.ClearHistory();
        mock.AddExpectation("usbipd list", { 0,
            "Connected:\r\n"
            "BUSID  VID:PID    DEVICE                         STATE\r\n"
            "5-1    1234:5678  USB Device                     Not attached\r\n"
            "5-2    abcd:1234  New Device                     Not attached\r\n",
            "" });

        dm.Refresh();
        Assert::AreEqual(1, callbackCount);
    }

    TEST_METHOD(Refresh_CallbackNotInvokedOnNoChange) {
        MockCommandExecutor mock;
        mock.AddExpectation("usbipd list", { 0,
            "Connected:\r\n"
            "BUSID  VID:PID    DEVICE                         STATE\r\n"
            "5-1    1234:5678  USB Device                     Not attached\r\n",
            "" });

        DeviceManager dm(&mock);
        dm.Refresh();

        int callbackCount = 0;
        dm.SetChangeCallback([&]() { callbackCount++; });

        mock.ClearHistory();
        mock.AddExpectation("usbipd list", { 0,
            "Connected:\r\n"
            "BUSID  VID:PID    DEVICE                         STATE\r\n"
            "5-1    1234:5678  USB Device                     Not attached\r\n",
            "" });

        dm.Refresh();
        Assert::AreEqual(0, callbackCount);
    }
};

TEST_CLASS(DeviceManagerPresentBusIdsTests) {
public:
    TEST_METHOD(GetPresentBusIds_ReturnsBusIds) {
        MockCommandExecutor mock;
        mock.AddExpectation("usbipd list", { 0,
            "Connected:\r\n"
            "BUSID  VID:PID    DEVICE                         STATE\r\n"
            "5-1    1234:5678  USB Device                     Not attached\r\n"
            "5-2    abcd:1234  USB Device 2                   Attached - Ubuntu\r\n",
            "" });

        DeviceManager dm(&mock);
        auto busIds = dm.GetPresentBusIds();

        Assert::AreEqual((size_t)2, busIds.size());
        Assert::AreEqual("5-1", busIds[0].c_str());
        Assert::AreEqual("5-2", busIds[1].c_str());
    }

    TEST_METHOD(GetPresentBusIds_CommandFails_ReturnsEmpty) {
        MockCommandExecutor mock;
        mock.AddExpectation("usbipd list", { (DWORD)-1, "", "failed" });

        DeviceManager dm(&mock);
        auto busIds = dm.GetPresentBusIds();
        Assert::AreEqual((size_t)0, busIds.size());
    }
};
