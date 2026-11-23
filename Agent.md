# Ai Passport 录音固件 · Agent 操作指南

本指南供能够访问本机 USB 串口的助手使用。命令从仓库根目录执行。安装现有版本直接使用仓库中的分段镜像，开发构建步骤在文末。

## 识别设备并刷写

1. 目标为 FoloToy Ai Passport，ESP32-C3，8 MB Flash。确认设备型号，使用 USB-C **数据线**连接已开机的设备。关闭占用串口的导出工具、串口监视器或 WebSerial 页面。
2. 在 Python 3.11+ 环境中安装工具并枚举串口。用插拔前后的差异找出工牌；若有多台同型号设备，先确定用户指定的那台。

   ```powershell
   python -m pip install "esptool>=4.8,<5"
   python -m serial.tools.list_ports -v
   ```

3. 将下文的 `COMx` 换为实机端口。预检核对三段镜像的 SHA-256、写入边界、芯片和 Flash 容量。校验失败时先解决报错。

   ```powershell
   python firmware/install.py --port COMx
   ```

4. 写入会替换引导程序、分区表和应用，旧布局中的数据可能受到影响。用户需要保留原机状态时，先备份到本机的新文件，保存脚本输出的 SHA-256。原厂恢复入口来自设备自己的二维码。

   ```powershell
   python firmware/install.py --port COMx --backup "$env:USERPROFILE\passport-original-8MB.bin"
   ```

5. 用户已要求为这台设备刷写、且预检通过后，执行安装。脚本按清单写入三个区域。单独的 `app.bin` 起始偏移为 `0x10000`。

   ```powershell
   python firmware/install.py --port COMx --write
   ```

6. 设备重启后说一句话，安静等待约 10 秒。进入列表，回听新片段，再通过 USB 导出并打开 WAV。分别报告镜像写入、设备回听和 WAV 导出中实际验证过的步骤。

## 录音与按键

普通模式开机后自动监听。WebRTC VAD 结合环境底噪、音量和发声周期触发开录。预录最多 480 ms，句中停顿在 1.2 秒以内完整保留，更长的明确无人声间隔保留两端各 0.6 秒。连续 10 秒未检出人声便结束片段，结尾最多留 80 ms。回放和 USB 传输会话期间暂停采集。

| 状态 | 操作 | 设备反应 |
| --- | --- | --- |
| 关机 / 开机 | 按电源键约 0.5 秒 / 2 秒 | 硬件开机 / 关机 |
| 息屏 → 壁纸 → 列表 | 每次短按任一功能键 | 第一次亮壁纸，第二次显示列表并选中最新片段 |
| 列表 | 短按 ＋ / － | 选择较新 / 较早的片段 |
| 列表 | 短按 OK | 从头播放选中片段，再按停止 |
| 列表 | 长按 OK | 打开清空页。短按 ＋ 清空设备录音，短按 － 或等待 5 秒取消。USB 导出中禁用清空 |
| 任一界面 | 长按 ＋ | 息屏，当前录音或播放继续 |
| 息屏、壁纸或列表 | 长按 － | 切换影院模式，进入时保存片段并停录，再次长按恢复。模式记入 NVS，重启后保留 |
| 列表闲置 20 秒 | 无需操作 | 电池供电时息屏，连接可识别的 USB 主机时回到壁纸 |

## 导出、配置与回收

在 Windows 运行 `desktop/start.cmd`。它优先使用随附的 `bin/PassportExport.exe`。工牌开机后通过 USB Serial/JTAG 传输片段，工具校时、校验并解码为 WAV。资源管理器显示的盘符映射到电脑本地归档，删除盘符里的 WAV 会影响本机副本。要释放工牌空间，在确认本机归档可用后运行 `desktop/reclaim.cmd`，工具会逐条校验再回收。

需要自定义时，把 [settings.example.json](desktop/settings.example.json) 复制为同目录的 `settings.json`，修改后重启导出工具。

