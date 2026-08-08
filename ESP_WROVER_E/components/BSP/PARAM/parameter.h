/*
 * @Author: yu.wang
 * @Date: 2025-10-08 18:03:59
 * @LastEditors: yu.wang
 * @LastEditTime: 2026-03-19 17:24:58
 * @Description: 
 */
/**
 ****************************************************************************************************
 * @file        exIT.h
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2023-08-26
 * @brief       外部中断驱动代码
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:正点原子 ESP32-S3 开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 * 
 ****************************************************************************************************
 */

#ifndef __PARAMETER_H_
#define __PARAMETER_H_

#include "esp_err.h"
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_system.h" 
#include "my_log.h"
#include "sdkconfig.h"
#include "nvs_flash.h"
#include "cJSON.h"

#define cNvsName                        "nvs_file"                              //NVS 参数区 :name
#define cNvsKeyParam                    "nvs_key_param"                         //NVS 参数区 :key name

#define cStorageGwNvsName               "gate"                                  //第一级
#define cStorageGwNvsSn                 "sn"                                    //SN
#define cStorageGwNvsDeviceType         "deviceType"                            //设备类型
#define cStorageGwNvsTmpMode            "tmpMode"                               //温度传感器模式
#define cStorageGwNvsMeter485En         "meter485En"                            //485电表使能

#define cStorageGwNvsFormatCnt          "formatCnt"                             //文件系统格式次数
#define cStorageGwNvsDebug              "deBugOn"                               //调试
#define cStorageGwNvsGwWorkMode         "gwWork"                                //网关工作模式
#define cStorageGwNvsGwChargeMode1      "gwChgMode1"                            //1枪网关充电模式
#define cStorageGwNvsGwChargeMode2      "gwChgMode2"                            //2枪网关充电模式


#define cStorageApNvsName               "ap"                                    //第一级
#define cStorageApNvsFlg                "enableFlg"                             //使能标志,0不使能,非0使能
#define cStorageApNvsSsid               "ssid"                                  //热点名称
#define cStorageApNvsPassword           "password"                              //热点密码
#define cStorageApNvsmqttIp             "mqttip"                                    //IP地址
#define cStorageApNvsmqttport           "mqttport"                               //端口
#define cStorageApNvsmqttsub            "mqttsub"                                  //连接固定子码掩码
#define cStorageApNvsmqttclient         "mqttclient"                          //WEB登录密码有效时间(必须U32)
#define cStorageApNvsmqttuser           "mqttuser"                               //WEB申请码
#define cStorageApNvsmqttpasswd         "mqttpasswd"

#define cStorageDataNvsName               "data"                                    //第一级
#define cStorageDataNvsPk1hV             "pk1h_v"                                //1小时功率峰值(W)
#define cStorageDataNvsPk1hT             "pk1h_t"                                //1小时峰值时间戳
#define cStorageDataNvsPk12hV            "pk12h_v"                               //12小时功率峰值(W)
#define cStorageDataNvsPk12hT            "pk12h_t"                               //12小时峰值时间戳
#define cStorageDataNvsPk1dV             "pk1d_v"                                //1天功率峰值(W)
#define cStorageDataNvsPk1dT             "pk1d_t"                                //1天峰值时间戳
#define cStorageDataNvsPk7dV             "pk7d_v"                                //7天功率峰值(W)
#define cStorageDataNvsPk7dT             "pk7d_t"                                //7天峰值时间戳
#define cStorageDataNvsPk1mV             "pk1m_v"                                //1月功率峰值(W)
#define cStorageDataNvsPk1mT             "pk1m_t"                                //1月峰值时间戳
#define cStorageDataNvsEnCnt             "en_cnt"                                //电量历史条目数(区间用电,最多30)
#define cStorageDataNvsEnYd              "en_yd"                                 //兼容旧字段(已弃用)
#define cStorageDataNvsEnE               "en_e"                                  //区间用电量数组(kWh)
#define cStorageDataNvsEnT               "en_t"                                  //区间结束时间戳数组
#define cStorageDataNvsEnBase            "en_base"                               //上次累计电量基准(kWh)
#define cStorageDataNvsEnLts             "en_lts"                                //上次采样时间戳

