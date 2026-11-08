#include "Layout.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <vector>

#include "core/Grid.h"
#include "core/Log.h"
#include "core/Paths.h"

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
            if (line.find(L"layout v2") != std::wstring::npos) {
                fileVersion = 2;
            } else if (line.find(L"layout v5") != std::wstring::npos) {
                fileVersion = 5;
            } else if (line.find(L"layout v4") != std::wstring::npos) {
                fileVersion = 4;
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
        if (!main.entries.empty() || main.grid.valid) return main;
        log::Warnf(L"布局文件里一条记录都没有，可能是上次写到一半被打断，尝试从备份恢复");
    } else {
        log::Infof(L"没有布局文件，使用默认排布: {}", path);
    }

    // 主文件缺失或解析成空：退回 .bak，并把内容写回主文件，
    // 这样后面的保存轮换（main -> .bak）不会把这份好数据又盖掉
    std::string bakBytes;
    if (ReadWholeFile(bak, bakBytes)) {
        Layout restored = ParseLayoutBytes(bakBytes, L"layout.tsv.bak");
        if (!restored.entries.empty() || restored.grid.valid) {
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

void SaveLayout(const Layout& layout) {
    tf::paths::EnsureDir(tf::paths::DataDir());

    std::wstring text =
        L"# Tilefold layout v5\n"
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
    for (const Entry& e : layout.entries) {
        text += std::format(L"{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\n", e.kind, e.group, e.item, e.col, e.row,
                            e.spanCols, e.spanRows, e.monitor);
    }

    SnapshotOncePerSession(LayoutPath());
    RotateBackup(LayoutPath());

    if (WriteWholeFileAtomic(LayoutPath(), ToUtf8(text))) {
        log::Infof(L"布局已保存 {} 项", layout.entries.size());
    } else {
        log::Errorf(L"布局保存失败（原文件未动）: {}", LayoutPath());
    }
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

std::vector<std::pair<std::wstring, int>> LoadOrder(const Layout& layout,
                                                   std::wstring_view group) {
    std::vector<std::pair<std::wstring, int>> out;
    for (const Entry& e : layout.entries) {
        if (e.kind != L"order" || e.group != group) continue;
        out.emplace_back(e.item, e.col);
    }
    return out;
}

void SetOrder(Layout& layout, std::wstring_view group,
              const std::vector<std::wstring>& names) {
    layout.entries.erase(std::remove_if(layout.entries.begin(), layout.entries.end(),
                                       [&](const Entry& e) {
                                           return e.kind == L"order" && e.group == group;
                                       }),
                        layout.entries.end());

    // 写在最后，保持 layout.tsv 里“先位置、后顺序”的阅读顺序
    int index = 0;
    for (const std::wstring& name : names) {
        Entry e;
        e.kind = L"order";
        e.group = std::wstring(group);
        e.item = name;
        e.col = index++;
        e.row = 0;
        e.spanCols = 1;
        e.spanRows = 1;
        layout.entries.push_back(std::move(e));
    }
}

}  // namespace tf::cfg
