# SZPI ESP32-S3 Handheld

一个基于立创实战派 ESP32-S3 的 LVGL 小掌机实验项目。当前重点是把板载 LCD、触摸、TF 卡、摄像头、音频、WLAN、BLE 和随机图片 API 串成一个可玩的产品雏形。

## 当前功能

- LVGL 双页启动器，支持触摸翻页和用户自定义按键翻页。
- 随机图片应用：HTTPS 拉图、加载态、缓存到 TF 卡、全屏/状态栏切换、自定义按键刷新。
- TF 卡文件管理：图片、GIF、文本、MP3/WAV 预览入口，并对大文件自动跳过预览以避免卡屏。
- 媒体库：后台扫描 TF 卡媒体文件，统计图片、音频、视频、文本并生成轻量索引。
- 音乐播放器：扫描 TF 卡音乐目录。
- 录音机：板载麦克风录制 16 kHz mono WAV 到 TF 卡。
- 摄像头：实时预览和拍照保存到 TF 卡。
- WLAN：扫描、开放网络直连、已连接网络历史保存到本机 NVS 和 TF 卡。
- TF 卡管理：查看容量、挂载文件系统、GPT/MBR/裸 FAT 识别、前 4 个分区条目，设备端可选 Auto/FAT32/exFAT 格式化，并重建产品目录。
- BLE HID 示例页：做了重复进入/退出的稳定性保护。
- 姿态页面：QMI8658 读数失败时避免用坏数据刷新 UI。

仍在探索的方向：BLE 音频或外接 A2DP 模块、USB 摄像头、小视频播放/录像、媒体缩略图和后台增量索引。

## 仓库结构

```text
backend/image-api/        PHP 随机图片 API，可部署到普通 PHP 主机
docs/                     构建、烧录、TF 卡、VS Code 和发布说明
examples/tf-card/         TF 卡目录和配置模板
firmware/handheld/        ESP-IDF 5.4.x 固件工程
tools/                    本地辅助脚本
```

## 5 分钟启动

1. 安装 ESP-IDF 5.4.x，推荐与本机验证版本一致：ESP-IDF v5.4.4。
2. 用 VS Code 打开仓库根目录，安装推荐的 Espressif IDF 扩展。
3. 插入 ESP32-S3，运行环境检查：

```powershell
.\tools\powershell\doctor.ps1
```

4. 准备 TF 卡，假设 TF 卡盘符是 `E:`：

```powershell
.\tools\powershell\prepare-tf-card.ps1 -Drive E: -Ssid "YOUR_2G_SSID" -Password "YOUR_WIFI_PASSWORD" -ApiUrl "https://example.com/image.php?key=change-me&return=print&esp=1"
```

5. 构建并烧录，假设串口是 COM7：

```powershell
.\tools\powershell\idf.ps1 build
.\tools\powershell\idf.ps1 -Port COM7 flash monitor
```

如果你的串口不是 COM7，替换成设备管理器里的实际端口。

VS Code 用户也可以直接运行这些任务：

- `Project: Doctor`
- `ESP-IDF: Build handheld`
- `ESP-IDF: Flash and monitor handheld`

## 隐私和配置

仓库不提交真实 WLAN 密码，也不提交后端 `config.php`。固件默认 WLAN 为空，推荐通过 TF 卡 `/szpi/config/wifi.ini` 或 WLAN 页面保存配置。

PHP API 默认 key 是 `change-me`，部署时复制 `backend/image-api/config.sample.php` 为 `config.php` 并修改。

## 文档

- [Getting Started](docs/getting-started.md)
- [TF Card Layout](docs/tf-card.md)
- [VS Code ESP-IDF](docs/vscode-esp-idf.md)
- [Windows Environment](docs/windows-environment.md)
- [Feature Notes](docs/features.md)
- [Roadmap](docs/roadmap.md)
- [Release Build](docs/release.md)

## 开源发布建议

源码仓库只提交可复现源代码和模板。生成的固件位于 `release/`，默认被
`.gitignore` 排除，更适合上传到 GitHub/Gitea Release。
