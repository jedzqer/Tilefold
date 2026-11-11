# Tilefold

Windows 11 桌面美化工具：把桌面变成手机那样的**格子布局**，再用**半透明的大文件夹**收纳应用。

它不替换 explorer.exe，也不接管鼠标——但原生桌面图标层（`SHELLDLL_DefView`）会被彻底隐藏，桌面内容改由 Tilefold 全面接管显示与交互（见「已知限制 / 还没做」里的说明）。任务栏、Win+D、把文件拖到桌面这些都还是原生行为。

---

## 现在能做什么

| 功能 | 说明 |
| --- | --- |
| 手机式网格 | 格子边长固定对齐 Windows 原生图标间距（约 76 DIP），列数不填就按屏幕宽度自动算 |
| 大文件夹 | `layout.tsv` 里的虚拟分组 = 桌面上一个半透明文件夹 |
| 直接启动 | 文件夹里前 N-1 个图标**点一下就启动**，不用先打开文件夹 |
| 「更多」格 | 装不下时最后一格不是静态 `⋯`，而是 3 个缩略图缩到 0.62 倍、±9° 错位叠放 + 剩余数量角标 |
| 拖动吸附 | 按住文件夹空白处/标题拖动，松手自动吸附到格子 |
| 右键改尺寸 | `大小` 子菜单：`宽度（1..6 列）` / `高度（1..6 行）` 两个子菜单，当前值打勾，另有「恢复默认 3×3」和灰显的「当前 W×H」 |
| 拖出图标 | 把图标从文件夹里拖到桌面 → 变成一个 1×1 独立图标（文件仍留在原处） |
| 拖回图标 | 独立图标拖回文件夹 → 收回；拖到另一个文件夹 → 只改逻辑归属，不移动文件 |
| 格子避让 | 自动寻找空位；屏幕已满时可能重叠 |
| 真实图标 | `IShellItemImageFactory` 异步提取 96px 图标，不是 `SHGetFileInfo` 的 32px 放大 |
| 真透明 | DirectComposition 直出，圆角外与壁纸逐像素一致（不是「画了一张假壁纸」） |
| 崩溃恢复 | Explorer 重启后自动重建（`TaskbarCreated` 广播 + 2.5 秒看门狗兜底） |

### 交互一览

| 操作 | 结果 |
| --- | --- |
| 左键点文件夹前 N-1 格 | 启动该应用 |
| 滚轮在文件夹上滚 | 整行翻页，看装不下的那些项；滚到底「更多」格自动消失，鼠标移开自动回顶 |
| 左键点「更多」格 / 空白格子 | 放大成居中面板（点卡片外或 × 关闭） |
| 面板里按住图标拖动 | 在同一文件夹内改顺序（写进 layout.tsv，重启不重置） |
| 按住空白处或标题拖动 | 移动文件夹，松手吸附格子 |
| 右键文件夹 | 尺寸菜单（宽/高/恢复默认）+ `删除文件夹（仅移除分组记录，文件不动）…` |
| 按住图标拖动 | 拖到桌面空白处 = 拖出；拖到别的文件夹 = 改逻辑归属（新进来的排末尾，文件不移动）；原地松手 = 启动 |
| 左键点 1×1 图标 | 启动 |
| 右键 1×1 图标 | `打开` / `放回 <分组> 文件夹` |
| 右键 此电脑 / 回收站 图标 | 资源管理器**原生**右键菜单：`管理` / `属性` / `清空回收站` / `映射网络驱动器`… 全是系统自己的动词，后面再追加一条 `打开` 和（如果适用）`放回 <分组> 文件夹` |
| 右键桌面空白处 | 由 Tilefold 自己弹的菜单（原生图标层已被接管隐藏，Explorer 不再提供背景菜单）：顶部是自有的 `刷新` + 分隔线，下面接上 Windows 原生背景菜单的 `新建收纳文件夹` / 查看 / 排序方式 / 粘贴 / 新建 / 显示设置 / 个性化 / 属性 |
| 右键托盘图标 | 菜单：`打开设置` / `开机自启动`（勾选式）/ `退出` |
| 双击托盘图标 | 打开设置 |

### 动效