| 字段 | 默认值 | 用途 |
| --- | --- | --- |
| `archive` | `~/Documents/PassportRecordings` | 本机归档根目录。可复制的 WAV 位于其下的 `USB/设备编号/` |
| `drive_letter` | `R` | 优先使用的盘符，冲突时自动选其他字母 |
| `port` | 空字符串 | 自动查找工牌；填写具体端口可固定设备 |
| `windows_notifications` | `true` | 控制导出工具的 Windows 通知 |

- **端口或盘符异常**：用 `bin/PassportExport.exe --self-test` 离线检查解码、写盘和盘符映射。用 `bin/PassportExport.exe --port COMx --once` 单次导出。
- **时间未同步**：在本次开机期间运行一次导出工具再关机。未校时就关机的片段以 `UNSYNC` 命名。
- **空间不足**：剩余约 300 KB 时提示，少于 24 KiB 时暂停新录音。先导出和核对 WAV，再回收。遇到解码失败可用 `--rescue` 保存原始编码数据。
- **录音区挂载失败**：已有非空数据时先保留并备份。只有用户明确允许丢弃该分区数据时，才使用 `bin/PassportExport.exe --initialize 设备编号`。设备编号取自工具提示。

目前用 USB 导出 WAV，可在电脑上交给 STT 模型。接墙充自动上传和 NAS/Docker 处理属于后续版本。

## 固件配置与开发

| 配置 | 当前值 | 源文件 |
| --- | --- | --- |
| 工具链与目标 | ESP-IDF 5.5.3，ESP32-C3，8 MB Flash | [CMakeLists.txt](firmware/CMakeLists.txt)、[sdkconfig.defaults](firmware/sdkconfig.defaults)、[组件锁文件](firmware/dependencies.lock) |
| Flash 布局 | 引导区从 0x0 开始，分区表 0x8000，应用 0x10000，LittleFS 录音区 0x300000 起、大小 0x500000 | [partitions.csv](firmware/partitions.csv)、[发布镜像清单](bin/flash/manifest.json) |
| 新录音 | 16 kHz 单声道，Speex 宽带，20 ms / 42 字节，16.8 kbps。5 MiB ÷ 126,000 字节/分钟 ≈ 41.6 分钟，文件系统和保留空间会降低可用时长 | [rec.h](firmware/main/rec.h)、[main.c](firmware/main/main.c) |
| 检测与剪切 | 8 kHz 检测，WebRTC VAD 模式 2；预录、停顿保留及结束等待在编译时设定 | [gate.c](firmware/main/gate.c)、[clip_buffer.h](firmware/main/clip_buffer.h) |
| 播放与导出 | 扬声器音量 80%；新文件导出为 16 kHz、16 位、单声道 WAV，旧 G.729 文件导出为 8 kHz WAV | [main.c](firmware/main/main.c)、[exporter.py](desktop/exporter.py) |
| 板级硬件 | ES8311、ST7789P3、CW2017、ADC 三键。移植时核对引脚、按键分压和外设驱动 | [bsp_pins.h](vendor/ai-passport/components/bsp/include/bsp_pins.h) |

在已激活 ESP-IDF 5.5.3 的终端中构建并校验镜像。Windows 工具链遇到中文路径问题时，使用英文路径的工作副本。

```sh
cd firmware
idf.py set-target esp32c3
idf.py build
idf.py merge-bin -o build/FoloToy-AI-Passport-full.bin
python ../vendor/ai-passport/tools/verify_firmware.py build
```

开发构建输出到 `firmware/build/`。安装脚本始终读取 `bin/flash/`；更新发布镜像时，同步更新三个分段、合并镜像及相应的 SHA-256 清单。改动编码或帧格式时，同时检查设备回放和电脑解码对旧片段的兼容性。

在仓库根目录运行电脑端回归测试：

```sh
python -m pip install -r desktop/requirements.txt
python -m unittest discover -s tests -v
```

更多设备状态、异常处理和原厂恢复信息见 [Doc.md](Doc.md)。
