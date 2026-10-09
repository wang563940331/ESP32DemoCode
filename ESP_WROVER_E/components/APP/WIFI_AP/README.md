# WIFI_AP — SoftAP 配网与本地 Web 架构

手机连上设备 SoftAP 后，通过浏览器访问 `http://192.168.4.1/...` 完成参数配置、电表查看、历史电量与日志下载。
本目录负责：**开热点 → 起 HTTP → 起 WebSocket → 读写 NVS 配置**。

---

## 一图看懂

```
手机 / PC
   │  Wi-Fi 连 SoftAP（SSID = 设备 SN，当前多为开放热点）
   ▼
ESP32 SoftAP  192.168.4.1
   │
   ├─ HTTP :80          页面与 REST 风格 API（只读展示 / 下载）
   │     wifi_ap.c          SoftAP + httpd 生命周期、/restart、Config 导出/写入
   │     wifi_ap_web.c      Web 总注册入口
   │     wifi_ap_web_*.c    各业务页与 API
   │
   └─ WebSocket /ws     实时配置读写、电表快照（主业务通道）
         wifi_ap_ws.c
              │ 订阅 event_bus
              ├─ EVENT_SENSOR_UPDATED → 本地缓存
              └─ EVENT_METER_UPDATED  → 本地缓存
```

**设计原则**：HTML 只负责 UI；改配置、读电表走 WebSocket；传感器/电表数据经 `event_bus` 推入 WS 本地缓存，WS 模块不直接依赖 METER/SENSOR 实现。

---

## 文件职责

| 文件                       | 职责                                                                                                |
| -------------------------- | --------------------------------------------------------------------------------------------------- |
| `wifi_ap.c` / `.h`     | SoftAP 启停、httpd、参数表、NVS 加载、`Config` JSON 导出/应用、`/restart` `/restore_defaults` |
| `wifi_ap_ws.c` / `.h`  | `ws://…/ws` 协议：握手、GetData / SetConfig、广播                                                |
| `wifi_ap_web.c` / `.h` | 聚合注册所有 HTTP 页面与 API（不含`/ws`）                                                         |
| `wifi_ap_web_common.*`   | 公共皮肤、导航条、`wifi_ap_web_send_page`                                                         |
| `wifi_ap_web_pages.*`    | `/wsconfig` 配置页、`/wsmeter` 电表页（HTML 内嵌在 C 字符串）                                   |
| `wifi_ap_web_energy.*`   | `/wsenergy`、`/api/energy/history`                                                              |
| `wifi_ap_web_logs.*`     | `/wslogs`、`/api/logs`、`/api/logs/download`                                                  |
| `wifi_ap_mem.*`          | 大缓冲优先 PSRAM，减轻内部 DRAM                                                                     |

页面没有独立 `.html` 文件，全部编译进固件字符串，便于单文件部署。

---

## 启动顺序

```
wifi_ap_init()
  ├─ load_params_from_nvs()          // 读 domain/port/wifi/mqtt/apAlways 等
  ├─ esp_wifi_set_config(AP)         // SSID=SN，密码见 AP_PASS
  └─ start_webserver()
        ├─ 注册 / 、/restart 、/restore_defaults
        ├─ wifi_ap_ws_register()     // /ws + 订阅 sensor/meter 事件
        └─ wifi_ap_web_register()
              ├─ pages   → /wsconfig /wsmeter
              ├─ energy  → /wsenergy /api/energy/history
              └─ logs    → /wslogs /api/logs*
```

`/` 会 302 到 `/wsconfig`。STA 接入/断开时通过 `event_bus` 发 `EVENT_AP_STA_*`，供 LED 等观察者使用。

配置页请手动打开：`http://192.168.4.1/wsconfig`（未做强制门户，避免劫持 DNS 干扰 Edge/MSN 等系统访问）。

---

## HTTP 路由一览

| 方法 | 路径                    | 说明                        |
| ---- | ----------------------- | --------------------------- |
| GET  | `/`                   | 重定向到配置页              |
| GET  | `/wsconfig`           | 参数配置（经 WS 读写）      |
| GET  | `/wsmeter`            | 电表实时页（经 WS GetData） |
| GET  | `/wsenergy`           | 历史电量页                  |
| GET  | `/wslogs`             | SD 日志列表页               |
| GET  | `/api/energy/history` | 历史电量 JSON               |
| GET  | `/api/logs`           | 日志文件列表                |
| GET  | `/api/logs/download`  | 下载指定日志                |
| GET  | `/restart`            | 重启设备                    |
| GET  | `/restore_defaults`   | 恢复默认参数并重启          |
| GET  | `/ws`                 | WebSocket 升级入口          |