| 动作 | 效果 |
| --- | --- |
| 鼠标停在图标上 | 图标放大到 1.1 倍、投影加重，名字保持可见和原位 |
| 按下左键 | 图标从 1.1 倍收到 0.98 倍（弹簧，不是线性） |
| 松开左键 | 弹簧回弹到悬停态，有轻微过冲 |
| 滚轮翻页 | 内容滑动 0.16–0.34s（easeOutCubic）落到新的一行，不再瞬间翻页 |
| 鼠标离开文件夹 | 滑动回顶，同时悬停态回弹 |

窗口没有焦点也能动：`WidgetWindow` 用一个自建的帧循环（`RequestAnimation()` 起 60Hz 定时器，
没人申请下一帧就停）驱动各部件的 `OnAnim(dt)`，静止时一颗时钟都不留。参数集中在
`src/widgets/Motion.h`，改手感只动这一个文件。

---

## 快速开始

### 环境要求

- Windows 11（只在 Win11 上测过）
- Visual Studio 2022 生成工具（MSVC v143，`/std:c++20`）
- CMake ≥ 3.20
- Windows SDK 10.0.26100 或更新

> **不提供 Ninja 支持**：README 里的命令用 VS 生成器。没有 Ninja 的机器直接照抄即可。

### 构建

```bat
cd <项目目录>
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

产物：`build\Debug\Tilefold.exe`（单实例，靠 `Local\Tilefold.SingleInstance` 互斥体保证）。

> 重新构建前先关掉正在运行的实例，否则链接会报 **LNK1168**（exe 被占用）。

### Release 便携包与安装版

在 PowerShell 中运行（需要上面的构建环境）：

```powershell
.\scripts\package.ps1
```

脚本使用独立的 `build-release` 目录，构建 Release 后生成
`build-release\Tilefold-<版本>-windows-<架构>.zip` 便携包和同名 `.msi` 安装程序，并输出两者的 SHA256。
打包需要 WiX Toolset 3；脚本检测到缺失时会弹出 UAC 窗口安装。最终用户无需安装 WiX。
对外显示的名字取自 `TILEFOLD_RELEASE_TAG`（`CMakeLists.txt` 里默认 `-beta`，正式版置空），
所以当前产物是 `Tilefold-1.0.0-beta-windows-x64.zip` / `.msi`；MSI 的 ProductVersion
仍是纯数字 `1.0.0`（WiX 不接受字母）。设置窗标题会显示完整版本号。
便携构建仍支持 CMake 3.20+；生成 per-user MSI 需 CMake 4.3+。
默认 x64；ARM64 需安装对应 MSVC 工具链，再传 `-Architecture ARM64 -BuildDirectory build-release-arm64`。
MSVC 运行库静态链接，使用者无需安装 Visual C++ Redistributable；其余依赖均为 Windows 系统 DLL。
两种包只包含 `Tilefold.exe`、`README.md`、`LICENSE`，不包含开发机的分组、快捷方式、布局或日志。

发布前更新 `CMakeLists.txt` 的项目版本，运行脚本。便携版解压到可写目录后双击 exe；安装版运行 MSI 后，可从当前用户的开始菜单启动。
在目标 Windows 版本上检查托盘、桌面控件和设置，再将 ZIP、安装程序和对应 SHA256 附到发布页。
首次启动会生成 `data\`：便携版就在解压目录里（跟着目录走），MSI 安装版在 `%LOCALAPPDATA%\Tilefold\data\`
（MSI 卸载和升级都会删安装目录，`data\` 放在里面会连带丢掉布局和快捷方式）。升级前退出程序即可。
MSI 按当前用户安装到 LocalAppData，不需要管理员权限；首次启动会打开设置窗，用户可选择是否登录 Windows 时自动启动，之后也能在设置窗或托盘菜单更改。当前产物未签名；发布前仍需在目标 Windows 版本上验收托盘、桌面控件和设置。

### 数据放哪里

程序**不往 `%APPDATA%` 写任何东西**，运行期数据全部在一个 `data\` 目录里。
开发树里是项目根下的 `data\`，便携版是解压目录旁的 `data\`，
安装版（MSI）是 `%LOCALAPPDATA%\Tilefold\data\`：

```text
<项目>\data\
├── layout.tsv           布局 + 虚拟分组 + 分组条目（唯一保存文件）
├── layout.tsv.bak       上一份完好的布局（每次保存前轮换过来）
├── layout-history\      每次启动留一份快照，最近 10 份
└── logs\tilefold.log    运行日志（2 MB 滚动）
```

保存策略是「不会把排布写坏」那一套：

- **原子替换**：先写 `layout.tsv.tmp`、`FlushFileBuffers` 落盘，再 `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)` 改名。
  进程被杀、断电、磁盘满，原文件要么是完整的旧内容，要么是完整的新内容；不会出现写了一半的截断文件。
  （旧实现是 `CREATE_ALWAYS` 直接覆盖原文件，写到一半挂掉就把排布彻底毁了 —— 下次启动只能看到「没有布局文件，使用默认排布」。）
- **轮换备份**：每次覆盖前把现役 `layout.tsv` 复制成 `layout.tsv.bak`（= 上一个已知good 状态）。
- **读回兜底**：启动时主文件读不出来或解析成 0 条，自动退回 `.bak` 并把内容写回主文件。
- **启动快照**：每个进程生命周期只留一份 `layout-history\layout-YYYYMMDD-HHMMSS.tsv`，保留最近 10 份，
  用来救「排布被自己改乱」或「手动弄坏文件」这种事后。

> 真被重置了（例如整个 `data\` 被删或换目录）：`layout-history\` 里那份快照就是最快的复原手段，
> 拷回去重命名成 `layout.tsv` 即可。

**分组和条目都存在 `layout.tsv` 里**；条目保存的是真实桌面文件的绝对路径，Tilefold 不复制、不移动这些文件。

`data\` 整个目录在 `.gitignore` 里：你的分组内容是自己机器上的东西，不该进仓库。
拷走整个项目目录（或单独拷 `data\`）就能带着排布走；便携版删掉解压目录就等于卸载。
安装版卸载后 `%LOCALAPPDATA%\Tilefold` 仍然留着，要彻底清干净得自己删这个目录。

怎么定位这个「项目根」：从 exe 所在目录往上找带 `CMakeLists.txt` 的目录
（所以 `build\Debug\Tilefold.exe` 会把数据写回项目根，清空 `build\` 不会丢数据）；
单独把 exe 拷到别处时找不到 `CMakeLists.txt`，就要看它在哪：
MSI 安装时会写 `HKCU\Software\Tilefold\Installer\InstallLocation`（默认 `%LOCALAPPDATA%\Tilefold 1.0.0-beta`），
exe 在这个目录下、或者落在 Program Files / `%LOCALAPPDATA%\Programs` 里，就算安装版，
数据走 `%LOCALAPPDATA%\Tilefold`（卸载和升级会删安装目录，不能把布局放在里面）；
其他位置当便携版，数据就放在 exe 旁边。
这条推导可以用环境变量覆盖：`TF_HOME`（数据根）。

### 运行

```bat
build\Debug\Tilefold.exe
```

首次启动会给每个分组自动分配格子并写入 `data\layout.tsv`，之后位置就固定了。
算出来的网格（列数、格距、工作区尺寸）也一起存进同一个文件，所以重启后格子边长不会另挑一个值。
想全部重排：删掉那个文件再启动。

### 开机自启动

首次启动的设置窗可勾选 `登录 Windows 时自动启动 Tilefold`；之后也可在设置窗或托盘菜单修改。它写的是**当前用户**的 Run 键（不需要管理员、不建计划任务）：

```text
HKCU\Software\Microsoft\Windows\CurrentVersion\Run
    Tilefold = "C:\...\Tilefold.exe"
