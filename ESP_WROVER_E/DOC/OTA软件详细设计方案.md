# OTA 软件详细设计方案

> 工程：`RemoteControlO_Com`（ESP32-WROVER-E / ESP-IDF 5.2）
> 方案形态：**MQTT 下发 FTP 参数 → WiFi FTP 流式下载 → `esp_ota` 边下边烧 → 切槽重启**
> 协议报文见 [`通信协议文档.md`](./通信协议文档.md) 第 4.5 节；分区见 [`Flash分区说明.md`](./Flash分区说明.md)。

---

## 1. 设计目标与约束

| 项       | 说明                                                                                         |
| -------- | -------------------------------------------------------------------------------------------- |
| 目标     | 远程升级应用固件，无需 USB 烧录；升级失败不破坏当前可运行镜像                                |
| 触发方式 | **仅 MQTT** `Type=FtpOTA`；不提供串口 `update` |
| 传输     | WiFi/LAN 上的 FTP（被动模式），参数全部由云端报文携带 |
| 烧写     | 流式：`data_cb` → `esp_ota_write`，不落本地完整 bin |
| 分区     | 双槽 `ota_0` / `ota_1` 各 1.5 MB + `otadata` |
| 进度     | 下载过程每 **10%** 上报一次 `FtpOTA_Status` |
| 仓库安全 | **公共 Git 不硬编码** FTP 机密；运行时由 MQTT 下发；调试日志可打印连接信息 |

非目标（当前版本不做）：

- HTTPS / 签名校验 / 差分包
- 串口或网页一键 OTA
- 编译自动上传（`otapush.sh` 与 build 解耦，需人工执行）

---

## 2. 总体架构

```
┌─────────────┐   MQTT FtpOTA    ┌──────────────┐
│  云平台     │ ───────────────► │ WIFI_STA     │
│             │ ◄── ACK/Status ─ │ json/telemetry│
└─────────────┘                  └──────┬───────┘
                                        │ ota_app_start_ftp_ex()
                                        ▼
                                 ┌──────────────┐
                                 │ APP/OTA      │  业务：任务、进度、esp_ota
                                 └──────┬───────┘
                                        │ ftp_wifi_*()
                                        ▼
                                 ┌──────────────┐
                                 │ MIDDLEWARE   │  FTP 协议与流式收包
                                 │ /FTP         │
                                 └──────┬───────┘
                                        │
                                        ▼
                                 ┌──────────────┐
                                 │ esp_ota_*    │  写非运行槽、切 boot
                                 │ + otadata    │
                                 └──────────────┘
```

分层职责：

| 层        | 目录/模块                    | 职责                                                  |
| --------- | ---------------------------- | ----------------------------------------------------- |
| 协议接入  | `APP/WIFI_STA/json.c`      | 解析 `FtpOTA`，校验字段，成功后走分发表 ack         |
| 上行组包  | `APP/WIFI_STA/telemetry.c` | `FtpOTA_ACK` / `FtpOTA_Status`                    |
| OTA 业务  | `APP/OTA`                  | 忙锁、独立任务、`NEW` 解析、进度节流、`esp_ota_*` |
| FTP 传输  | `MIDDLEWARE/FTP`           | 登录/CWD/RETR、SIZE 进度、列目录找最新 bin            |
| 分区/引导 | ESP-IDF `app_update`       | begin/write/end/set_boot；`partitions.csv`          |

---

## 3. 模块设计

### 3.1 APP/OTA（`OTA.c` / `OTA.h`）

**对外接口：**

| 接口                          | 说明                                                |
| ----------------------------- | --------------------------------------------------- |
| `ota_app_init()`            | 启动时打印当前运行分区；**不**注册串口命令    |
| `ota_app_start_ftp_ex(cfg)` | 校验参数 → 置忙 → 创建 `ota_task`；供 MQTT 调用 |

**内部状态 `ota_ctx_t`：**

| 字段                              | 含义                                     |
| --------------------------------- | ---------------------------------------- |
| `handle` / `part` / `began` | `esp_ota` 句柄、目标分区、是否已 begin |
| `failed` / `written`          | 写失败标志、已写入字节                   |
| `last_report_pct`               | 已上报进度门槛（0/10/…/100）            |
| `ftp`                           | 本次 MQTT 下发参数副本                   |

**忙锁 `s_ota_busy`：** 任务运行期间为 true；重复 `FtpOTA` 直接拒绝（handler 失败 → 不回 ACK）。任务结束（失败路径）或重启前清忙（成功路径直接 `esp_restart`）。

