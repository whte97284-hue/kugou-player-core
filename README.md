<div align="center">
  <img src="static/icon.png" alt="Logo" width="128" height="128">

  # Kugou Audio Decoder
  ### 酷狗音频解码终端

  <p align="center">
    <b>现代化、高性能的音频解密工具，基于 Flask 构建。</b>
  </p>

  <p>
    <img src="https://img.shields.io/badge/version-2.2.0-blue.svg?style=flat-square" alt="Version">
    <img src="https://img.shields.io/badge/license-MIT-green.svg?style=flat-square" alt="License">
    <img src="https://img.shields.io/badge/platform-Windows-lightgrey.svg?style=flat-square" alt="Platform">
    <img src="https://img.shields.io/badge/backend-Python-yellow.svg?style=flat-square" alt="Python">
  </p>
</div>

---

## 目录

- [项目概述](#项目概述)
- [核心特性](#核心特性)
- [安装与部署](#安装与部署)
- [使用指南](#使用指南)
- [技术架构](#技术架构)
- [开发相关](#开发相关)
- [免责声明](#免责声明)
- [许可证](#许可证)

---

## 项目概述

**酷狗音频解码终端** 是一款专为 Windows 平台设计的音频格式转换工具。它能够将酷狗音乐特有的加密格式（KGM/KGG）高效转换为通用的 MP3 或 FLAC 格式，方便用户在不同设备上进行本地播放与归档。

本项目采用 **Flask** 后端加网页前端的架构，在保证底层解码高效稳定的同时，提供了极具现代感和交互性的图形用户界面。

---

## 核心特性

- **高效转码**：支持酷狗 KGM (.kgm/.kgma)、KGG (.kgg) 和网易云 NCM (.ncm) 格式的无损/高保真解码。
- **批量处理**：支持拖拽多个文件或文件夹进行并发转换，实时显示任务进度。
- **现代化 UI**：内置多套精美主题（经典终端、深海蓝、赛博粉等），支持深色模式。
- **资产管理**：集成的归档库功能，支持对已转换音频的搜索、播放与管理。
- **即时预览**：内置音频播放器，转换完成后可直接试听效果。
- **设置持久化**：自动保存输出目录、音量偏好等用户设置。

---

## 安装与部署

### 便携版

适用于临时使用或通过 USB 设备携带。

- **文件名**: `酷狗音频解码器-2.1.0.exe`
- **说明**: 单文件可执行程序。启动时会自动释放资源到临时目录，退出后自动清理。由于路径动态变化，**不支持**固定到任务栏。

> [!NOTE]
> 运行本软件需要 Windows 10 (64-bit) 或更高版本操作系统。

---

## 使用指南

### 格式转换

1. **拖拽导入**：在主界面直接将 `.kgm`、`.kgg` 或 `.ncm` 文件拖入中央区域。
2. **选择文件**：点击中央区域，通过文件资源管理器选择文件。
3. **自动处理**：导入后软件将自动开始转换，界面下方会显示详细的进度条和日志状态。

### 支持格式

| 平台 | 格式 | 扩展名 | 说明 |
| :--- | :--- | :--- | :--- |
| 酷狗音乐 | KGM/KGMA | `.kgm` `.kgma` | 播放器内核已内置掩码;旧版网页工具仍需 kgm.mask |
| 酷狗音乐 | KGG | `.kgg` | 需要本机播放记录 |
| 网易云音乐 | NCM | `.ncm` | 可直接解码，支持封面和标签 |

### 资源管理

进入 **"归档库"** 页面查看所有已转换的音频文件。

- **播放**：点击播放图标进行试听。
- **定位**：点击文件夹图标打开文件所在目录。
- **复制**：支持一键将文件复制到剪贴板，方便粘贴到其他设备或目录。
- **搜索**：顶部搜索框可快速过滤文件名。

### 个性化设置

进入 **"设置"** 页面：

- **输出目录**：自定义转换后的文件存储位置（默认为软件根目录下的 `output`）。
- **主题切换**：点击不同的主题卡片即时切换应用风格，配置会自动保存。

---

## 技术架构

本项目采用了典型的混合应用架构，结合了 Web 技术的灵活性与 Python 的强大处理能力。

| 模块 | 技术栈 | 职责 |
| :--- | :--- | :--- |
| **用户界面** | HTML5 / CSS3 / ES6 | 构建响应式界面，处理用户交互逻辑与状态管理。 |
| **后端服务** | Python Flask | 提供 RESTful API，处理业务逻辑，如文件系统操作、配置管理等。 |
| **解码核心** | CLI Tools (kgg-dec, kgm-dec, ncmdump) | 底层二进制工具，负责实际的音频解密与转码工作。 |
| **构建工具** | PyInstaller | 负责将前后端及依赖环境打包为可分发的 Windows 可执行文件。 |

---

## 开发相关

如果您希望参与贡献或进行二次开发，请参考以下信息。

### 项目结构

```
KugouConverterWeb/
├── static/             # 前端静态资源 (CSS, JS, Images)
├── templates/          # HTML 模板
├── tools/              # 解码核心工具集
├── app.py              # Flask 后端入口
├── build.bat           # 打包脚本 (PyInstaller)
└── requirements.txt    # Python 依赖清单
```

### 构建流程

1. 安装依赖：`pip install -r requirements.txt`。
2. 运行 `build.bat`，使用 PyInstaller 打包。

---

## 免责声明

> [!WARNING]
> **请务必仔细阅读以下条款：**

1.  **仅限个人学习**：本软件仅供个人技术研究和学习使用，**严禁**用于任何商业用途或盈利行为。
2.  **版权尊重**：本软件不提供任何音乐下载服务。用户仅可对自己本地已拥有合法使用权（如已购买 VIP）的音频文件进行格式转换。
3.  **法律责任**：用户在使用本软件过程中产生的任何法律责任（包括但不限于侵犯版权），均由用户自行承担。开发者不对任何滥用行为负责。
4.  **禁止销售**：本软件完全免费开源，如果您是付费购买的，请立即退款并举报。

---

## 许可证

本项目基于 **MIT License** 开源。

核心解码工具来源于以下开源项目：
- [Unlock Music](https://github.com/unlock-music) - KGM/KGG 解码
- [ncmdump](https://github.com/taurusxin/ncmdump) - NCM 解码

---

Copyright © 2024-2026 AudioDecoder Project. All Rights Reserved.
