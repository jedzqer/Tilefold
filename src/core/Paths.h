#pragma once

#include <string>
#include <vector>

namespace tf::paths {

// 运行期数据都在单个 data\ 目录里：
//
//   data\box\<分组>\...     大文件夹的分组目录
//   data\layout.tsv          布局
//   data\logs\tilefold.log   日志
//
// 开发树里这个目录就是项目根下的 data\；便携 ZIP 是解压目录旁边的 data\；
// 安装版（MSI）落在 %LOCALAPPDATA%\Tilefold\data\，不往 %APPDATA% 里塞东西，
// 也不会被卸载程序连带删掉。

// 数据根目录的“宿主”：
//   1. 环境变量 TF_HOME（最高优先级）
//   2. 从 exe 目录往上找带 CMakeLists.txt 的目录（开发树
//      build\Debug\Tilefold.exe -> <项目>）
//   3. exe 在安装位置（MSI 写的 HKCU\Software\Tilefold\Installer\InstallLocation 下面，
//      或 Program Files / %LOCALAPPDATA%\Programs）
//      -> %LOCALAPPDATA%\Tilefold（卸载/升级会删安装目录，data\ 不能留在里面）
//   4. 其他位置（便携 ZIP）-> exe 所在目录，数据跟着目录走
std::wstring AppRoot();

// 当前进程 exe 的完整路径（注册桌面右键菜单时要拿它写命令行）
std::wstring ExePath();

std::wstring DataDir();
std::wstring LayoutPath();
std::wstring LogDir();

// 桌面目录列表（用户桌面 + 公共桌面）。
// 首次调用解析一次后缓存：SHGetKnownFolderPath 每次都要走 shell/注册表，
// 而看门狗每 2.5s 就要扫一遍桌面，不能每轮都重新问系统。
const std::vector<std::wstring>& DesktopDirs();

// 递归建目录（SHCreateDirectoryExW）；已存在也算成功
bool EnsureDir(const std::wstring& dir);

}  // namespace tf::paths