**任务参数：** 栈 8 KB，优先级 5；独立于 MQTT 收包任务，避免阻塞订阅。

### 3.2 MIDDLEWARE/FTP

| 接口                             | 说明                                                                 |
| -------------------------------- | -------------------------------------------------------------------- |
| `ftp_wifi_get()`               | 连接 → 登录 → CWD → RETR；`data_cb` 非空则流式交给上层          |
| `ftp_wifi_find_newest_bin()`   | 在 `remote_path` 下找修改时间最新的 `.bin`，写入 `remote_name` |
| `ftp_client_set_progress_cb()` | RETR 过程按约 1% 变化通知上层；OTA 再节流到 10%                      |

找最新文件策略：

1. 优先 **MLSD**（解析 `type=file` + `modify=`）
2. 失败则 **NLST** 收集 `.bin`，再对每个文件 **MDTM** 比新
3. 最多扫描约 48 个文件名（NLST 表在堆上，避免撑爆 OTA 任务栈）

### 3.3 WIFI_STA 协议侧

- 分发表项：`{ "FtpOTA", json_handle_ftp_ota, telemetry_build_ftp_ota_ack }`
- handler **返回 true** 后框架自动发 `FtpOTA_ACK`（表示**任务已启动**，非升级完成）
- 进度与状态上报走已注册的 `telemetry_set_publish_fn`（通常即 MQTT publish）

---

## 4. 业务流程

### 4.1 时序（正常路径）

```
云平台              设备 MQTT/json         ota_task           FTP 服务器
  │                      │                    │                   │
  │── FtpOTA ───────────►│                    │                   │
  │                      │ start_ftp_ex OK    │                   │
  │◄─ FtpOTA_ACK ────────│                    │                   │
  │                      │── create task ────►│                   │
  │                      │                    │── (可选 NEW) 列目录►│
  │                      │                    │◄─ 最新 .bin 名 ────│
  │                      │                    │── RETR 流式 ──────►│
  │◄─ FtpOTA_Status 10% ─┼────────────────────│◄─ 数据块 ──────────│
  │◄─ … 20% … 100% ──────┼────────────────────│   esp_ota_write    │
  │                      │                    │ esp_ota_end        │
  │                      │                    │ set_boot_partition │
  │                      │                    │ delay 3s / restart │
  │◄─ StatusChange ──────│  (新固件上线)       │                   │
```

### 4.2 状态说明

| 阶段      | 行为                                                                               |
| --------- | ---------------------------------------------------------------------------------- |
| 受理      | 参数合法且未忙 → 建任务 → 回 `FtpOTA_ACK`（`ack=OK`）                        |
| 解析 NEW  | `FILENAME=NEW` 时先找最新 `.bin`，再下载；日志只打路径/文件名                  |
| 下载/烧写 | 首包 `esp_ota_begin(OTA_SIZE_UNKNOWN)`；每包 `esp_ota_write`                   |
| 进度      | FTP 层按 SIZE 算百分比；OTA 每跨 10% 发 `FtpOTA_Status`；收齐补 100%             |
| 成功      | `esp_ota_end` → `esp_ota_set_boot_partition` → 延时 3 s → `esp_restart`   |
| 失败      | `esp_ota_abort`（若已 begin）；清 `s_ota_busy`；**不重启**，继续跑旧固件 |

### 4.3 `FILENAME=NEW`

- 语义：下载 `PATH` 目录内**修改时间最新**的 `.bin`
- 与确切文件名互斥：非 `NEW` 时直接 RETR 指定名
- 串口无入口；仅 MQTT 可触发

---

## 5. 分区与启动策略

详见 [`Flash分区说明.md`](./Flash分区说明.md)。要点：

| 分区                  | 作用                                                   |
| --------------------- | ------------------------------------------------------ |
| `ota_0` / `ota_1` | 双应用槽，各 1.5 MB；镜像须小于槽大小                  |
| `otadata`           | 记录下次启动槽；由 `esp_ota_set_boot_partition` 更新 |
| `nvs`               | 配网/MQTT/SN 等，**OTA 不清空**                  |

运行时：`esp_ota_get_next_update_partition(NULL)` 取**非当前运行槽**作为写入目标。
首次 `idf.py flash` 一般写入 `ota_0@0x20000`；升级成功后可能从 `ota_1@0x1A0000` 启动。

---

## 6. 云端协议摘要

完整字段与示例见 [`通信协议文档.md`](./通信协议文档.md) §4.5。