配置保存**不再走** HTTP 表单 `/save`，统一走 WS `SetConfig`。

---

## WebSocket 协议（`/ws`）

连接：`ws://192.168.4.1/ws`
报文为 JSON，公共字段：`Type` + `params`。

### 客户端 → 设备

| Type          | 作用           | params 示例                              |
| ------------- | -------------- | ---------------------------------------- |
| `GetData`   | 拉取数据       | `{ "get": "Config" }` / `"MeterAll"` |
| `SetConfig` | 写配置并落 NVS | `{ "domain": "…", "port": 1883, … }` |
| `Ping`      | 心跳           | 任意 / 空                                |

### 设备 → 客户端

| Type         | 时机                                     |
| ------------ | ---------------------------------------- |
| `Config`   | 握手成功自动下发；或 GetData 请求 Config |
| `MeterAll` | GetData 请求电表全量（读本地缓存）       |
| `CmdAck`   | SetConfig / 命令确认                     |
| `Error`    | 参数错误等                               |
| `Pong`     | 应答 Ping                                |

握手完成后会立刻推送一帧 `Config`（含 WiFi/MQTT 等字段，当前**无登录校验**）。

配置写入路径：

```
浏览器 SetConfig
  → wifi_ap_ws 解析
  → wifi_ap_config_apply_json()   // 写全局变量 + NVS
  → wifi_ap_config_finish_action() // 可能重连 MQTT 或延时重启
  → CmdAck
```

---

## 与其它模块的关系

```
WIFI_AP
  ├─ BSP/PARAM     NVS 读写（sStorageAp* / sStorageGw*）
  ├─ UTILITY/event_bus   AP 连断事件；订阅 SENSOR/METER 更新
  ├─ WIFI_STA/mqtt       SetConfig 后可能触发重连（由 finish_action）
  └─ SD_FAT              日志列表/下载（经 wifi_ap_web_logs）
```

WS 侧**不直读** `g_sensor_data` / `g_meter_data`，只读自己维护的缓存，降低耦合。

---

## SoftAP 相关注意点

| 项           | 现状                                               |
| ------------ | -------------------------------------------------- |
| SSID         | 设备序列号 SN                                      |
| 密码         | `AP_PASS`，空字符串 = **开放热点**         |
| AP IP        | 固定`192.168.4.1`（IDF 默认）                    |
| `apAlways` | NVS：1 常开 AP；0 按策略（BOOT/超时）              |
| 鉴权         | HTTP/WS **当前无** Basic Auth / Token / HTTPS |

配置请手动打开 `http://192.168.4.1/wsconfig`（未启用强制门户，避免 DNS 劫持干扰电脑上 Edge/MSN 等访问）。

开放热点时，同网段任何人可打开 `/wsconfig` 并经 WS 改参。若需加固，优先：SoftAP WPA2 密码 + HTTP Basic Auth（含 `/ws` 握手）；HTTPS 自签证书在 SoftAP 上收益有限且占 RAM。

---

## 本地内存

`wifi_ap_mem`：大包（WS 收发、日志下载缓冲等）优先 `MALLOC_CAP_SPIRAM`，失败再回退内部堆。httpd 的 `max_open_sockets` 等预算见 `wifi_ap.c` 中 `start_webserver()` 注释（需给 MQTT 等留 socket）。

---

## 快速定位

| 想改…            | 去看                                                       |
| ----------------- | ---------------------------------------------------------- |
| 热点名/密码/信道  | `wifi_ap.c` 顶部宏与 `wifi_ap_init`                    |
| 配置项有哪些字段  | `wifi_ap.c` 的 `config_params[]`                       |
| 配置页 UI         | `wifi_ap_web_pages.c` 内嵌 HTML                          |
| WS 命令处理       | `wifi_ap_ws.c` → `wifi_ap_ws_handle_text`             |
| 加一个新页面      | 新`wifi_ap_web_xxx` + 在 `wifi_ap_web_register` 里注册 |
| 电表/温湿度不刷新 | 查`event_bus` 是否在 publish；WS 是否已 subscribe        |
