# ClipStash

把剪贴板里的图片直接变成 PNG 文件路径。Windows 上的超轻量剪贴板工具：单 exe、无安装包、无第三方 DLL。

> 仅支持 **Windows 10/11（x64 / x86）**。Linux/macOS 需要独立的剪贴板与编码实现，当前代码不能直接打包成这两个平台。

## 为什么用它

很多聊天窗口、工单系统、网页上传框只接受“文件”，不认剪贴板里的图片。ClipStash 在后台监听剪贴板，一旦发现图片就自动存成 PNG，并把剪贴板替换成一个文件路径。你直接 `Ctrl+V` 就是发文件。

适合这些场景：

- 截图后直接发到微信/QQ/钉钉等聊天窗口，而不是被压成图片消息
- 往只支持文件上传的网页/工单/表单里粘贴截图
- 把剪贴板里的图片快速落盘成 PNG，方便归档、贴图床或继续编辑
- 不想装带托盘、常驻几十 MB 的截图/剪贴板套件

## 特点

- **超低占用**：x64 约 174 KB，x86 约 145 KB；空闲私有内存约 1.1 MB，空闲 CPU 0%
- **完全隐藏**：消息专用窗口，无任务栏、无托盘、无 Alt+Tab
- **无安装包**：单个 exe，MSVC 静态 CRT，不依赖 VC 运行库或第三方 DLL
- **开机自启**：写当前用户的 `HKCU\...\Run`，不需要管理员权限
- **事件驱动**：阻塞在消息循环，不轮询、无定时器
- **可诊断**：错误默认写入 `%LOCALAPPDATA%\ClipStash\clipstash.log`

## 工作方式

```text
复制图片
   ↓
ClipStash 读取 CF_DIBV5 / CF_DIB / CF_BITMAP
   ↓
编码为 PNG，存到 %TEMP%\ClipStash
   ↓
剪贴板替换为 CF_HDROP（文件路径）
   ↓
到资源管理器 / 聊天窗口 / 上传框里 Ctrl+V
```

转换后的 PNG 文件在：

```text
%TEMP%\ClipStash\
```

## 安装

### 方式一：下载发行版（推荐）

1. 打开 [Releases](https://github.com/fengctm/ClipStash/releases)。
2. 下载 `ClipStash-v1.0.0-win-x64.zip`（大多数电脑）或 `win-x86.zip`（32 位系统）。
3. 解压到任意目录。
4. 双击 `install.bat`。

脚本会：

- 把 `ClipStash.exe` 复制到 `%LOCALAPPDATA%\ClipStash\`
- 写入 HKCU Run 开机自启
- 立即启动后台实例

不需要管理员权限。

### 方式二：从源码构建

需要 Visual Studio Build Tools（MSVC）和 Windows SDK。

```bat
build.bat        :: x64
build-x86.bat    :: x86
install.bat      :: 安装当前构建
```

`build.bat` / `build-x86.bat` 目前写死了本机 VS 2026 BuildTools 路径；换机器时改一下 `vcvars` 路径。

## 使用

复制图片的方式都行：

- `Win+Shift+S` 截图
- `PrintScreen`
- 浏览器里右键“复制图片”
- 聊天软件里复制图片

然后到接受文件的窗口按 `Ctrl+V`。

### 命令

| 命令 | 作用 |
|---|---|
| `ClipStash.exe` | 启动后台监听 |
| `ClipStash.exe --install` | 注册开机自启并启动 |
| `ClipStash.exe --uninstall` | 停止并删除自启 |
| `ClipStash.exe --quit` | 只停止当前实例 |
| `ClipStash.exe --selftest` | 编码测试图，验证 WIC/PNG |
| `ClipStash.exe --verbose` | 额外记录成功转换和诊断信息 |
| `ClipStash.exe --help` | 显示用法 |

停止 / 卸载：

```powershell
& "$env:LOCALAPPDATA\ClipStash\ClipStash.exe" --quit
& "$env:LOCALAPPDATA\ClipStash\ClipStash.exe" --uninstall
```

或直接双击项目里的 `uninstall.bat`（会保留日志和临时 PNG）。

## 资源占用

| 项目 | 实测 |
|---|---|
| exe | x64 178,176 字节；x86 147,968 字节 |
| 空闲私有内存 | 约 1.1 MB |
| 空闲 CPU | 0%（3 秒采样无增长） |
| 4K 图片转换峰值 | 约 66 MB 私有内存 |

说明：WIC 的 PNG 编码器内部会缓存整帧，所以 4K 峰值不是 1–3 MB。8K 截图会更高。日常空闲仍然约 1 MB。

## 限制

- **仅 Windows**：依赖 Win32 剪贴板、`HWND_MESSAGE`、WIC、HKCU Run。Linux/macOS 不是“换个打包参数”就能出，需要独立的平台实现。
- **只写 CF_HDROP**：粘贴到记事本等纯文本目标不会有反应；需要粘到接受文件的位置。
- **不要和 PasteEx 同时运行**：两者都会抢剪贴板，表现为 `OpenClipboard` 失败或写回被对方覆盖。
- **支持格式**：24/32bpp、BI_RGB、标准 BGRA 掩码的 CF_DIBV5/CF_DIB；其他格式回退 CF_BITMAP。
- **未签名 exe**：SmartScreen/杀软可能提示；个人使用可接受。
- **日志**：默认只记录错误；`--verbose` 才记录成功转换。日志在 `%LOCALAPPDATA%\ClipStash\clipstash.log`，超过 1 MB 轮转为 `.old`。

## License

当前仓库未添加 LICENSE，默认保留所有权利。如需在其他项目中使用，请先联系作者。