```

几个已经处理掉的情况：

- **首次启动不会偷偷加自启**。不勾就是不写，注册表里干干净净。
- **exe 被挪了位置会自己改回来**。每次启动都拿 Run 值里的路径跟 `GetModuleFileNameW()` 比，
  不一致就重写（`Autostart::SyncIfEnabled()`）。不加这段的话，Debug/Release 换着跑、或者把 exe
  复制到 `C:\Tools\` 之后旧登记会指向一个不存在的文件 —— Windows 开机时找不到程序，**静默失败**，
  用户只会看到「开机没自启」，没有任何提示。
- **勾选状态每次弹菜单现查注册表**，不在内存里缓存：任务管理器「启动」页里被手动禁用/删除后，
  菜单显示的状态仍然是对的。
- 想彻底手动清掉：删掉上面那个 `Tilefold` 值即可，程序不依赖它运行。

---

## 网格规则

- 每台显示器独立按本地 DIP 工作区建立网格，格距以约 76 DIP 为基准；显示器原点和窗口位置使用物理屏幕像素，允许负坐标。
- 每屏保存列数、格距和工作区尺寸；尺寸变化在 ±4 DIP 内复用列数，否则按原格距重算列数。
- 跨屏拖动按鼠标所在显示器吸附，控件的尺寸、输入命中和绘制按目标屏缩放换算。
- `layout.tsv` 保存首选显示器和格子坐标。拔屏、缩小工作区时只对临时显示副本夹紧和避让，不覆盖首选位置；接回原屏后可恢复。
- 用户在临时位置明确拖动或改尺寸时，会把该控件当前的位置与目标屏写成新的首选布局。
- 留白按格距计算，文件夹跨度可选宽、高各 1..6 格；当前屏不足以容纳原尺寸时临时缩小。
- 当前屏幕没有足够空格时仍会显示控件，但无法保证不重叠；不会无限向屏幕外排布。

---

## 数据与配置

| 路径 | 内容 |
| --- | --- |
| `<项目>\data\layout.tsv` | 唯一保存文件：每个部件的格子坐标、虚拟分组、分组条目完整路径、上次生效的网格（原子写入） |
| `<项目>\data\layout.tsv.bak` | 上一份完好布局，主文件坏了自动回退到它 |
| `<项目>\data\layout-history\` | 每次启动的布局快照（最近 10 份），排布被弄坏时的救回手段 |
| `<项目>\data\logs\tilefold.log` | 运行日志，2 MB 滚动 |
| `HKCU\...\CurrentVersion\Run\Tilefold` | 开机自启登记（托盘菜单勾选/取消，路径变了下次启动自动改写） |

`layout.tsv` 是 UTF-8 TSV，手改也安全：

```text
# Tilefold layout v6
# kind<TAB>group<TAB>item<TAB>col<TAB>row<TAB>spanCols<TAB>spanRows<TAB>monitor
cols\t0\t\t0\t0\t0\t0
grid\t19\t8084\t1536\t816\t0\t0
monitor\t<显示器接口ID>\t19\t8084\t1536\t816\t0
group\t工作\t\t0\t0\t0\t0
gitem\t工作\tC:\Users\me\Desktop\example-app.lnk\t0\t0\t1\t1
gitem\t工作\tC:\Users\me\Desktop\another-app.lnk\t1\t0\t1\t1
folder\t工作\t\t0\t0\t3\t3\t<显示器接口ID>
icon\t工作\tC:\Users\me\Desktop\example-app.lnk\t6\t0\t1\t1\t<显示器接口ID>
desktop\t__desktop_items__\tC:\Users\me\Desktop\loose.lnk\t3\t0\t1\t1\t<显示器接口ID>
shell\tsystem\tThisPC\t9\t0\t1\t1\t<显示器接口ID>
```

- `group`：虚拟分组定义；空分组也要有这一行，否则重启后分组会消失。
- `gitem`：分组条目，`group` 是所属分组，`item` 是文件完整路径，`col` 是组内排序号。
- `folder`：桌面上的大文件夹部件，`group` 指向虚拟分组；位置/跨度与其他部件相同。
- `icon`：从分组拖出的独立图标，`group` + `item` 指向原条目；文件不会离开原位置。
- `desktop`：未分组的桌面条目；`shell`：此电脑/回收站等系统图标。
- `cols`：0 = 自动列数；非 0 = 手动指定。
- `grid` / `monitor`：保存列数、格距和工作区尺寸，重启时还原网格。
- 当前只读写 v6 格式；旧版（v2–v5）文件不再自动迁移，需要事先转成 v6（分组与条目写入 `layout.tsv`）。
- 显示用布局会按屏幕范围和占位做避让；此临时调整不会自动覆盖已保存的首选位置。

---

## 架构

```text
src/
├── main.cpp                 单实例互斥体 / OleInitialize(STA) / 消息循环
├── core/
│   ├── App.*                装配一切：网格、布局、扫描、widget 生命周期、拖动与重排
│   ├── Grid.*               手机式网格的纯函数几何（Spec / RectForSpan / CellAt / SnapRect）
│   ├── Layout.*             layout.tsv 读写 + Entry 查询（TSV，无第三方依赖）
│   ├── Paths.*               数据目录定位：exe → 项目根（找 CMakeLists.txt）→ data\
│   ├── Dpi.*                Per-Monitor V2 的 DIP ↔ 像素换算
│   ├── Tray.*              通知区域图标 + 右键菜单（打开设置 / 开机自启动 / 退出）
│   ├── Autostart.*         HKCU Run 开机自启：写/删/启动时校正旧路径
│   ├── Ipc.h               控制窗口类名 + `Tilefold.NewBoxFolder` 自定义消息（--new-box 转发用）
│   ├── PromptDialog.*      自建的单行文本输入框（没有 .rc 资源，手搭窗口 + 自跑模态循环）
│   └── Log.*                std::format 风格日志，2 MB 滚动
├── render/
│   ├── GpuDevice.*          D3D11 + DXGI + D2D + DirectComposition + DWrite + WIC 单例
│   ├── Surface.*            每窗口一个 DComp 表面，外部绘制（Begin/End）
│   └── Layer.*              玻璃卡片：圆角填充 + 内描边 + 顶部高光
├── shell/
│   ├── DesktopHost.*        找桌面宿主（Progman / WorkerW）与 SHELLDLL_DefView
│   ├── BoxScan.*            扫描真实桌面（用户桌面 + 公共桌面）
│   ├── GroupFolder.*        分组名消毒（非法字符/DOS 设备名/超长），不再创建真实目录
│   ├── DesktopContextMenu.* 桌面空白处右键菜单那一项：注册表 Install/Remove + `--new-box` 入口
│   ├── ShellMenu.*          命名空间条目的**原生**右键菜单（IContextMenu），此电脑/回收站用
│   ├── IconStore.*          专用 STA 线程异步提取图标，完成即回调重绘
│   └── Launcher.*           ShellExecuteEx + SEE_MASK_INVOKEIDLIST，独立线程（shell 扩展会卡）
└── widgets/
    ├── WidgetWindow.*       通用窗口：桌面 widget 走 child（挂宿主下），放大面板走 popup；DComp 直出、DPI、拖动、点击/右键/移动/松开、帧循环
    ├── Motion.h             动效参数（悬停/按下）集中在这里，改手感只改它
    ├── BigFolderWidget.*    大文件夹：3×3 网格、固定尺寸图标、滚轮翻页、更多格、尺寸菜单
    ├── ExpandedFolder.*     放大面板：居中卡片、面板内拖动排序、滚动条
    ├── SingleIconWidget.*   1×1 独立图标
    └── Paint.*              图标绘制（真实图标 or 哈希色块占位，二者绝不叠画）
