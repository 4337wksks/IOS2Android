# IOS2Android

**简体中文** | [English](README.en.md)

IOS2Android 将 ARM64 iOS 应用转换为 Android APK。原应用的 ARM64 代码嵌入 ELF，由兼容运行库执行。运行库实现部分 Darwin、Objective-C 和 iOS 系统框架接口，通过 Android 宿主提供窗口、输入、网络和音频等能力。Metal 使用 Vulkan 后端，着色器转换与应用打包在构建时完成。

项目仍在开发中，**尚未完整实现全部 iOS 接口及其到 Android 的转换**。应用能否运行取决于使用的接口、镜像格式和着色器，需要针对具体版本进行适配和验证。构建成功不代表所有功能均可用。

## 目录结构

```text
IOS2Android/
├── porter/                    离线构建工具
│   ├── __main__.py            命令行入口
│   ├── build.py               配置、适配加载与构建流程
│   ├── files.py               文件操作与缓存
│   ├── bundle/                IPA 元数据、字体、NIB 和 CoreUI 资源
│   ├── image/                 Mach-O 处理、镜像补丁、接口索引与展开信息
│   ├── shaders/               Metal/AIR/GLSL 解析、转换与打包
│   └── android/               编译、APK 打包与签名
├── runtime/                   兼容运行库
│   ├── core/                  Darwin ABI、Objective-C、调度与对象生命周期
│   ├── frameworks/            iOS 系统框架接口
│   ├── backends/vulkan/       GPU 命令、资源、管线与呈现
│   ├── contracts/             Metal descriptor 定义
│   ├── include/i2a/           运行库扩展接口
│   ├── diagnostics/           运行诊断
│   └── support/               纹理解码等辅助代码
├── host/android/              Android 宿主
│   ├── java/                  Activity 与平台接口
│   ├── res/                   样式与资源
│   └── templates/             AndroidManifest 模板
├── pyproject.toml             Python 项目与依赖配置
├── LICENSE                    非商业使用许可
├── NOTICE                     署名与来源通知
├── README.md                  中文说明
└── README.en.md               英文说明
```

## 参考项目

| 项目 | 简介与参考内容 |
|---|---|
| [nx2android](https://github.com/shirai91/nx2android) | 将 Nintendo Switch 程序移植到 Android 的项目。本仓库参考其离线构建流程与宿主分层。 |
| [Maclator](https://github.com/linuxkid473/maclator) | macOS 程序兼容运行项目。本仓库参考其 ABI 与 descriptor 元数据预计算、资源生命周期管理。 |
| [CARParser](https://github.com/Timac/CARParser) | Apple CoreUI `.car` 资源解析工具，用于参考资源格式与字段定义。 |

## 补充说明

### 构建

当前构建环境使用 Python 3.11+、Windows Android NDK r27d、SDK Build Tools 35 / API 35 和 JDK。Python 依赖在 `pyproject.toml` 中声明；原生依赖、着色器工具和签名信息通过外部 JSON 配置指定。工具链与依赖需自行准备。

在仓库根目录运行以下命令查看用法：

```bash
python -B -m porter --help
python -B -m porter build --help
```

命令包括 `inspect`、`build`、`check-core` 和 `shader`。其中 `check-core` 只编译核心运行库，不链接应用适配器，也不生成可运行 APK。

应用补丁、资源替换、服务配置和着色器转换规则由外部适配器提供。IPA、工具链、配置、签名密钥、适配器和构建产物均放在仓库外。

在线服务优先复用原应用的通用地址与配置；iOS 专属的账号、支付、推送和第三方平台标识不能直接用于 Android。没有可复用配置时，运行库按未配置处理，返回相应失败或不可用状态；Helpshift 有有效 domain 时可使用网页后端。

只读 Bundle 资源默认通过 Android AssetManager 从已安装 APK 访问。文件接口保持资源内的偏移、长度和 EOF，未压缩资源可直接映射；压缩资源按需解码到内存，要求真实文件路径的接口才提取单个文件。应用启动时清理运行库创建的旧资源缓存。

构建阶段将 SPIR-V 打包为 `assets/i2a/shaders.bin`；首次使用时放入应用私有 `code_cache/vulkan/<PACKAGE_REVISION>/`，后续启动直接映射该缓存。图形与计算管线共用持久化的 `pipelines.bin`，按实际管线状态创建并由驱动复用已缓存代码。独立的 `PACKAGE_REVISION` 每次打包更新，和应用版本号无关；新 APK 启动时清理旧修订目录，GPU 或驱动不兼容时重建管线缓存。缓存写入失败不影响正常资源访问和管线创建。

APK 输出到外部工作区的 `out/<application-id>/<CFBundleVersion>/dev|release/`。不同版本共用构建缓存时需顺序构建；并行构建应使用独立工作区。

### 兼容范围

- 加密镜像与 chained fixups 尚不支持，动态库和符号加载也有覆盖限制。
- UIKit、Auto Layout 和 CoreAnimation 仅实现部分功能，尚不支持任意 NIB 页面、完整触摸滚动与表格控件行为。
- WebKit 尚缺同步 JavaScript、多窗口、下载、流式请求、自定义请求头的 POST、导航响应决策和非零数据检测选项。
- 文本输入尚缺完整 TextKit、自定义文本容器、组合字符删除语义、日历等数据检测和原生状态栏点击滚动。
- Metal → Vulkan 与 MSL/AIR 转换仅支持已实现的接口和转换路径。
- 音频格式支持仍有限，实时 AudioQueue 播放尚未实现。
- 通知的完整日历/位置触发、分类动作与附件，Bonjour 浏览/解析，以及手势失败依赖与完整仲裁尚未实现。

### 许可与署名

本项目采用 [PolyForm Noncommercial 1.0.0](LICENSE) 许可。

**本仓库供学习、研究和非商业开发使用，不允许用于商业用途或盈利活动。** 许可允许非商业使用、修改、分发和代码引用，完整条款见 `LICENSE`。

原始仓库：[ThatSkyOldServer/IOS2Android](https://github.com/ThatSkyOldServer/IOS2Android)。引用本项目代码时，须注明原作者或原始仓库。

### 分发规则

- 允许出于非商业目的复制、修改和分发本项目，包括源码、编译产物及修改版本。
- 每次分发必须提供 `LICENSE` 许可文本或其官方链接，并保留 [NOTICE](NOTICE) 中所有以 `Required Notice:` 开头的署名行，包括作者和原始仓库地址。
- 分发和后续使用均须遵守非商业限制，不得将本项目或修改版本用于商业用途或盈利活动，也不得以允许商用的许可重新发布本项目代码。
- 第三方工具、库与 SDK 保留各自的许可及署名要求。本许可不授予原 iOS 应用、应用资源或第三方依赖的分发权利；如需一同分发，须另行取得授权或满足其适用许可。

完整授权条件以 `LICENSE` 为准。
