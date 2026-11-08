#pragma once

#include <windows.h>

#include <optional>
#include <string>

namespace tf::ui {

// 极简的单行输入框：标题 + 提示 + 一行 EDIT + 确定/取消。
//
// 项目里没有 .rc 对话框模板（也不打算为了一个输入框引入资源编译），
// 所以这里手搭窗口 + 自己跑一个模态循环：回车=确定，ESC=取消。
// 循环里仍然 DispatchMessage 到其它窗口，所以托盘、看门狗定时器不受影响。
//
// 返回 std::nullopt 表示用户取消（或窗口根本没建起来）。
std::optional<std::wstring> PromptForText(HWND owner, const wchar_t* title, const wchar_t* prompt,
                                          const std::wstring& initial);

}  // namespace tf::ui
