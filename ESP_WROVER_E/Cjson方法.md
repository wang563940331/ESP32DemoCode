## 创建 / 解析 JSON

### 1.1 从字符串解析 JSON

```
cJSON *cJSON_Parse(const char *value);
```

* **作用** ：把一段 JSON 字符串 → 转成 cJSON 结构体
* **返回** ：成功返回根节点，失败返回 NULL
* **你项目里正在用这个！**

---

### 1.2 释放 JSON 树（必须调用！）

```
void cJSON_Delete(cJSON *item);
```

* **作用** ：释放整个 JSON 树的内存
* **注意** ：**不调用会内存泄漏**
* 只要 `cJSON_Parse` 成功，最后一定要 `cJSON_Delete`

---

## 2. 从 JSON 中取值（你最常用）

### 2.1 根据 key 获取子节点

```
cJSON *cJSON_GetObjectItem(cJSON *object, const char *string);
```

* **作用** ：从 JSON 对象里根据 key 取值
* ```
  cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
  ```

---

### 2.2 判断 key 是否存在

```
int cJSON_HasObjectItem(cJSON *object, const char *string);
```

* 返回 1 = 存在
* 返回 0 = 不存在

---

### 2.3 获取类型

```
int cJSON_IsString(const cJSON *item);
int cJSON_IsNumber(const cJSON *item);
int cJSON_IsObject(const cJSON *item);
int cJSON_IsArray(const cJSON *item);
int cJSON_IsBool(const cJSON *item);
int cJSON_IsNull(const cJSON *item);
```


### 2.4 获取真实值

```
char*  cJSON_GetStringValue(cJSON *item);
double cJSON_GetNumberValue(cJSON *item);
int    cJSON_GetBoolValue(cJSON *item);
```



{
    "name":"ESP32",
    "status":1,
    "temperature":25.6
}

// 1. 先获取节点
cJSON *name = cJSON_GetObjectItem(root, "name");

// 2. 判断是不是字符串
if(cJSON_IsString(name))
{
    // 3. 获取真实字符串
    char *str_val = cJSON_GetStringValue(name);
    printf("name = %s\n", str_val);  // 输出：ESP32
}

cJSON *temp = cJSON_GetObjectItem(root, "temperature");

if(cJSON_IsNumber(temp))
{
    double num_val = cJSON_GetNumberValue(temp);
    printf("温度 = %.1f\n", num_val);  // 输出 25.6
}

cJSON *status = cJSON_GetObjectItem(root, "status");

if(cJSON_IsBool(status))
{
    int bool_val = cJSON_GetBoolValue(status);
    printf("状态 = %d\n", bool_val);  // 输出 1
}

---

## 3. 创建 JSON（构造 JSON）

### 3.1 创建根对象

```
cJSON *cJSON_CreateObject(void);
```

---

### 3.2 添加键值对到对象


```
void cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item);
```

快捷函数（最常用）：

```
void cJSON_AddStringToObject(cJSON *object, const char *key, const char *value);
void cJSON_AddNumberToObject(cJSON *object, const char *key, double value);
void cJSON_AddBoolToObject(cJSON *object, const char *key, int value);
void cJSON_AddNullToObject(cJSON *object, const char *key);
```

---

## 4. JSON → 字符串

### 4.1 格式化输出（带换行缩进）

```
char *cJSON_Print(cJSON *item);
```

### 4.2 压缩输出（无空格，适合传输）


```
char *cJSON_PrintUnformatted(cJSON *item);
```

 **注意** ：返回的字符串需要 `free()`！

---

## 5. 数组操作

### 5.1 创建数组

```
cJSON *cJSON_CreateArray(void);
```

### 5.2 添加到数组

```
void cJSON_AddItemToArray(cJSON *array, cJSON *item);
```

### 5.3 获取数组长度

```
int cJSON_GetArraySize(cJSON *array);
```

### 5.4 根据下标取数组元素

```
cJSON *cJSON_GetArrayItem(cJSON *array, int index);
```



# JSON 数组到底有什么用？（超级直白版）

我用**你做 ESP32 + MQTT 物联网开发**最常遇到的场景告诉你， **数组不是没用的语法，是你以后必用的核心功能** ！

## 一句话总结作用

**数组 = 一次性打包 一组数据**

不用发 10 次 MQTT，发 **1 次**就够了！

---

# 1. 你最常用的场景：一次性上报多个设备状态

你现在的设备可能不止一个，比如：

* 灯 1
* 灯 2
* 插座
* 温湿度传感器

如果不用数组，你要发 4 次 MQTT：

plaintext

```
{"name":"灯1","status":1}
{"name":"灯2","status":0}
{"name":"插座","status":1}
{"name":"温湿度","temp":25}
```

用数组， **只发 1 次** ：

json

```
{
  "deviceList": [
    {"name":"灯1","status":1},
    {"name":"灯2","status":0},
    {"name":"插座","status":1},
    {"name":"温湿度","temp":25}
  ]
}
```

✅ **优点：省流量、省电量、MQTT 不拥堵**

---

# 2. 服务器下发多条命令

APP / 云端要同时控制多个设备：

json

```
{
  "commands": [
    {"cmd":"light1","action":"on"},
    {"cmd":"light2","action":"off"},
    {"cmd":"fan","action":"on"}
  ]
}
```

ESP32 解析数组 → **循环执行所有命令**

不用数组，服务器要发 N 条指令，很容易乱序。

---

# 3. 存储一组列表（NVS 里也能用）

例如：

* 历史温度
* 历史时间
* 多个 WiFi 名称
* 多个定时任务

json

```
{
  "temp_history": [26,27,25,28,30]
}
```

不用数组，你要存 5 个键值对，麻烦死。

---

# 4. 批量上传数据（比如传感器缓存上报）

ESP32 每秒采一次温湿度，存 10 组再上报：

json

```
{
  "data": [
    {"time":"10:00","temp":25},
    {"time":"10:01","temp":26},
    {"time":"10:02","temp":25}
  ]
}
```

✅  **大大节省电量** （不用频繁联网）

---

# 5. 最简单的比喻

* **JSON 对象 {} = 一个物品的信息**

  例如：一个设备
* **JSON 数组 [] = 一列表物品**

  例如：**设备列表、命令列表、数据列表**

---

# 最终总结（你一定要记住）

## 数组的作用：

### **一次传输 一组数据、一批设备、一组命令、一串记录**

### **让 MQTT 通信更简洁、更快、更省资源**

---

# 6. 错误处理

### 6.1 获取解析失败原因

```
const char *cJSON_GetErrorPtr(void);
```
