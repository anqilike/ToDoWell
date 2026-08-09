# ToDoWell

桌面极简待办事项管理工具。原生 C++17 + Direct2D 编写，单文件免安装，常驻桌面右下角。

项目主页: <https://anqilike.github.io/ToDoWell/>

## 简介

ToDoWell 由 Python 3.8 + tkinter 的 1.5.0 版本完全用 C++17 与 Direct2D 重写（当前版本 2.5.5）。窗口常驻桌面右下角，支持项目分类管理、待办任务增删改、中文输入法候选框跟随、多套归位/关闭动画、开机自启与 JSON 持久化。无任何第三方运行时依赖。

## 功能特性

- **项目分类管理**：新建、双击改名、一键删除，8 色编号徽标循环标识
- **待办任务**：点击圆圈完成（淡出动画后移除）、单击文字编辑、回车连续新增
- **中文输入法**：组合窗口与候选列表跟随光标（Win7 IMM32 与 Win11 TSF 微软拼音均正常），拼音与候选弹层由 Direct2D 自绘，与界面风格完全一致
- **自绘 UI**：Direct2D 硬件加速渲染，微软雅黑 + Segoe UI Symbol，不使用标准控件
- **动画**：5 种归位动画、关闭淡出、底部 + / 齿轮与删除叉号悬停旋转、任务圆圈悬停高亮渐变
- **设置面板**：开机自启（HKCU 注册表）、标题前缀（最多 5 个汉字）、归位动画切换、关于面板
- **数据持久化**：`todos.json` / `config.json` 每次操作即时写入，零第三方依赖
- **平台兼容**：Windows 7（SP1 + Platform Update）与 Windows 11，支持高 DPI

## 构建

需要 Visual Studio（含 MSVC 与 Windows SDK），直接运行：

```bat
build.bat
```

输出 `ToDoWell.exe`（约 395 KB，`/MT` 静态链接，不依赖 VCRedist）。

## 运行

双击 `ToDoWell.exe` 即可使用。数据文件 `todos.json`、`config.json` 生成于 exe 同目录，删除后下次启动自动重建为默认状态。

## 版本历史

- **2.5.5**：修复图标旋转中心（按字形墨迹中心旋转）；删除叉号悬停旋转（仅当前项）；任务圆圈悬停高亮修复
- **2.5.0**：输入法处理重写——候选框在 Win7/Win11 均跟随光标；输入控件与界面融合、消除双层文字；设置/关于遮罩淡出修复；底部图标悬停旋转动画
- **2.0.0**：C++/Direct2D 重写完成（替代 Python/tkinter 1.5.0）
- **1.5.0**：Python/tkinter 版本（源码已归档至 `前期python版本/`）

## 项目结构

| 文件 | 职责 |
| --- | --- |
| `main.cpp` | 程序入口、窗口创建、消息循环、DPI、多媒体定时器 |
| `app.cpp` / `app.h` | App 类：UI 构建、渲染、布局、交互、动画、设置/关于、IME 轮询与候选自绘 |
| `gfx.cpp` / `gfx.h` | Direct2D / DirectWrite 封装、调色板、字体 |
| `storage.cpp` / `storage.h` | JSON 数据读写、开机自启、UTF-8 转换 |
| `json.cpp` / `json.h` | 自实现 JSON 解析与序列化 |
| `build.bat` | 一键编译脚本（rc + cl + link） |
| `ToDoWell_2.0.0_doc.docx` | 技术规格与实现文档（含更新记录） |

## 许可

仓库暂未指定开源许可证，作者：天才的5014。
