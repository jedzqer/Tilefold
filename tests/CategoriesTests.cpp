// 桌面分组映射（data\categories.tsv）的解析规则。
// 这张表是用户自己在文本编辑器里改的，写错一个字符不能让图标消失，
// 所以逐条钉住：注释、空行、BOM、CRLF、多余空白、大小写、行内没有制表符……都试一遍。
//
// 注意：测试数据一律用占位名字（分组甲 / 示例应用.lnk），不要写真实软件名——
// 个人软件清单属于用户数据，只能待在 data\categories.tsv 里，不能出现在仓库。

#include "core/Categories.h"

#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void Check(const wchar_t* what, const std::wstring& got, const std::wstring& want) {
    if (got == want) return;
    std::printf("FAIL %ls\n  got  = [%ls]\n  want = [%ls]\n", what, got.c_str(), want.c_str());
    ++g_failures;
}

void CheckCount(const wchar_t* what, std::size_t got, std::size_t want) {
    if (got == want) return;
    std::printf("FAIL %ls\n  got  = %zu\n  want = %zu\n", what, got, want);
    ++g_failures;
}

void CheckNull(const wchar_t* what, const std::wstring* got) {
    if (got == nullptr) return;
    std::printf("FAIL %ls\n  unexpectedly matched group [%ls]\n", what, got->c_str());
    ++g_failures;
}

void CheckGroup(const wchar_t* what, const std::wstring* got, const std::wstring& want) {
    if (got != nullptr && *got == want) return;
    std::printf("FAIL %ls\n  got  = [%ls]\n  want = [%ls]\n", what,
                got == nullptr ? L"<null>" : got->c_str(), want.c_str());
    ++g_failures;
}

void TestParseBasics() {
    using tf::cfg::ParseCategories;

    const auto one = ParseCategories(L"分组甲\t示例应用.lnk\n");
    CheckCount(L"single row", one.size(), 1);
    if (one.size() == 1) {
        Check(L"single row group", one[0].group, L"分组甲");
        Check(L"single row filename", one[0].filename, L"示例应用.lnk");
    }

    // 组名在前、文件名在后（制表符分隔），一行一条
    const auto two = ParseCategories(L"分组甲\t示例应用.lnk\n分组乙\t另一个应用.lnk\n");
    CheckCount(L"two rows", two.size(), 2);
    if (two.size() == 2) {
        Check(L"two rows [0]", two[0].group + L"|" + two[0].filename, L"分组甲|示例应用.lnk");
        Check(L"two rows [1]", two[1].group + L"|" + two[1].filename, L"分组乙|另一个应用.lnk");
    }
}

void TestParseTolerance() {
    using tf::cfg::ParseCategories;

    // 注释、空行、只有空白的行都要忽略（用户会往里写说明）
    const auto commented = ParseCategories(
        L"# 桌面分组映射\n"
        L"\n"
        L"   \t  \n"
        L"#分组甲\t示例应用.lnk\n"
        L"分组甲\t示例应用.lnk\n");
    CheckCount(L"comments skipped", commented.size(), 1);

    // 带 BOM 的文件（记事本另存可能加）不能把第一行的注释/数据吃掉
    const auto bom = ParseCategories(L"\uFEFF分组甲\t示例应用.lnk\n");
    CheckCount(L"bom tolerated", bom.size(), 1);
    if (bom.size() == 1) Check(L"bom group", bom[0].group, L"分组甲");

    // 带 BOM 的注释行同样得当成注释
    const auto bomComment = ParseCategories(L"\uFEFF#注释\n分组甲\t示例应用.lnk\n");
    CheckCount(L"bom comment tolerated", bomComment.size(), 1);

    // CRLF 换行：\r 不能留在字段尾巴上，否则永远匹配不上文件名
    const auto crlf = ParseCategories(L"分组甲\t示例应用.lnk\r\n分组乙\t另一个应用.lnk\r\n");
    CheckCount(L"crlf rows", crlf.size(), 2);
    if (crlf.size() == 2) Check(L"crlf filename trimmed", crlf[0].filename, L"示例应用.lnk");

    // 字段两边的空白（用户手动对齐）要吃掉
    const auto spaced = ParseCategories(L"  分组甲  \t  示例应用.lnk  \n");
    CheckCount(L"spaces trimmed row", spaced.size(), 1);
    if (spaced.size() == 1) {
        Check(L"spaces trimmed group", spaced[0].group, L"分组甲");
        Check(L"spaces trimmed filename", spaced[0].filename, L"示例应用.lnk");
    }

    // 没有制表符的行猜不出归属，直接跳过；空字段同理
    const auto malformed = ParseCategories(L"分组甲 示例应用.lnk\n分组甲\t\n\t示例应用.lnk\n");
    CheckCount(L"malformed rows skipped", malformed.size(), 0);

    // 同一个分组可以有多条；重复的行不去重（由调用方幂等处理）
    const auto dup = ParseCategories(L"分组甲\tA.lnk\n分组甲\tB.lnk\n");
    CheckCount(L"same group twice", dup.size(), 2);
}

void TestLookup() {
    using tf::cfg::GroupForPath;

    const auto table = tf::cfg::ParseCategories(
        L"分组甲\t示例应用.lnk\n"
        L"分组乙\t大小写扩展名.LNK\n");

    // 传进来的是完整路径，只按文件名比
    CheckGroup(L"full path", GroupForPath(table, L"D:\\Desktop\\示例应用.lnk"), L"分组甲");

    // Windows 文件名不区分大小写：.LNK 和 .lnk 是同一个东西
    CheckGroup(L"case insensitive ext", GroupForPath(table, L"D:\\Desktop\\大小写扩展名.lnk"), L"分组乙");
    CheckGroup(L"case insensitive name", GroupForPath(table, L"D:\\Desktop\\示例应用.LNK"), L"分组甲");

    // 没配过的、空路径、空表都是 nullptr（= 当普通独立图标铺开，不是错误）
    CheckNull(L"unmapped file", GroupForPath(table, L"D:\\Desktop\\未配置.lnk"));
    CheckNull(L"empty path", GroupForPath(table, L""));
    CheckNull(L"empty table", GroupForPath({}, L"D:\\Desktop\\示例应用.lnk"));

    // 键是完整文件名（含扩展名）：只差扩展名就不算同一项。
    // 这条故意固定成“不匹配”：宁可让用户重新指一遍，也不要凭同名把无关文件吸进分组
    CheckNull(L"same name other ext", GroupForPath(table, L"D:\\Desktop\\示例应用.exe"));
}

void TestPathIsUnderDataDir() {
    // 映射表必须待在数据目录里（data\categories.tsv），不能跑到程序目录或 %APPDATA%
    const std::wstring path = tf::cfg::CategoriesPath();
    const std::wstring suffix = L"\\categories.tsv";
    Check(L"path suffix",
          path.size() >= suffix.size() ? path.substr(path.size() - suffix.size()) : path, suffix);
}

}  // namespace

int main() {
    TestParseBasics();
    TestParseTolerance();
    TestLookup();
    TestPathIsUnderDataDir();

    if (g_failures != 0) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("categories: all checks passed\n");
    return 0;
}
