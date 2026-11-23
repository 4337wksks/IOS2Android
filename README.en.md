# IOS2Android

[简体中文](README.md) | **English**

IOS2Android converts ARM64 iOS applications into Android APKs. It embeds the original ARM64 code in an ELF binary and runs it with a compatibility runtime. The runtime implements a subset of Darwin, Objective-C, and iOS framework interfaces, with an Android host providing windows, input, networking, and audio. Metal uses a Vulkan backend; shader translation and application packaging take place at build time.

The project is under development. **Support for iOS APIs and their translation to Android is incomplete.** Compatibility depends on the APIs, binary format, and shaders used by an application. Each application version needs adaptation and validation. A successful build does not guarantee that every feature works.

## Project layout

```text
IOS2Android/
├── porter/                    Offline build tools
│   ├── __main__.py            CLI entry point
│   ├── build.py               Configuration, adapter loading, and build pipeline
│   ├── files.py               File operations and caching
│   ├── bundle/                IPA metadata, fonts, NIB, and CoreUI resources
│   ├── image/                 Mach-O processing, binary patches, interface indexing, and unwind data
│   ├── shaders/               Metal/AIR/GLSL parsing, translation, and packaging
│   └── android/               Compilation, APK packaging, and signing
├── runtime/                   Compatibility runtime
│   ├── core/                  Darwin ABI, Objective-C, scheduling, and object lifetimes
│   ├── frameworks/            iOS framework interfaces
│   ├── backends/vulkan/       GPU commands, resources, pipelines, and presentation
│   ├── contracts/             Metal descriptor definitions
│   ├── include/i2a/           Runtime extension API
│   ├── diagnostics/           Runtime diagnostics
│   └── support/               Texture decoding and other support code
├── host/android/              Android host
│   ├── java/                  Activity and platform interfaces
│   ├── res/                   Styles and resources
│   └── templates/             AndroidManifest template
├── pyproject.toml             Python project and dependency configuration
├── LICENSE                    Noncommercial license
├── NOTICE                     Attribution and source notices
├── README.md                  Chinese README
└── README.en.md               English README
```

## Related projects

| Project | Description and reference |
|---|---|
| [nx2android](https://github.com/shirai91/nx2android) | Ports Nintendo Switch software to Android. A reference for the offline build pipeline and host architecture. |
| [Maclator](https://github.com/linuxkid473/maclator) | A macOS compatibility project. A reference for precomputed ABI and descriptor metadata, and resource lifetime management. |
| [CARParser](https://github.com/Timac/CARParser) | A parser for Apple CoreUI `.car` resources. A reference for resource formats and field definitions. |

## Notes

### Building

The current build environment uses Python 3.11+, Android NDK r27d for Windows, SDK Build Tools 35 / API 35, and a JDK. Python dependencies are declared in `pyproject.toml`. External JSON configuration specifies native dependencies, shader tools, and signing details. The toolchain and dependencies must be installed separately.

Run these commands from the repository root to view usage:

```bash
python -B -m porter --help
python -B -m porter build --help
```

The CLI provides `inspect`, `build`, `check-core`, and `shader`. The `check-core` command compiles only the core runtime, without linking application adapters, and does not produce a runnable APK. Dev APKs enable Android debugging for ADB access to private storage; release APKs disable it.

External adapters supply application patches, resource replacements, service configuration, and shader translation rules. Keep IPAs, toolchains, configuration, signing keys, adapters, and build outputs outside the repository.

Online services reuse portable endpoints and configuration from the original application where available. iOS account, payment, push, and third-party platform identifiers cannot be used directly on Android. Without compatible configuration, the runtime reports failure or unavailability. Helpshift can use its web backend when a valid domain is available.

Resources are normally read from the installed APK; interfaces that require a real file path extract individual files. Startup removes resource copies left by older runtime versions. Shaders and driver compilation results are cached in private storage. Updates remove old caches even when the application version stays the same. Unavailable caches are regenerated.

APKs are written to `out/<application-id>/<CFBundleVersion>/dev|release/` in the external workspace. Build different input versions sequentially when sharing a cache. Use separate workspaces for parallel builds.

### Compatibility

- Encrypted binaries and chained fixups are not supported. Dynamic library and symbol loading also have coverage limits.
- Scene lifecycle, complex NIBs, layout, animation, scrolling, tables, and text editing still have compatibility gaps; not every native interface can be rendered.
- Web pages support basic loading and interaction. Multiple windows, downloads, and some navigation and scripting APIs remain unsupported.
- Metal-to-Vulkan and MSL/AIR translation cover only the interfaces and translation paths currently implemented.
- Audio format support is limited, and real-time AudioQueue playback is not implemented.
- Full calendar/location notification triggers, notification actions and attachments, Bonjour browsing/resolution, and gesture failure dependencies and arbitration are not implemented.
- Background location, region monitoring, heading, and some authorization APIs are not implemented. Geocoding depends on the device's Android service.

### License and attribution

This project is licensed under [PolyForm Noncommercial 1.0.0](LICENSE).

**This repository is intended for learning, research, and noncommercial development. Commercial use and use for profit are not permitted.** The license allows noncommercial use, modification, redistribution, and reuse of code. See `LICENSE` for the full terms.

Source repository: [ThatSkyOldServer/IOS2Android](https://github.com/ThatSkyOldServer/IOS2Android). Code reused from this project must credit the original authors or link to the source repository.

### Redistribution

- You may copy, modify, and redistribute the project for noncommercial purposes, including source code, compiled binaries, and modified versions.
- Every redistribution must include the `LICENSE` text or its official URL and preserve every line beginning with `Required Notice:` in [NOTICE](NOTICE), including the authors and source repository URL.
- Redistribution and subsequent use remain subject to the noncommercial terms. The project and modified versions may not be used commercially or for profit, and project code may not be republished under a license that permits commercial use.
- Third-party tools, libraries, and SDKs retain their own licenses and attribution requirements. This license does not authorize redistribution of the original iOS applications, their assets, or third-party dependencies. Distributing them requires separate permission or compliance with their applicable licenses.

See `LICENSE` for the complete and governing terms.