// #define cStorageNetAppNvsName           "netApp"                               //第一级
// #define cStorageNetAppNvsConnHost       "connHost"                             //连接主机
// #define cStorageNetAppNvsHost1          "host1"                                //主机1
// #define cStorageNetAppNvsHost2          "host2"                                //主机2
// #define cStorageNetAppNvsWhtdEn         "whtdEn"                               //WHT设备使能
// #define cStorageNetAppNvsWhtdHost       "whtdHost"                             //WHT设备主机
// #define cStorageNetAppNvsProtocol       "protocol"                             //协议
// #define cStorageNetAppNvsIp             "ip"                                   //IP地址
// #define cStorageNetAppNvsPort           "port"                                 //端口
// #define cStorageNetAppNvsPath           "path"                                 //路径
// #define cStorageNetAppNvsInfo           "info"  



// 参数类型枚举
typedef enum {
    PARAM_TYPE_STRING,    // 字符串类型
    PARAM_TYPE_INT,       // 整数类型
    PARAM_TYPE_UINT32,    // 无符号32位整数
    PARAM_TYPE_UINT16,    // 无符号16位整数
    PARAM_TYPE_UINT8,     // 无符号8位整数
    PARAM_TYPE_FLOAT      // 浮点数类型
} eParamType_t;

 

typedef enum 
{
    
    cStorageApCmdGwNvsSn,                                                      //网关SN
    cStorageApCmdGwNvsDeviceType,                                                  //网关设备类型
    cStorageApCmdTmpMode,                                                       //温度传感器模式
    cStorageApCmdMeter485En,                                                    //485电表使能

    
    cStorageApCmdFlg,                                                       //使能标志
    cStorageApCmdSsid,                                                          //热点名称
    cStorageApCmdPassword,                                                      //热点密码
    cStorageApCmdNvsmqttIp,                                                            //IP地址
    cStorageApCmdNvsmqttport,                                                       //默认网关地址
    cStorageApCmdNvsmqttsub,                                                          //连接固定子码掩码
    cStorageApCmdNvsmqttclient,                                                  //WEB密码有效时间(4个字节时间戳)
    cStorageApCmdNvsmqttuser,                                                       //WEB申请码
    cStorageApCmdNvsmqttpasswd,                                                   //WEB登录密码
  

    cStorageApCmdPk1hV,                                                         //1小时功率峰值
    cStorageApCmdPk1hT,                                                         //1小时峰值时间戳
    cStorageApCmdPk12hV,                                                        //12小时功率峰值
    cStorageApCmdPk12hT,                                                        //12小时峰值时间戳
    cStorageApCmdPk1dV,                                                         //1天功率峰值
    cStorageApCmdPk1dT,                                                         //1天峰值时间戳
    cStorageApCmdPk7dV,                                                         //7天功率峰值
    cStorageApCmdPk7dT,                                                         //7天峰值时间戳
    cStorageApCmdPk1mV,                                                         //1月功率峰值
    cStorageApCmdPk1mT,                                                         //1月峰值时间戳
    cStorageApCmdEnCnt,                                                         //电量历史条目数
    cStorageApCmdEnYd,                                                          //兼容旧字段(已弃用)
    cStorageApCmdEnE,                                                           //区间用电量数组
    cStorageApCmdEnT,                                                           //区间时间戳数组
    cStorageApCmdEnBase,                                                        //上次累计电量基准
    cStorageApCmdEnLts,                                                         //上次采样时间戳

    eStorageApCmdMax
}__attribute__((packed)) eStorageApCmd_t;

typedef enum 
{
    eStorageApRstFail                   = -7,                                   //操作/结果失败或不生效
    eStorageApRstWriteParamErr          = -6,                                   //写参数错误
    eStorageApRstReadParamErr           = -5,                                   //读参数错误
    eStorageApRstParamErr               = -4,                                   //参数错误
    eStorageApRstObjOut                 = -3,                                   //对象超出
    eStorageApRstObjNo                  = -2,                                   //对象无(删除到底或者为0了)
    eStorageApRstObjNull                = -1,                                   //对象为空
    eStorageApRstSuccess                = 0,
    
}__attribute__((packed)) eStorageApRst_t;


