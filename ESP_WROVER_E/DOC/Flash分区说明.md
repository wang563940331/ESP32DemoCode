# Flash 分区说明

本文档描述当前工程（ESP32-WROVER-E，**8MB** Flash）的分区布局、用途，以及固件占用与剩余容量评估。

分区定义文件：[`partitions.csv`](../partitions.csv)  
配置项：`CONFIG_PARTITION_TABLE_CUSTOM=y`，`CONFIG_ESPTOOLPY_FLASHSIZE=8MB`

---

## 1. 整体布局（地址从低到高）

| 区域 | Offset | Size | 说明 |
|------|--------|------|------|
| Bootloader | `0x1000` | ~28 KB | 二阶段引导（不在 `partitions.csv` 中） |
| Partition Table | `0x8000` | 4 KB | 分区表本体 |
| **nvs** | `0x9000` | 24 KB (`0x6000`) | WiFi/业务 NVS 键值 |
| **otadata** | `0xF000` | 8 KB (`0x2000`) | OTA 启动槽位（下次从 ota_0 还是 ota_1） |
| **phy_init** | `0x11000` | 4 KB (`0x1000`) | RF PHY 校准数据 |
| *(对齐空隙)* | `0x12000` ~ `0x1FFFF` | 56 KB | 对齐到 `0x20000`，当前未单独建分区 |
| **ota_0** | `0x20000` | **3 MB** (`0x300000`) | 应用槽 A |
| **ota_1** | `0x320000` | **3 MB** (`0x300000`) | 应用槽 B |
| *(Flash 尾部空闲)* | `0x620000` ~ `0x7FFFFF` | **约 1.875 MB** | 未划分，可后续扩展 |

示意：

```
0x000000  ┌─────────────────┐
          │  (保留/引导头)   │
0x001000  ├─────────────────┤ Bootloader
0x008000  ├─────────────────┤ Partition Table
0x009000  ├─────────────────┤ nvs          24KB
0x00F000  ├─────────────────┤ otadata       8KB
0x011000  ├─────────────────┤ phy_init      4KB
0x012000  ├─────────────────┤ (gap)        56KB
0x020000  ├─────────────────┤ ota_0         3MB  ← 应用槽 A
0x320000  ├─────────────────┤ ota_1         3MB  ← 应用槽 B
0x620000  ├─────────────────┤ 空闲       ~1.875MB
0x800000  └─────────────────┘ 8MB Flash 结束
```

---

## 2. 各分区详细说明

### 2.1 nvs（`data/nvs`）

- **偏移**：`0x9000`，**大小**：24 KB  
- **用途**：WiFi 凭据、MQTT/AP/网关参数（`parameter`/`parameterSet`）、设备 SN 等  
- **注意**：擦除 NVS 会丢配网与业务参数；OTA **不会**自动清 NVS  

### 2.2 otadata（`data/ota`）

- **偏移**：`0xF000`，**大小**：8 KB  
- **用途**：记录当前/下次启动的 OTA 槽（`ota_0` / `ota_1`）  
- **写入时机**：`esp_ota_set_boot_partition()` 成功后  

### 2.3 phy_init（`data/phy`）

- **偏移**：`0x11000`，**大小**：4 KB  
- **用途**：射频 PHY 初始化/校准数据  

### 2.4 ota_0 / ota_1（`app/ota_0`、`app/ota_1`）

- **偏移**：`0x20000` / `0x320000`  
- **大小**：各 **3 MB**（`0x300000` = 3145728 字节）  
- **用途**：双备份应用固件；运行在一块时，OTA 写入另一块，校验通过后切槽重启  
- **业务入口**：MQTT `FtpOTA` → FTP 流式下载 → `esp_ota_write`（无串口 update）

首次串口烧录一般写入 `ota_0`（见 `idf.py flash` 的 offset `0x20000`）。  
日志中若出现 `Loaded app from partition at offset 0x320000`，说明当前已从 **ota_1** 启动。

---

## 3. 当前固件占用与剩余容量评估

统计基准（本机一次成功构建，数字随版本变化）：

| 项目 | 数值 |
|------|------|
| Flash 总容量 | 8 MB = 8388608 字节 |
| 应用分区上限（单槽） | **3 MB** = 3145728 字节 |
| 当前 `RemoteControlO_Com.bin`（参考） | 约 **1.3 MB** 量级 |
| 单槽剩余（参考） | 约 **1.7 MB** 以上 |
| 单槽已用比例（参考） | 约 **45%** 以内 |

### 3.1 应用分区结论

| 评估项 | 结论 |
|--------|------|
| 能否继续 OTA | 可以；单槽 3 MB，余量相对充裕 |
| 风险 | 短期内不易顶满；仍建议控制无谓膨胀 |
| 改分区注意 | 从旧 1.5 MB 布局迁到 3 MB 后，**必须全量重烧分区表 + otadata + 双槽** |

### 3.2 整片 Flash 结论

| 区域 | 占用 |
|------|------|
| Bootloader + 表 + nvs + otadata + phy + gap | ~120 KB 量级 |
| ota_0 + ota_1 | **6.0 MB** |
| **未使用尾部** | **约 1.875 MB**（`0x620000` 起） |

尾部仍可规划小容量 SPI Flash 存储；大日志继续放 **SD 卡**。

---

## 4. 烧录与 OTA 注意点

1. **改分区表后**务必重烧分区表（及 bootloader/otadata），不要只烧 app；建议 `idf.py erase-flash` 后再全量烧录。  
2. 手动 `./otapush.sh` 上传为 `RemoteControlO<版本>.bin`；设备侧经 MQTT `FtpOTA` 写入**非当前运行槽**。  
3. NVS 在 `0x9000`，与应用槽分离，正常 OTA **保留配置**。  
4. 日志与大文件在 **SD 卡**，不占用上述 Flash 应用分区。

---

## 5. 相关文件

| 文件 | 作用 |
|------|------|
| `partitions.csv` | 自定义分区表 |
| `components/APP/OTA/OTA.c` | FTP + `esp_ota_*` 升级 |
| `otapush.sh` | 把 bin 推到 FTP（凭据走环境变量 / `.env`） |
| `sdkconfig` | `FLASHSIZE=8MB`、自定义分区表开关 |

---

*文档生成时 bin 大小以当时构建产物为准；重新 `idf.py build` 后请用实际 `RemoteControlO_Com.bin` 字节数更新第 3 节数字。*
