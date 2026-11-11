// 分组名的清洗规则：纯字符串逻辑，不需要窗口和 GPU。
// 这些规则是给用户输入兜底的（非法字符、DOS 设备名、超长、重名），错一条就是
// 一个奇怪的布局键或者一个看不见的分组，所以逐条钉住。

#include "shell/GroupFolder.h"

#include <cstdio>
#include <string>

namespace {

int g_failures = 0;

void Check(const wchar_t* what, const std::wstring& got, const std::wstring& want) {
    if (got == want) return;
    std::printf("FAIL %ls\n  got  = [%ls]\n  want = [%ls]\n", what, got.c_str(), want.c_str());
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

    // "system" / "__desktop_items__" 是 layout.tsv 里的系统槽位：用户用这两个名字建分组，
    // 删分组时 RemoveGroup 会把此电脑/回收站（或未分组桌面条目）的布局一起清掉。
    // 跟设备名一样补个下划线就彻底避开了。
    Check(L"reserved system", SanitizeGroupName(L"system"), L"_system");
    Check(L"reserved system case", SanitizeGroupName(L"SyStEm"), L"_SyStEm");
    Check(L"reserved desktop slot", SanitizeGroupName(L"__desktop_items__"), L"___desktop_items__");
    Check(L"systems is fine", SanitizeGroupName(L"systems"), L"systems");
    Check(L"my system is fine", SanitizeGroupName(L"my system"), L"my system");
}

}  // namespace

int main() {
    TestSanitize();
    if (g_failures == 0) std::printf("GroupFolder: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
