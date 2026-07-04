#include "CppUnitTest.h"
#include "../src/common/Error.h"
#include "../src/common/Utils.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

TEST_CLASS(UtilsTests) {
public:
    TEST_METHOD(Split_Basic) {
        auto tokens = utils::Split("a,b,c", ',');
        Assert::AreEqual((size_t)3, tokens.size());
        Assert::AreEqual("a", tokens[0].c_str());
        Assert::AreEqual("b", tokens[1].c_str());
        Assert::AreEqual("c", tokens[2].c_str());
    }

    TEST_METHOD(Split_EmptyString) {
        auto tokens = utils::Split("", ',');
        Assert::AreEqual((size_t)0, tokens.size());
    }

    TEST_METHOD(Split_NoDelimiter) {
        auto tokens = utils::Split("hello", ',');
        Assert::AreEqual((size_t)1, tokens.size());
        Assert::AreEqual("hello", tokens[0].c_str());
    }

    TEST_METHOD(Trim_LeadingSpaces) {
        std::string result = utils::Trim("  hello");
        Assert::AreEqual("hello", result.c_str());
    }

    TEST_METHOD(Trim_TrailingSpaces) {
        std::string result = utils::Trim("hello  ");
        Assert::AreEqual("hello", result.c_str());
    }

    TEST_METHOD(Trim_BothSides) {
        std::string result = utils::Trim("  hello world  ");
        Assert::AreEqual("hello world", result.c_str());
    }

    TEST_METHOD(Trim_Newlines) {
        std::string result = utils::Trim("\r\nhello\r\n");
        Assert::AreEqual("hello", result.c_str());
    }

    TEST_METHOD(Trim_EmptyString) {
        std::string result = utils::Trim("");
        Assert::AreEqual("", result.c_str());
    }

    TEST_METHOD(Trim_AllWhitespace) {
        std::string result = utils::Trim("   \t\r\n");
        Assert::AreEqual("", result.c_str());
    }

    TEST_METHOD(StartsWith_Matching) {
        Assert::IsTrue(utils::StartsWith("hello world", "hello"));
    }

    TEST_METHOD(StartsWith_NotMatching) {
        Assert::IsFalse(utils::StartsWith("hello world", "world"));
    }

    TEST_METHOD(StartsWith_EmptyPrefix) {
        Assert::IsTrue(utils::StartsWith("hello", ""));
    }

    TEST_METHOD(StartsWith_LongerPrefix) {
        Assert::IsFalse(utils::StartsWith("hel", "hello"));
    }

    TEST_METHOD(Replace_Basic) {
        std::string result = utils::Replace("hello world", "world", "there");
        Assert::AreEqual("hello there", result.c_str());
    }

    TEST_METHOD(Replace_NoMatch) {
        std::string result = utils::Replace("hello world", "xyz", "abc");
        Assert::AreEqual("hello world", result.c_str());
    }

    TEST_METHOD(Replace_Multiple) {
        std::string result = utils::Replace("a,b,c", ",", " | ");
        Assert::AreEqual("a | b | c", result.c_str());
    }

    TEST_METHOD(Utf8Roundtrip) {
        std::wstring original = L"Hello 世界 🌍";
        std::string utf8 = utils::WStringToUtf8(original);
        std::wstring restored = utils::Utf8ToWString(utf8);
        Assert::AreEqual(original, restored);
    }

    TEST_METHOD(Utf8Empty) {
        Assert::AreEqual("", utils::WStringToUtf8(L"").c_str());
        Assert::AreEqual(L"", utils::Utf8ToWString("").c_str());
    }

    TEST_METHOD(OemToUtf8_Empty) {
        Assert::AreEqual("", utils::OemToUtf8("").c_str());
    }

    TEST_METHOD(OemToUtf8_AsciiIsSame) {
        // ASCII characters are the same in OEM and UTF-8
        std::string result = utils::OemToUtf8("hello");
        Assert::AreEqual("hello", result.c_str());
    }
};

TEST_CLASS(ErrorCodeTests) {
public:
    TEST_METHOD(Result_Ok) {
        Result r = Result::Ok();
        Assert::IsTrue(r.IsSuccess());
        Assert::AreEqual((int)ErrorCode::Success, (int)r.code);
    }

    TEST_METHOD(Result_Fail) {
        Result r = Result::Fail(ErrorCode::WslNotInstalled, "WSL is not installed");
        Assert::IsFalse(r.IsSuccess());
        Assert::AreEqual((int)ErrorCode::WslNotInstalled, (int)r.code);
        Assert::AreEqual("WSL is not installed", r.message.c_str());
    }
};