//网关NVS配置JSON文件默认内容格式 弃用
//1.标准原始数据
//2.ATE中用到数据(因为过ATE时会清除恢复默认数据)

// static const char *pNvsKeyParamDefault = 
// "{"
//     "\"" cStorageGwNvsName "\" :"
//     "{"
//         "\"" cStorageGwNvsSn "\" : \"12345678900001,"
//         "\"" cStorageGwNvsDeviceType "\" : 0"
//         // "\"" cStorageGwNvsFormatCnt "\" : 0,"
//         // "\"" cStorageGwNvsDebug "\" : 0,"
//         // "\"" cStorageGwNvsGwWorkMode "\" : 0,"
//         // "\"" cStorageGwNvsGwChargeMode1 "\" : 1,"
//         // "\"" cStorageGwNvsGwChargeMode2 "\" : 1"
//     "},"
//     "\"" cStorageApNvsName "\" :"
//     "{"
//         "\"" cStorageApNvsFlg "\" : 0,"
//         "\"" cStorageApNvsSsid "\" : \"WiFiName\","
//         "\"" cStorageApNvsPassword "\" : \"admin123\","
//         "\"" cStorageApNvsmqttIp "\" : \"192.168.4.1\","
//         "\"" cStorageApNvsmqttport "\" : 1883,"
//         "\"" cStorageApNvsmqttsub "\" : \"sub\","
//         "\"" cStorageApNvsmqttclient "\" : \"client\","
//         "\"" cStorageApNvsmqttuser "\" : \"tuser\","
//         "\"" cStorageApNvsmqttpasswd "\" : \"passwd\""
//     "}"
//     // "\"netApp\" :"
//     // "{"
//     //     "\"" cStorageNetAppNvsConnHost "\" : \"host1\"," 
//     //     "\"" cStorageNetAppNvsHost1 "\" : { \"" cStorageNetAppNvsProtocol "\" : \"tcp\", \"" cStorageNetAppNvsIp "\" : \"sp.en-plus.cn\", \"" cStorageNetAppNvsPort "\" : 17746, \"" cStorageNetAppNvsPath "\" : \"null\", \"" cStorageNetAppNvsInfo "\" : {} },"
//     //     "\"" cStorageNetAppNvsHost2 "\" : { \"" cStorageNetAppNvsProtocol "\" : \"tcp\", \"" cStorageNetAppNvsIp "\" : \"sp.en-plus.cn\", \"" cStorageNetAppNvsPort "\" : 17746, \"" cStorageNetAppNvsPath "\" : \"null\", \"" cStorageNetAppNvsInfo "\" : {} },"
//     //     "\"" cStorageNetAppNvsWhtdEn "\" : 0,"
//     //     "\"" cStorageNetAppNvsWhtdHost "\" : { \"" cStorageNetAppNvsIp "\" : \"dev.en-plus.cn\", \"" cStorageNetAppNvsPort "\" : 18841 }"
//     // "}"
// "}";




//nvs 参数组件 缓存结构
typedef struct
{
    cJSON                               *pJsonParam;                            //NVS 参数区 对应的JSON对象
    SemaphoreHandle_t                   hMutex;                                 //该JSON对象的保护锁
}stNvsCache_t;



// 参数配置结构体
typedef struct {
    eStorageApCmd_t  eCmd;           // 命令枚举值
    const char*      pParamName;     // NVS中的参数名
    eParamType_t     eType;          // 参数类型
    const char*      pDefaultValue;  // 默认值（字符串形式）
    const char*      pGroupName;     // 所属分组（"gate" 或 "ap"）
    uint8_t          rebootAction;   // 保存后行为: 0=不操作, 1=重连网络, 2=重启设备
} stParamConfig_t;




// 声明参数配置数组
extern const stParamConfig_t g_stParamConfig[];
extern const int g_iParamCount;





bool NVS_init(void);
bool sNvsParamUnlock(void);
bool sNvsParamLock(void);
bool sNvsParamSet(bool printfen);
cJSON *sNvsParamGet(void);
bool sNvsParamPrint(void);
char* generateDefaultJsonString(void);
bool sNvsParamRestoreDefaults(void);
bool sNvsParamCleanUnused(void);
#endif