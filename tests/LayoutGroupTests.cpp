// 虚拟分组的纯数据行为：布局文件是唯一来源，不涉及文件系统。

#include "core/Layout.h"
#include "core/ReservedNames.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool ok, const char* message) {
    if (ok) return;
    std::printf("FAIL %s\n", message);
    ++g_failures;
}

// 手改 layout.tsv / 旧数据里的两个坑：同一个路径出现在两个分组（GroupForPath 只认第一个，
// 另一条永远是幽灵），以及用户分组叫了系统槽位的名字（删分组会连此电脑/回收站一起清掉）。
void TestParseHardening() {
    using namespace tf;

    const std::string dup =
        "# Tilefold layout v6\n"
        "cols\t0\t\t0\t0\t0\t0\n"
        "group\tA\t\t0\t0\t0\t0\n"
        "group\tB\t\t0\t0\t0\t0\n"
        "gitem\tA\tshared.lnk\t0\t0\t1\t1\n"
        "gitem\tB\tshared.lnk\t0\t0\t1\t1\n";
    const cfg::Layout layout = cfg::ParseLayoutBytes(dup, L"test");
    const cfg::GroupState* a = cfg::FindGroup(layout, L"A");
    const cfg::GroupState* b = cfg::FindGroup(layout, L"B");
    Check(a != nullptr && a->items.size() == 1, "duplicate path kept in the first group");
    Check(b != nullptr && b->items.empty(), "duplicate path dropped from the second group");
    const cfg::GroupState* owner = cfg::GroupForPath(layout, L"shared.lnk");
    Check(owner != nullptr && owner->name == L"A", "one owner per path");

    // 保留名不能当分组读进来：那会让 RemoveGroup 把它下面的系统记录一起清掉
    const std::string reserved =
        "# Tilefold layout v6\n"
        "cols\t0\t\t0\t0\t0\t0\n"
        "group\tsystem\t\t0\t0\t0\t0\n"
        "group\t__desktop_items__\t\t0\t0\t0\t0\n"
        "group\tMine\t\t0\t0\t0\t0\n";
    const cfg::Layout guarded = cfg::ParseLayoutBytes(reserved, L"test");
    Check(cfg::FindGroup(guarded, L"system") == nullptr, "reserved group name 'system' ignored");
    Check(cfg::FindGroup(guarded, L"__desktop_items__") == nullptr,
          "reserved group name '__desktop_items__' ignored");
    Check(cfg::FindGroup(guarded, L"Mine") != nullptr, "normal group still parsed");

    // 就算内存里已经有了，RemoveGroup 也要拒绝：那是此电脑/回收站和未分组桌面条目的槽位
    cfg::Layout live;
    cfg::EnsureGroup(live, L"system");
    cfg::EnsureGroup(live, L"Real");
    cfg::Entry thisPc;
    thisPc.kind = L"shell";
    thisPc.group = L"system";
    thisPc.item = L"ThisPC";
    thisPc.spanCols = 1;
    thisPc.spanRows = 1;
    cfg::SetEntry(live, thisPc);
    Check(!cfg::RemoveGroup(live, L"SYSTEM"), "remove reserved group refused (case-insensitive)");
    Check(cfg::FindEntry(live, L"shell", L"system", L"ThisPC") != nullptr,
          "shell entry survives a refused remove");
    Check(cfg::RemoveGroup(live, L"Real"), "normal group still removable");

    Check(cfg::IsReservedGroupName(L"__Desktop_Items__"), "reserved check is case-insensitive");
    Check(!cfg::IsReservedGroupName(L"Systems"), "similar names are not reserved");
}

}  // namespace

int main() {
    using namespace tf;
    cfg::Layout layout;
    cfg::EnsureGroup(layout, L"A");
    cfg::EnsureGroup(layout, L"B");
    Check(layout.groups.size() == 2, "ensure groups");

    Check(cfg::AddGroupItem(layout, L"A", L"C:/Temp/one.lnk", 0), "add one");
    Check(cfg::AddGroupItem(layout, L"A", L"C:/Temp/two.lnk", 1), "add two");
    Check(cfg::AddGroupItem(layout, L"B", L"C:/Temp/three.lnk", 0), "add three");
    Check(cfg::GroupForPath(layout, L"C:/Temp/two.lnk") != nullptr, "find membership");

    Check(cfg::MoveGroupItem(layout, L"C:/Temp/two.lnk", L"B"), "move membership");
    const cfg::GroupState* groupA = cfg::FindGroup(layout, L"A");
    const cfg::GroupState* groupB = cfg::FindGroup(layout, L"B");
    Check(groupA != nullptr && groupA->items.size() == 1, "source group after move");
    Check(groupB != nullptr && groupB->items.size() == 2, "target group after move");

    cfg::SetGroupItemOrder(layout, L"B", {L"C:/Temp/two.lnk", L"C:/Temp/three.lnk"});
    Check(groupB->items[0].path == L"C:/Temp/two.lnk", "reorder first");
    Check(groupB->items[0].order == 0 && groupB->items[1].order == 1, "reorder indices");

    cfg::Entry icon;
    icon.kind = L"icon";
    icon.group = L"B";
    icon.item = L"C:/Temp/two.lnk";
    icon.spanCols = 1;
    icon.spanRows = 1;
    cfg::SetEntry(layout, icon);
    Check(cfg::IsExtracted(layout, L"B", L"C:/Temp/two.lnk"), "extracted icon");

    Check(cfg::RemoveGroup(layout, L"B"), "remove group");
    Check(layout.groups.size() == 1, "group removed");
    Check(cfg::FindEntry(layout, L"icon", L"B", L"C:/Temp/two.lnk") == nullptr, "group entries removed");

    if (g_failures == 0) std::printf("Layout groups: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
