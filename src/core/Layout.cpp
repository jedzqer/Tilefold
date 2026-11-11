#include "Layout.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <vector>

#include "core/Grid.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/ReservedNames.h"

namespace {

HANDLE g_layoutLock = INVALID_HANDLE_VALUE;

std::string ToUtf8(std::wstring_view w) {
    if (w.empty()) return {};
    const int need = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0,
                                           nullptr, nullptr);
    if (need <= 0) return {};
    std::string out(static_cast<size_t>(need), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), need, nullptr,
                          nullptr);
    return out;
}

std::wstring FromUtf8(std::string_view s) {
    if (s.empty()) return {};
    const int need = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (need <= 0) return {};
    std::wstring out(static_cast<size_t>(need), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), need);
    return out;
}

bool ReadWholeFile(const std::wstring& path, std::string& out) {
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!::GetFileSizeEx(h, &size) || size.QuadPart <= 0 || size.QuadPart > (8 << 20)) {
        ::CloseHandle(h);
        return false;
    }
    out.assign(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const BOOL ok = ::ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &read, nullptr);
    ::CloseHandle(h);
    if (!ok) return false;
    out.resize(read);
    return true;
}

// 先写 .tmp 再原子替换：进程被杀/断电/磁盘满的时候，layout.tsv 要么是完整的旧内容，
// 要么是完整的新内容，绝不会出现写了一半的截断文件。
// （以前直接 CREATE_ALWAYS 覆盖原文件，写到一半挂掉就把排布彻底毁掉了）
bool WriteWholeFileAtomic(const std::wstring& path, const std::string& data) {
    const std::wstring tmp = path + L".tmp";
    HANDLE h = ::CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = ::WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
    // 落盘再改名：否则改名成功了内容还在缓存里，断电后是个空文件
    const BOOL flushed = (ok && written == data.size()) ? ::FlushFileBuffers(h) : FALSE;
    ::CloseHandle(h);
    if (!ok || written != data.size() || !flushed) {
        ::DeleteFileW(tmp.c_str());
        return false;
    }
    if (!::MoveFileExW(tmp.c_str(), path.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ::DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

// 每次覆盖前把现役文件留一份 layout.tsv.bak（= 上一个已知good 状态）
void RotateBackup(const std::wstring& path) {
    const std::wstring bak = path + L".bak";
    if (::GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    if (!::CopyFileW(path.c_str(), bak.c_str(), FALSE)) {
        tf::log::Warnf(L"布局备份失败: {} -> {}", path, bak);
    }
}

// 每次进程启动只留一份历史快照（保存很频繁，逐次快照会把目录塞爆）
// 历史快照只留最近 keep 份：文件名里带时间戳，字典序就是时间序
void PruneHistory(const std::wstring& dir, std::size_t keep) {
    std::vector<std::wstring> names;
    WIN32_FIND_DATAW fd{};
    HANDLE h = ::FindFirstFileW((dir + L"\\layout-*.tsv").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) names.emplace_back(fd.cFileName);
    } while (::FindNextFileW(h, &fd));
    ::FindClose(h);
    if (names.size() <= keep) return;
    std::sort(names.begin(), names.end());
    for (std::size_t i = 0; i + keep < names.size(); ++i) {
        ::DeleteFileW((dir + L"\\" + names[i]).c_str());
    }
}

// 每次进程启动只留一份历史快照（保存很频繁，逐次快照会把目录塞爆）。
// 裁剪也放在这里：快照数量只会因为它而增长，一次会话裁一次就够，
// 而且不依赖“这次会话到底保存过几次”。
void SnapshotOncePerSession(const std::wstring& path) {
    static bool done = false;
    if (done) return;
    done = true;

    const std::wstring dir = tf::paths::DataDir() + L"\\layout-history";
    std::string bytes;
    if (ReadWholeFile(path, bytes) && !bytes.empty() && tf::paths::EnsureDir(dir)) {
        SYSTEMTIME st{};
        ::GetLocalTime(&st);
        const std::wstring file = std::format(
            L"{}\\layout-{:04}{:02}{:02}-{:02}{:02}{:02}.tsv", dir, st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute, st.wSecond);
        if (!WriteWholeFileAtomic(file, bytes)) {
            tf::log::Warnf(L"布局历史快照写入失败: {}", file);
        }
    }
    PruneHistory(dir, 10);
}

std::vector<std::wstring> SplitLines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    std::wstring cur;
    for (const wchar_t c : text) {
        if (c == L'\n') {
            lines.push_back(cur);
            cur.clear();
        } else if (c != L'\r') {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

// 按 TAB 切分并保留空字段（item 为空是有意义的：folder 没有条目名）
std::vector<std::wstring> SplitTab(const std::wstring& line) {
    std::vector<std::wstring> fields;
    std::wstring cur;
    for (const wchar_t c : line) {
        if (c == L'\t') {
            fields.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    fields.push_back(cur);
    return fields;
}

bool ParseInt(const std::wstring& s, int& out) {
    if (s.empty()) return false;
    return ::swscanf_s(s.c_str(), L"%d", &out) == 1;
}

}  // namespace

namespace tf::cfg {

std::wstring LayoutPath() { return tf::paths::LayoutPath(); }

bool AcquireLayoutFile() {
    if (g_layoutLock != INVALID_HANDLE_VALUE) return true;
    const std::wstring dir = tf::paths::DataDir();
    if (!tf::paths::EnsureDir(dir)) {
        tf::log::Errorf(L"无法创建布局数据目录: {}", dir);
        return false;
    }

    const std::wstring lockPath = dir + L"\\.tilefold-layout.lock";
    HANDLE lock = ::CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr);
    if (lock == INVALID_HANDLE_VALUE) {
        tf::log::Errorf(L"无法独占锁定布局目录: {} err={}", lockPath, ::GetLastError());
        return false;
    }

    const std::wstring probe = dir + L"\\.tilefold-write-test.tmp";
    HANDLE test = ::CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (test == INVALID_HANDLE_VALUE) {
        tf::log::Errorf(L"布局目录不可写: {} err={}", dir, ::GetLastError());
        ::CloseHandle(lock);
        return false;
    }
    const char byte = 'x';
    DWORD written = 0;
    const BOOL wrote = ::WriteFile(test, &byte, 1, &written, nullptr);
    const BOOL flushed = wrote && written == 1 ? ::FlushFileBuffers(test) : FALSE;
    ::CloseHandle(test);
    if (!flushed) {
        tf::log::Errorf(L"布局目录写入测试失败: {} err={}", dir, ::GetLastError());
        ::CloseHandle(lock);
        return false;
    }
    g_layoutLock = lock;
    tf::log::Infof(L"布局目录独占锁定且写入预检通过: {}", dir);
    return true;
}

void ReleaseLayoutFile() {
    if (g_layoutLock == INVALID_HANDLE_VALUE) return;
    ::CloseHandle(g_layoutLock);
    g_layoutLock = INVALID_HANDLE_VALUE;
}

// 把字节解析成 Layout。label 只用于日志，方便看出这批内容来自主文件还是备份
Layout ParseLayoutBytes(const std::string& bytes, const std::wstring& label) {
    Layout layout;

    const std::wstring text = FromUtf8(bytes);
    size_t lineNo = 0;
    size_t skipped = 0;
    int fileVersion = 3;
    for (const std::wstring& line : SplitLines(text)) {
        ++lineNo;
        if (line.empty()) continue;
        if (line[0] == L'#') {
            // 旧文件带着 v2 头：那时候 cols 是“列数=8”的固定值，
            // 现在 1×1 要跟原生图标一样大，列数必须按分辨率自动算，
            // 所以把旧文件的显式列数当作自动处理。
            if (line.find(L"layout v6") != std::wstring::npos) {
                fileVersion = 6;
            } else if (line.find(L"layout v5") != std::wstring::npos) {
                fileVersion = 5;
            } else if (line.find(L"layout v4") != std::wstring::npos) {
                fileVersion = 4;
            } else if (line.find(L"layout v2") != std::wstring::npos) {
                fileVersion = 2;
            }
            continue;
        }

        const std::vector<std::wstring> f = SplitTab(line);
        // 新格式：kind<TAB>group<TAB>item<TAB>col<TAB>row<TAB>spanCols<TAB>spanRows
        if (f.size() == 7 || f.size() == 8) {
            if (f[0] == L"cols") {
                int v = grid::kAutoCols;
                if (ParseInt(f[1], v) && fileVersion >= 3) layout.cols = v;
                continue;
            }
            // v4 新增：上次实际生效的网格 grid<TAB>cols<TAB>pitch×100<TAB>workW<TAB>workH
            if (f[0] == L"grid") {
                int c = 0, pitchX100 = 0, ww = 0, wh = 0;
                if (ParseInt(f[1], c) && ParseInt(f[2], pitchX100) && ParseInt(f[3], ww) &&
                    ParseInt(f[4], wh) && c > 0 && pitchX100 > 0) {
                    layout.grid.cols = c;
                    layout.grid.pitch = static_cast<float>(pitchX100) / 100.0f;
                    layout.grid.workW = ww;
                    layout.grid.workH = wh;
                    layout.grid.valid = true;
                } else {
                    log::Warnf(L"layout.tsv 第 {} 行的 grid 记录无效，按自动网格处理", lineNo);
                }
                continue;
            }
            if (f[0] == L"monitor") {
                int c = 0, pitch = 0, w = 0, h = 0;
                if (ParseInt(f[2], c) && ParseInt(f[3], pitch) && ParseInt(f[4], w) &&
                    ParseInt(f[5], h) && c > 0 && pitch > 0 && w > 0 && h > 0) {
                    layout.monitors.push_back({f[1], {c, pitch / 100.0f, w, h, true}});
                }
                continue;
            }
            if (f[0] == L"group") {
                if (f[1].empty()) continue;
                // 手改出来的 / 老版本存下来的保留名不是分组（那是此电脑/回收站和未分组桌面
                // 条目的槽位）：当分组读进来以后 RemoveGroup 就会把它们一起清掉，直接忽略。
                if (IsReservedGroupName(f[1])) {
                    log::Warnf(L"layout.tsv 第 {} 行用了保留分组名 [{}]，已忽略该 group 行", lineNo, f[1]);
                    ++skipped;
                    continue;
                }
                EnsureGroup(layout, f[1]);
                continue;
            }
            if (f[0] == L"gitem") {
                int order = 0;
                if (f[1].empty() || f[2].empty() || !ParseInt(f[3], order)) {
                    ++skipped;
                    continue;
                }
                // 一个路径只能属于一个分组：GroupForPath() 只认第一个，同一个图标出现在
                // 两个分组时会有一条永远是幽灵（点了没反应、也删不掉），解析时直接丢掉。
                if (const GroupState* owner = GroupForPath(layout, f[2]);
                    owner != nullptr && owner->name != f[1]) {
                    log::Warnf(L"layout.tsv 第 {} 行：{} 已经属于分组 [{}]，忽略分组 [{}] 里的重复记录",
                               lineNo, f[2], owner->name, f[1]);
                    ++skipped;
                    continue;
                }
                AddGroupItem(layout, f[1], f[2], order);
                continue;
            }
            Entry e;
            e.kind = f[0];
            e.group = f[1];
            e.item = f[2];
            if (f.size() == 8) e.monitor = f[7];
            if (e.kind != L"folder" && e.kind != L"icon" && e.kind != L"order" &&
                e.kind != L"shell" && e.kind != L"desktop") {
                ++skipped;
                continue;
            }
            if (!ParseInt(f[3], e.col) || !ParseInt(f[4], e.row) ||
                !ParseInt(f[5], e.spanCols) || !ParseInt(f[6], e.spanRows)) {
                log::Warnf(L"layout.tsv 第 {} 行数值解析失败，已跳过", lineNo);
                ++skipped;
                continue;
            }
            layout.entries.push_back(std::move(e));
            continue;
        }

        // 旧格式（v1）：分组名<TAB>x<TAB>y —— 像素坐标，网格化之后没有意义，直接丢弃重排
        ++skipped;
    }

    log::Infof(L"已读取布局 v{}：{} 项，列数={}（0=自动），网格={} 列/格距 {:.1f}，跳过 {} 行 ({})",
               fileVersion, layout.entries.size(), layout.cols,
               layout.grid.valid ? std::to_wstring(layout.grid.cols) : std::wstring(L"未记录"),
               layout.grid.pitch, skipped, label);
    return layout;
}

Layout LoadLayout() {
    const std::wstring path = LayoutPath();
    const std::wstring bak = path + L".bak";

    std::string bytes;
    if (ReadWholeFile(path, bytes)) {
        Layout main = ParseLayoutBytes(bytes, L"layout.tsv");
        // 有条目（或至少记着网格）就算读到了东西，直接用
        if (!main.entries.empty() || !main.groups.empty() || main.grid.valid) return main;
        log::Warnf(L"布局文件里一条记录都没有，可能是上次写到一半被打断，尝试从备份恢复");
    } else {
        log::Infof(L"没有布局文件，使用默认排布: {}", path);
    }

    // 主文件缺失或解析成空：退回 .bak，并把内容写回主文件，
    // 这样后面的保存轮换（main -> .bak）不会把这份好数据又盖掉
    std::string bakBytes;
    if (ReadWholeFile(bak, bakBytes)) {
        Layout restored = ParseLayoutBytes(bakBytes, L"layout.tsv.bak");
        if (!restored.entries.empty() || !restored.groups.empty() || restored.grid.valid) {
            log::Warnf(L"已从备份恢复布局 {} 项 <- {}", restored.entries.size(), bak);
            if (!WriteWholeFileAtomic(path, bakBytes)) {
                log::Errorf(L"备份恢复成功但写回主文件失败: {}", path);
            }
            return restored;
        }
        log::Warnf(L"备份 {} 也是空的，按默认排布继续", bak);
    }

    return Layout{};
}

bool SaveLayout(const Layout& layout, Backup backup) {
    tf::paths::EnsureDir(tf::paths::DataDir());

    std::wstring text =
        L"# Tilefold layout v6\n"
        L"# kind<TAB>group<TAB>item<TAB>col<TAB>row<TAB>spanCols<TAB>spanRows\n"
        L"# 格子坐标，不是像素：换分辨率/换显示器都不会跑出屏幕\n"
        L"# cols=0 表示自动：格子边长对齐原生图标间距，1×1 部件跟原生图标一样大\n"
        L"# grid 行记住上次实际生效的网格（列数/pitch×100/工作区宽高 DIP），\n"
        L"# 下次启动按它还原，免得每次重挑列数把排布挤乱\n";
    text += std::format(L"cols\t{}\t\t0\t0\t0\t0\n", layout.cols);
    if (layout.grid.valid) {
        text += std::format(L"grid\t{}\t{}\t{}\t{}\t0\t0\n", layout.grid.cols,
                            static_cast<int>(std::lround(layout.grid.pitch * 100.0f)),
                            layout.grid.workW, layout.grid.workH);
    }
    for (const auto& m : layout.monitors) {
        text += std::format(L"monitor\t{}\t{}\t{}\t{}\t{}\t0\n", m.monitor, m.grid.cols,
                            static_cast<int>(std::lround(m.grid.pitch * 100.0f)), m.grid.workW, m.grid.workH);
    }
    for (const GroupState& g : layout.groups) {
        text += std::format(L"group\t{}\t\t0\t0\t0\t0\n", g.name);
        std::vector<GroupItem> items = g.items;
        std::stable_sort(items.begin(), items.end(), [](const GroupItem& a, const GroupItem& b) {
            return a.order < b.order;
        });
        for (const GroupItem& it : items) {
            if (it.path.empty()) continue;
            text += std::format(L"gitem\t{}\t{}\t{}\t0\t1\t1\n", g.name, it.path, it.order);
        }
    }
    for (const Entry& e : layout.entries) {
        text += std::format(L"{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\n", e.kind, e.group, e.item, e.col, e.row,
                            e.spanCols, e.spanRows, e.monitor);
    }

    if (backup == Backup::Rotate) {
        SnapshotOncePerSession(LayoutPath());
        RotateBackup(LayoutPath());
    }

    const bool ok = WriteWholeFileAtomic(LayoutPath(), ToUtf8(text));
    if (ok) {
        size_t itemCount = 0;
        for (const auto& g : layout.groups) itemCount += g.items.size();
        log::Infof(L"布局已保存 {} 项，{} 个虚拟分组，{} 个分组条目", layout.entries.size(), layout.groups.size(), itemCount);
    } else {
        log::Errorf(L"布局保存失败（原文件未动）: {}", LayoutPath());
    }
    return ok;
}

const Entry* FindEntry(const Layout& layout, std::wstring_view kind, std::wstring_view group,
                       std::wstring_view item) {
    for (const Entry& e : layout.entries) {
        if (e.kind == kind && e.group == group && e.item == item) return &e;
    }
    return nullptr;
}

void SetEntry(Layout& layout, Entry entry) {
    for (Entry& e : layout.entries) {
        if (e.kind == entry.kind && e.group == entry.group && e.item == entry.item) {
            e = std::move(entry);
            return;
        }
    }
    layout.entries.push_back(std::move(entry));
}

bool RemoveEntry(Layout& layout, std::wstring_view kind, std::wstring_view group,
                 std::wstring_view item) {
    const auto it = std::find_if(layout.entries.begin(), layout.entries.end(), [&](const Entry& e) {
        return e.kind == kind && e.group == group && e.item == item;
    });
    if (it == layout.entries.end()) return false;
    layout.entries.erase(it);
    return true;
}

bool IsExtracted(const Layout& layout, std::wstring_view group, std::wstring_view item) {
    return FindEntry(layout, L"icon", group, item) != nullptr;
}

const GroupState* FindGroup(const Layout& layout, std::wstring_view name) {
    for (const GroupState& g : layout.groups) {
        if (g.name == name) return &g;
    }
    return nullptr;
}

GroupState* FindGroup(Layout& layout, std::wstring_view name) {
    for (GroupState& g : layout.groups) {
        if (g.name == name) return &g;
    }
    return nullptr;
}

GroupState& EnsureGroup(Layout& layout, std::wstring_view name) {
    if (GroupState* existing = FindGroup(layout, name)) return *existing;
    GroupState group;
    group.name = std::wstring(name);
    layout.groups.push_back(std::move(group));
    return layout.groups.back();
}

bool RemoveGroup(Layout& layout, std::wstring_view name) {
    // 保留名下面挂着的是此电脑/回收站（kind="shell"）和未分组桌面条目（kind="desktop"），
    // 跟着分组一起删掉就是数据丢失，宁可拒绝。
    if (IsReservedGroupName(name)) {
        log::Warnf(L"拒绝删除保留分组名 [{}]，那里存着系统图标和未分组桌面条目的布局", name);
        return false;
    }
    bool removed = false;
    const auto it = std::find_if(layout.groups.begin(), layout.groups.end(),
                                [&](const GroupState& g) { return g.name == name; });
    if (it != layout.groups.end()) {
        layout.groups.erase(it);
        removed = true;
    }
    layout.entries.erase(std::remove_if(layout.entries.begin(), layout.entries.end(),
                                       [&](const Entry& e) { return e.group == name; }),
                        layout.entries.end());
    return removed;
}

const GroupState* GroupForPath(const Layout& layout, std::wstring_view path) {
    if (path.empty()) return nullptr;
    for (const GroupState& g : layout.groups) {
        for (const GroupItem& item : g.items) {
            if (item.path == path) return &g;
        }
    }
    return nullptr;
}

const GroupItem* FindGroupItem(const Layout& layout, std::wstring_view group,
                               std::wstring_view path) {
    const GroupState* g = FindGroup(layout, group);
    if (g == nullptr) return nullptr;
    for (const GroupItem& item : g->items) {
        if (item.path == path) return &item;
    }
    return nullptr;
}

bool AddGroupItem(Layout& layout, std::wstring_view group, std::wstring_view path,
                  int order) {
    GroupState* g = FindGroup(layout, group);
    if (g == nullptr || path.empty()) return false;
    if (FindGroupItem(layout, group, path) != nullptr) return true;
    // 全局唯一：一个路径只能属于一个分组。MoveGroupItem 走的就是“先清后加”这条路，
    // 这里再兜一次底（解析旧文件、手改 layout.tsv、以后新调用方都不会造出两边都有记录）：
    // 同一图标出现在两个文件夹里时，另一条永远是幽灵——点不开，也删不掉。
    bool takenElsewhere = false;
    for (GroupState& other : layout.groups) {
        if (&other == g) continue;
        const std::size_t before = other.items.size();
        other.items.erase(std::remove_if(other.items.begin(), other.items.end(),
                                         [&](const GroupItem& item) { return item.path == path; }),
                          other.items.end());
        if (other.items.size() != before) takenElsewhere = true;
    }
    if (takenElsewhere) {
        // 那个分组已经不再拥有它了，留在那里的图标位置记录也没了意义（不然会重建出一个
        // 指向非成员的格子），跟 MoveGroupItem 的清理保持一致
        layout.entries.erase(std::remove_if(layout.entries.begin(), layout.entries.end(),
                                            [&](const Entry& e) {
                                                return e.kind == L"icon" && e.item == path &&
                                                       e.group != std::wstring(group);
                                            }),
                             layout.entries.end());
    }
    if (order < 0) {
        order = 0;
        for (const GroupItem& item : g->items) order = (item.order >= order) ? item.order + 1 : order;
    }
    g->items.push_back(GroupItem{std::wstring(path), order});
    return true;
}

bool RemoveGroupItem(Layout& layout, std::wstring_view group, std::wstring_view path) {
    GroupState* g = FindGroup(layout, group);
    if (g == nullptr) return false;
    const auto it = std::find_if(g->items.begin(), g->items.end(),
                                [&](const GroupItem& item) { return item.path == path; });
    if (it == g->items.end()) return false;
    g->items.erase(it);
    return true;
}

bool MoveGroupItem(Layout& layout, std::wstring_view path, std::wstring_view toGroup) {
    if (path.empty() || FindGroup(layout, toGroup) == nullptr) return false;
    for (GroupState& g : layout.groups) {
        g.items.erase(std::remove_if(g.items.begin(), g.items.end(),
                                     [&](const GroupItem& item) { return item.path == path; }),
                      g.items.end());
    }
    layout.entries.erase(std::remove_if(layout.entries.begin(), layout.entries.end(),
                                       [&](const Entry& e) {
                                           return e.kind == L"icon" && e.item == path;
                                       }),
                        layout.entries.end());
    return AddGroupItem(layout, toGroup, path);
}

void SetGroupItemOrder(Layout& layout, std::wstring_view group,
                       const std::vector<std::wstring>& paths) {
    GroupState* g = FindGroup(layout, group);
    if (g == nullptr) return;
    std::vector<GroupItem> reordered;
    reordered.reserve(g->items.size());
    for (const std::wstring& path : paths) {
        const auto it = std::find_if(g->items.begin(), g->items.end(),
                                    [&](const GroupItem& item) { return item.path == path; });
        if (it == g->items.end()) continue;
        reordered.push_back(*it);
        g->items.erase(it);
    }
    for (const GroupItem& item : g->items) reordered.push_back(item);
    for (std::size_t i = 0; i < reordered.size(); ++i) reordered[i].order = static_cast<int>(i);
    g->items = std::move(reordered);
}

}  // namespace tf::cfg
