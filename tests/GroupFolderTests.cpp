// 「新建收纳文件夹」的命名/建目录规则：纯字符串 + 文件系统逻辑，不需要窗口和 GPU。
// 这些规则是给用户输入兜底的（非法字符、DOS 设备名、超长、重名），错一条就是
// 一个建不出来的目录或者一个看不见的分组，所以逐条钉住。

#include "shell/GroupFolder.h"

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <string>

namespace {

namespace fs = std::filesystem;

int g_failures = 0;

void Check(const wchar_t* what, const std::wstring& got, const std::wstring& want) {
    if (got == want) return;
    std::printf("FAIL %ls\n  got  = [%ls]\n  want = [%ls]\n", what, got.c_str(), want.c_str());
    ++g_failures;
}

void CheckTrue(const wchar_t* what) {
    std::printf("FAIL %ls\n", what);
    ++g_failures;
}

void TestSanitize() {
    using tf::box::SanitizeGroupName;

    // 文件名里不允许的 9 个字符直接丢掉（不是替换成空格：替换会把 "a/b" 变成 "a b"）
    Check(L"forbidden chars", SanitizeGroupName(L"a/b\\c:d*e?f\"g<h>i|j"), L"abcdefghij");

    // 连续空白压成一个；全角空格也算空白
    Check(L"collapse spaces", SanitizeGroupName(L"a   b"), L"a b");
    Check(L"ideographic space", SanitizeGroupName(L"a\u3000b"), L"a b");
    Check(L"leading space", SanitizeGroupName(L"   abc"), L"abc");

    // 结尾的空格和点：Windows 会悄悄吃掉它们，导致名字和用户输入不一致
    Check(L"trailing dots", SanitizeGroupName(L"name . . "), L"name");
    Check(L"only dots", SanitizeGroupName(L"  ..  "), tf::box::kDefaultGroupName);

    // 控制字符
    Check(L"control chars", SanitizeGroupName(L"a\x01\x1f" L"b"), L"ab");

    // 洗完之后什么都不剩 -> 退回默认名
    Check(L"empty -> default", SanitizeGroupName(L""), tf::box::kDefaultGroupName);
    Check(L"junk -> default", SanitizeGroupName(L"///"), tf::box::kDefaultGroupName);

    // DOS 设备名：CON、nul.txt、COM7 这些无论扩展名都建不出来
    Check(L"CON", SanitizeGroupName(L"CON"), L"_CON");
    Check(L"nul.txt", SanitizeGroupName(L"nul.txt"), L"_nul.txt");
    Check(L"com7", SanitizeGroupName(L"com7"), L"_com7");
    Check(L"nullify is fine", SanitizeGroupName(L"nullify"), L"nullify");

    // 超长截断，且截断后不能留着结尾的点/空格
    const std::wstring longName = std::wstring(60, L'x') + L"...";
    const std::wstring truncated = SanitizeGroupName(longName);
    if (truncated.size() != 48) {
        std::printf("FAIL truncate length: %zu\n", truncated.size());
        ++g_failures;
    }

    // 正常名字原样保留
    Check(L"plain", SanitizeGroupName(L"AI 工具"), L"AI 工具");
}

void TestCreate() {
    std::error_code ec;
    const fs::path root = fs::temp_directory_path(ec) /
        (L"tilefold-group-test-" + std::to_wstring(::GetCurrentProcessId()));
    if (ec) {
        CheckTrue(L"temp_directory_path failed");
        return;
    }
    fs::remove_all(root, ec);

    // 第一次：用默认名
    const std::wstring first = tf::box::CreateGroupFolder(root.wstring(), tf::box::kDefaultGroupName);
    if (first.empty() || !fs::is_directory(fs::path(first), ec)) {
        CheckTrue(L"first CreateGroupFolder failed");
        fs::remove_all(root, ec);
        return;
    }

    // 第二次：同名要自动变成 "新文件夹 (2)"，不能覆盖也不能失败
    const std::wstring second = tf::box::CreateGroupFolder(root.wstring(), tf::box::kDefaultGroupName);
    const fs::path secondPath(second);
    const std::wstring expected = std::wstring(tf::box::kDefaultGroupName) + L" (2)";
    Check(L"unique name", secondPath.filename().wstring(), expected);

    // 第三次 -> "新文件夹 (3)"
    const std::wstring third = tf::box::CreateGroupFolder(root.wstring(), tf::box::kDefaultGroupName);
    Check(L"unique name 3", fs::path(third).filename().wstring(),
          std::wstring(tf::box::kDefaultGroupName) + L" (3)");

    // 非法名字要经过 Sanitize 再落盘
    const std::wstring dirty = tf::box::CreateGroupFolder(root.wstring(), L"a/b:c");
    Check(L"sanitized on create", fs::path(dirty).filename().wstring(), L"abc");

    // 分组根目录不存在时应该自己建出来（用户可能删过 data\box）
    const fs::path nested = root / L"fresh" / L"deeper";
    const std::wstring nestedCreated = tf::box::CreateGroupFolder(nested.wstring(), L"收纳");
    if (nestedCreated.empty() || !fs::is_directory(nested, ec)) {
        CheckTrue(L"CreateGroupFolder should create missing root");
    }

    fs::remove_all(root, ec);
}

}  // namespace

int main() {
    TestSanitize();
    TestCreate();
    if (g_failures == 0) std::printf("GroupFolder: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