| Type              | 方向       | 含义                                   |
| ----------------- | ---------- | -------------------------------------- |
| `FtpOTA`        | 云 → 设备 | 下发 IP/PORT/USER/PASSWD/PATH/FILENAME |
| `FtpOTA_ACK`    | 设备 → 云 | 受理成功，`params.ack="OK"`          |
| `FtpOTA_Status` | 设备 → 云 | `status="download"`，`progress="10"  |
| `StatusChange`  | 设备 → 云 | 重启后登录上报，含新 `version`       |

必填参数缺一不可：`IP`、`PORT`、`USER`、`PASSWD`、`PATH`、`FILENAME`。
`device` 若携带须与本机 SN 一致。

---

## 7. 工程侧配套：`otapush.sh`

| 项     | 说明                                                                    |
| ------ | ----------------------------------------------------------------------- |
| 作用   | 把本地 `build/RemoteControlO_Com.bin` 上传到 FTP 目录 |
| 远程名 | `RemoteControlO<分支-短哈希>.bin`（与固件 `APP_VERSION_FULL` 一致） |
| 与编译 | **不挂钩**；需手动 `./otapush.sh` |
| 机密   | `OTA_FTP_HOST` / `USER` / `PASS` 等环境变量或根目录 `.env`，不进 Git |
| 设备侧 | MQTT 可指定该确切名，或 `FILENAME=NEW` 取目录最新 |

---

## 8. 关键实现参数

| 宏/常量                   | 值        | 含义              |
| ------------------------- | --------- | ----------------- |
| `OTA_TASK_STACK`        | 8 × 1024 | OTA 任务栈        |
| `OTA_TASK_PRIO`         | 5         | 任务优先级        |
| `OTA_REBOOT_DELAY_MS`   | 3000      | 成功后重启前延时  |
| `OTA_PROGRESS_STEP_PCT` | 10        | MQTT 进度上报步长 |
| `OTA_FTP_FILENAME_NEW`  | `"NEW"` | 最新固件关键字    |

---

## 9. 错误与边界

| 场景                     | 处理                                    |
| ------------------------ | --------------------------------------- |
| 缺字段 / 空白参数        | `start_ftp_ex` 失败 → 无 ACK         |
| OTA 进行中再次下发       | 拒绝，打日志                            |
| 无双 OTA 分区            | 任务退出失败                            |
| `NEW` 目录无 `.bin`  | 失败退出，不重启                        |
| FTP/写 Flash 中途失败    | abort（若已 begin），清忙，保持旧槽启动 |
| `esp_ota_end` 校验失败 | 同上                                    |
| 镜像大于分区             | write/end 失败                          |
| 进度跨越多个 10%         | while 循环连续上报多个门槛              |

---

## 10. 机密与日志

- **源码/脚本**：公共仓库不写死真实 FTP 口令；设备侧只接受 MQTT 下发；`otapush.sh` 从环境变量或本地 `.env`（已 gitignore）读取  
- **运行日志**：允许打印主机/端口/用户/路径等，便于现场排查（口令是否打日志由调试需要决定）  
- **MQTT 报文**：含口令时依赖 Broker/链路安全，由部署方负责  

---

## 11. 源码索引

| 内容             | 路径                                                         |
| ---------------- | ------------------------------------------------------------ |
| OTA 业务         | `components/APP/OTA/OTA.c`、`OTA.h`                      |
| FTP WiFi 封装    | `components/MIDDLEWARE/FTP/ftp_wifi.c`、`ftp_wifi.h`     |
| FTP 协议实现     | `components/MIDDLEWARE/FTP/ftp_client.c`、`ftp_client.h` |
| MQTT 下行 FtpOTA | `components/APP/WIFI_STA/json.c`                           |
| ACK / 进度组包   | `components/APP/WIFI_STA/telemetry.c`、`telemetry.h`     |
| 分区表           | `partitions.csv`                                           |
| 上传脚本         | `otapush.sh`                                               |
| 启动调用         | `main/main.c` → `ota_app_init()`                        |

---

## 12. 验证建议

1. 全量烧录双分区固件，确认运行槽与 `otadata` 正常。
2. `./otapush.sh` 上传带版本名的 bin。
3. MQTT 下发确切 `FILENAME`，观察 `FtpOTA_ACK` → 每 10% `FtpOTA_Status` → 重启 → `StatusChange.version` 更新。
4. 再下发 `FILENAME=NEW`，确认选中最新上传文件。
5. 升级中再次下发，应被拒绝；故意错误密码/文件名，应失败且不重启、旧固件仍可运行。
6. 确认公共仓库与提交中无真实 FTP 口令；`otapush` 使用 `.env` 或环境变量。