```

### 几个关键决定（都是踩坑换来的，别随手改回去）

**1. widget 是桌面宿主的 `WS_CHILD` 子窗口，不是顶层 popup；只有放大面板是 popup。**
历史：一开始是 popup + owner=Progman，平时层级是对的，但 `Win+D`「显示桌面」会把每个**顶层**窗口逐个 `SetWindowPos(HWND_BOTTOM)`（带 `SWP_NOSENDCHANGING`，`WM_WINDOWPOSCHANGING` 拦不到）—— 8 块面板的 z 序被整体反转、全落到 Progman 之下，而 Progman 不透明，于是桌面只剩一块侥幸留在宿主上面的文件夹（用户反馈的「Win+D 后只剩一个文件夹」）。事后硬抬 z 序是不行的：`SetWindowPos(hwnd, progman, …)` 返回成功，`GetWindow` 链却纹丝不动，Progman 会同步抢回最顶（日志会 5 ms 一次无限抽插）。
现在：桌面 widget 建窗时直接 `WS_CHILD` 到 `host_`（Progman / 持有 `SHELLDLL_DefView` 的 WorkerW）。子窗口不参与 Show Desktop，`Win+D` 后 8 块全在原处。两个必须做的配套：
- 建完窗要 `SetWindowPos(hwnd, HWND_TOP, …)`：Progman 会让 `SHELLDLL_DefView` 保持 topmost，新建的子窗口反而落在它下方，被桌面图标视图盖住。
- 透明依旧成立：卡片圆角外仍然透出壁纸，不是黑底（`Surface` 的 `B8G8R8A8 + PREMULTIPLIED` + `WS_EX_NOREDIRECTIONBITMAP` 下 DComp 会把 alpha 合成到父表面）。实测过左边缘剖面：`x=915..945` 是 `(254,197,108) → (223,183,118)` 的连续过渡，没有黑阶。
- `TF_WIDGET_MODE=popup|child` 可强制覆盖模式，只用于对照排查。
放大面板（`panelMode_`）永远是顶层 popup：它是用户主动打开的，要压在正在用的程序之上、还得能收 ESC。
验收判据：`Win+D` 后 8 块 widget 矩形相对纯壁纸基准的 `meanDiff` 必须是 42–59，而不是 6–12。

**2. 每个大文件夹一个独立小窗口，不做全屏透明覆盖层。**
全屏覆盖层即使 `WM_NCHITTEST` 返回 `HTTRANSPARENT` 也没用：OLE 拖放的目标判定走 `WindowFromPoint`，不读 `NCHITTEST`，从资源管理器拖文件到桌面会被整个吃掉。窗口之外全是原生行为。

**3. 平时的 Z 序靠子窗口身份，不再靠 `SetWindowPos(HWND_BOTTOM→host)`。**
子窗口天然在宿主内部排序，不会再盖到终端等普通窗口之上，也不再需要 `WM_WINDOWPOSCHANGING` 去强插 `hwndInsertAfter`。保留的经验值：`SetWindowPos(h, progman, …)` 的语义是「插到 Progman 紧邻之上」（实测 target idx 206 / progman idx 207），不是「插到最顶」；而 `HWND_TOP` 在 Progman 这一层只保证「兄弟中最顶」。

**4. 文件夹里的图标尺寸恒定（`paint::kNativeIconDip = 46 DIP`），不随面板变大。**
面板大小只决定**能摆几个**图标：3×3 → 9 格（8 个直接点 + 1 个更多），4×4 → 16 格（15 + 1）。

**5. 永远不要在 widget 自己的回调里销毁它。**
拖动/改尺寸会触发整层重排，`RelayoutAll()` 里 `folders_.clear()` 会把正在执行回调的那个对象析构掉（use-after-free，实测崩在 `ucrtbased.dll` 0x80000003）。所以所有重排都走 `PostMessageW(kRelayoutMsg)` 排到消息循环里做。

**6. 启动应用走 `ShellExecuteEx` + PIDL。**
`SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC | SEE_MASK_INVOKEIDLIST`，exe / lnk / UWP / `shell:AppsFolder` 一条路径通吃；放独立线程是因为 shell 扩展可能卡住 UI 线程。

**7. 图标用 `IShellItemImageFactory::GetImage`。**
`SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK` 请求 96px；`SHGetFileInfo` 只有 32px、放大必糊，也拿不到 UWP 的真实图标。

**8. 滚轮只能走 `WH_MOUSE_LL` 低级钩子，`WM_MOUSEWHEEL` 收不到。**
widget 带 `WS_EX_NOACTIVATE`，永远没焦点，系统的悬停滚动把 `WM_MOUSEWHEEL` 发给**有焦点**的窗口（终端就自己吃掉了）。所以 App 装 `WH_MOUSE_LL`，按 `MSLLHOOKSTRUCT->pt` 找到对应的文件夹 widget 再转发。

**9. 文件夹内的图标顺序记在 `layout.tsv` 的 `gitem` 行的 `col` 字段里。**
组内顺序不再依赖磁盘：面板里拖动排序后把整组条目按新顺序重写进 `gitem`（`col` = 0..N-1），启动时按 `col` stable_sort 还原；新收进组的条目追加到末尾。`CellFreeInLayout` / `NormalizeLayout` 仍然必须只处理 `folder`/`icon` 条目，`group`/`gitem` 不占格子。

**10. 屏幕位置使用物理像素，控件内部使用本地 DIP。**
桌面 child 窗口可能继承 Explorer 宿主的 DPI，不能仅用 `GetDpiForWindow` 判断其所在屏幕缩放。
桌面控件按物理窗口矩形所在显示器取缩放，顶层面板与设置窗口按各自窗口 DPI 处理。
转换屏幕坐标前保留显示器物理原点，再对本地偏移缩放，避免跨屏和负原点被统一缩放。
所有用 `SetCursorPos` / `mouse_event` 的测试脚本仍需先启用 DPI awareness，避免坐标虚拟化。

**11. 此电脑 / 回收站的右键菜单是 explorer 自己生成的，我们只转发消息。**
`SHParseDisplayName` → `SHBindToParent` → `GetUIObjectOf(IID_IContextMenu)` → `QueryContextMenu`，
所以「管理」「属性」「清空回收站」「映射网络驱动器」这些系统动词全在，以后 Windows 加新项也自动跟上。
两个坑：
- 不能用 `CMF_EXTENDEDVERBS`：那会把 Shift+右键的「删除（不进回收站）」也摊出来。
- 必须把 `WM_INITMENUPOPUP` / `WM_DRAWITEM` / `WM_MEASUREITEM` / `WM_MENUCHAR` 转发给
  `IContextMenu3::HandleMenuMsg2`（个体扩展用 `IContextMenu2::HandleMenuMsg`），否则自绘型的
  第三方扩展画不出来。所以弹菜单时临时建了一个 `Tilefold.MenuHost` 窗口当 owner 专干这件事。

**12. 桌面右键「新建收纳文件夹」= 注册表一项 + `--new-box` 转发，不另开进程。**
写 `HKCU\Software\Classes\DesktopBackground\Shell\Tilefold.NewBox`（`Position=Top`），不需要管理员
权限；启动时写、退出时摘，免得目录被拷走后留一个死项。点菜单拉起的是 `tilefold.exe --new-box`：
这个分支必须在**单实例互斥体之前**分流（主实例通常正在跑），主实例在就把
`Tilefold.NewBoxFolder` 消息投给它的控制窗口，不在就自己弹输入框写一条虚拟分组。
主实例收到消息后写 `layout.tsv` + 重扫，新分组会自动拿到一个空格子（`App::BuildDesktopLayer` 里那段）。

**13. 「删除文件夹」= 只删分组记录，不动文件。**
右键大文件夹 → `删除文件夹（仅移除分组记录，文件不动）…`，确认后从 `layout.tsv` 里删掉该分组
（`group` + `gitem` + 文件夹条目 + 从它拖出去的独立图标条目），文件仍留在桌面上，重扫后变成
1×1 图标。实现上同样要投递 `kDeleteGroupMsg = WM_APP + 5` 回消息循环再干活：这段代码会析构
发起右键的那块 widget，在它自己的窗口过程栈里做就是踩自己的栈。

**14. 桌面上的快捷方式按虚拟分组归类，不是物理搬进 `data\box`。**
`App::BuildDesktopLayer` 先按 `layout.tsv` 重建分组，再 `ScanDesktopItems()` 扫一遍桌面
（用户桌面 + 公共桌面）：已属于某个分组的不再平铺，其余的写成 `kind=desktop` 的 1×1 条目
（`FindFreeCell` 找空格子）。扫描结果的去重与清理必须跟着扫描和虚拟分组表走：
已删除的快捷方式对应的 `desktop` 条目和 `gitem` 要一起剔除，否则会留下指向空路径的死格子。
`RescanGroups` 和定时刷新也走同一条路径，别只在启动时做一次。

---

## 开发调试

仓库里**不带**调试脚本。抓屏、像素比对、模拟鼠标那批 PowerShell 工具是开发过程中本地用的一次性的东西，
路径、坐标、壁纸假设全是本机的，跟程序本体无关，所以没有一起发布。

程序本体留了几个环境变量开关，只影响行为、不写盘，排查时够用：

| 变量 | 作用 |
| --- | --- |
| `TF_HOME` | 覆盖数据根目录（默认按 exe 位置推导，见上文） |
| `TF_WIDGET_MODE=popup\|child` | 强制桌面 widget 走顶层 popup / 宿主 child，用于对照两种层级的差异 |
| `TF_WIDGET_POS=x,y` | 把第一块 widget 钉在指定 DIP 坐标，截图对齐/复现位置问题用 |
| `TF_NO_LAUNCH=1` | 只记日志、不真启动应用（不然一点就把程序拉起来，桌面被抢走，动效那几帧拍不到） |

日志在 `<项目>\data\logs\tilefold.log`，超过 2 MB 自动滚动成 `tilefold.1.log`。

---

## 动效实现要点

- 帧循环在 `WidgetWindow`：`RequestAnimation()` 开 60 Hz 定时器，`WM_TIMER` 里算 `dt` 并逐个 `OnAnim(dt)`，没人再申请下一帧就 `KillTimer`，静止时零开销。
- `motion::Approach`（指数逼进，悬停）与 `motion::SpringStep`（弹簧 + 速度积分，按下/回弹）在 `Motion.h`；缩放由 `motion::IconScale(hover, press)` 合成：`1 + 0.10*hover - 0.12*press`，所以按下时相对悬停态可见地缩了 11%。
- 悬停时标签保持可见和原位；翻页、更多格变形时仍保留文字过渡。
- 滚动翻页：`scrollFrom_` → `firstItem_`，`scrollT_` 在 `0.16–0.34s` 内走完（距离越远给的时间越长，`easeOutCubic`）；绘制时 `frac = ScrollPos() - floor(ScrollPos())` 变成整格位移，多画一行让新行滑进来，格子结束到卡片内（不然滑动时图标会溢出玻璃圆角）。
- 动效状态一律不写回 `firstItem_` 这种持久字段，`ClickTargetAt`/`HitCell` 用的是取整后的 `ViewBase()`，所以动画中途点击也不会点错格子。实测一页翻页：`pos = 0 → 1.23 → 1.83 → 2.54 → 2.73 → 2.96 → 3.00`（≈168 ms、7 帧），不是一步跳过去。

---

## 已知限制 / 还没做

- ~~**模块展开面板**（点文件夹后铺开的完整面板）还没实现~~：已实现（`src/widgets/ExpandedFolder.cpp`），点空白格子或「更多」格打开，面板内可拖动排序。
- **原生桌面图标由 Tilefold 全面接管**（没有开关）：启动时就写 `HKCU\...\Explorer\Advanced\HideIcons=1`，再对每个 `SHELLDLL_DefView` 调一次 `ShowWindow(SW_HIDE)`，所以原生图标和 Explorer 的桌面背景右键菜单在运行期间从不出现。代价是右键菜单只能由我们弹（`shell/DesktopContextMenu.cpp`），系统的「显示桌面图标」开关在 Tilefold 跑着时也不起作用。退出时会把 `HideIcons` 写回 `0` 并恢复 `DefView`，原生桌面原样回来：接管只在运行期间生效，不会把用户的桌面留在空状态。
- **模糊背景**是直接画半透明白玻璃，没有对壁纸做真高斯模糊。
- **动态壁纸**（Wallpaper Engine / Spotlight）没有特殊处理。
- ~~**拖放**只支持程序内部拖动，还不接受从资源管理器拖入文件~~：内部拖动已覆盖三种图标（组内图标 / 1×1 桌面图标 / 此电脑回收站），从资源管理器拖入仍未做。
- 不提供列数 / 网格间距的 UI（只能改 `layout.tsv` 的 `cols`）。
- 已实现独立每屏网格和断屏临时回退；真实多显示器混合 DPI、物理热插拔仍需硬件验收。
- 提供 Release 便携 ZIP 和当前用户 MSI 安装程序；尚无代码签名。

---

## 开发约定

- C++20 / `/W4` / `/permissive-` / `/utf-8`，**不引第三方库**（布局用 TSV 而不是 JSON 就是为了这个）。
- 加新源文件要同步改 `CMakeLists.txt`。
- 头文件尽量用同目录相对包含（`#include "Log.h"`），否则 clangd 拿不到源码根目录（`-Isrc`）时会连带报一堆假的 `std::` 错误，把真问题埋掉。
- 改完跑一遍：`cmake --build build --config Debug`，然后看 `data\logs\tilefold.log`（日志里有数据目录、网格、坐标、图标数量）；想确认像素真的画出来了就抓图对比。

---

## 仓库里有什么

| 路径 | 是否入库 | 说明 |
| --- | --- | --- |
| `src/` | ✅ | 全部源码 |
| `CMakeLists.txt` | ✅ | 唯一的构建入口 |
| `.clangd` | ✅ | 编辑器侧标准/宏设置（不含任何绝对路径） |
| `README.md` / `LICENSE` | ✅ | |
| `build/` | ❌ | CMake 产物、中间文件、截图 |
| `data/` | ❌ | 你的分组目录、布局、日志 —— 每个人不一样，不该进仓库 |
| `compile_commands.json` | ❌ | 由 CMake / clangd 生成 |
| `.cache/`、`.pi/` | ❌ | clangd 索引、本地工具缓存 |

请 clone 完整仓库：默认构建包含 `tests/`，便携打包还需要 `scripts/`、`README.md` 和 `LICENSE`。
`data/`、`build/` 都是第一次构建/启动时自动生成的。

## 便携性

- 源码里**没有任何绝对路径**：数据目录按 exe 位置推导（`src/core/Paths.cpp`），
  `.clangd` 也不写 `C:/Users/<你>/...`。
- 不依赖任何第三方库，只用 Windows SDK 自带的 D3D11 / D2D / DirectComposition / WIC / Shell。
- 不往注册表塞东西（除了你主动勾选的开机自启那一项），不装服务、不写系统目录。

## License

MIT，见 [LICENSE](LICENSE)。
