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
#include "json.h"
#include "cJSON.h"
#include "utility.h"
#define cNvsName                        "nvs_file"                              //NVS 参数区 :name
#define cNvsKeyParam                    "nvs_key_param"                         //NVS 参数区 :key name

#define cStorageGwNvsName               "gate"                                  //第一级
#define cStorageGwNvsSn                 "sn"                                    //SN
#define cStorageGwNvsDeviceType         "deviceType"                            //设备类型
#define cStorageGwNvsFormatCnt          "formatCnt"                             //文件系统格式次数
#define cStorageGwNvsDebug              "deBugOn"                               //调试
#define cStorageGwNvsGwWorkMode         "gwWork"                                //网关工作模式
#define cStorageGwNvsGwChargeMode1      "gwChgMode1"                            //1枪网关充电模式
#define cStorageGwNvsGwChargeMode2      "gwChgMode2"                            //2枪网关充电模式
     

#define cStorageApNvsName               "ap"                                    //第一级
#define cStorageApNvsFlg                "enableFlg"                             //使能标志,0不使能,非0使能
#define cStorageApNvsSsid               "ssid"                                  //热点名称
#define cStorageApNvsPassword           "password"                              //热点密码
#define cStorageApNvsIp                 "ip"                                    //IP地址
#define cStorageApNvsDefGwIp            "defGwIp"                               //默认网关地址
#define cStorageApNvsMask               "mask"                                  //连接固定子码掩码
#define cStorageApNvsValidityTime       "validityTime"                          //WEB登录密码有效时间(必须U32)
#define cStorageApNvsReqCode            "reqCode"                               //WEB申请码
#define cStorageApNvsWebPassword        "webPassword"  

#define cStorageNetAppNvsName           "netApp"                               //第一级
#define cStorageNetAppNvsConnHost       "connHost"                             //连接主机
#define cStorageNetAppNvsHost1          "host1"                                //主机1
#define cStorageNetAppNvsHost2          "host2"                                //主机2
#define cStorageNetAppNvsWhtdEn         "whtdEn"                               //WHT设备使能
#define cStorageNetAppNvsWhtdHost       "whtdHost"                             //WHT设备主机
#define cStorageNetAppNvsProtocol       "protocol"                             //协议
#define cStorageNetAppNvsIp             "ip"                                   //IP地址
#define cStorageNetAppNvsPort           "port"                                 //端口
#define cStorageNetAppNvsPath           "path"                                 //路径
#define cStorageNetAppNvsInfo           "info"  

//网关NVS配置JSON文件默认内容格式
//1.标准原始数据
//2.ATE中用到数据(因为过ATE时会清除恢复默认数据)
static const char *pNvsKeyParamDefault = 
"{"
    "\"" cStorageGwNvsName "\" :"
    "{"
        "\"" cStorageGwNvsSn "\" : \"SN00000000000000\","
        "\"" cStorageGwNvsDeviceType "\" : 94,"
        "\"" cStorageGwNvsFormatCnt "\" : 0,"
        "\"" cStorageGwNvsDebug "\" : 0,"
        "\"" cStorageGwNvsGwWorkMode "\" : 0,"
        "\"" cStorageGwNvsGwChargeMode1 "\" : 1,"
        "\"" cStorageGwNvsGwChargeMode2 "\" : 1"
    "},"
    "\"" cStorageApNvsName "\" :"
    "{"
        "\"" cStorageApNvsFlg "\" : 0,"
        "\"" cStorageApNvsSsid "\" : \"SN00000000000000\","
        "\"" cStorageApNvsPassword "\" : \"admin123\","
        "\"" cStorageApNvsIp "\" : \"192.168.4.1\","
        "\"" cStorageApNvsDefGwIp "\" : \"192.168.4.1\","
        "\"" cStorageApNvsMask "\" : \"255.255.255.0\","
        "\"" cStorageApNvsValidityTime "\" : 0,"
        "\"" cStorageApNvsReqCode "\" : \"null\","
        "\"" cStorageApNvsWebPassword "\" : \"123456\""
    "},"
    "\"netApp\" :"
    "{"
        "\"" cStorageNetAppNvsConnHost "\" : \"host1\"," 
        "\"" cStorageNetAppNvsHost1 "\" : { \"" cStorageNetAppNvsProtocol "\" : \"tcp\", \"" cStorageNetAppNvsIp "\" : \"sp.en-plus.cn\", \"" cStorageNetAppNvsPort "\" : 17746, \"" cStorageNetAppNvsPath "\" : \"null\", \"" cStorageNetAppNvsInfo "\" : {} },"
        "\"" cStorageNetAppNvsHost2 "\" : { \"" cStorageNetAppNvsProtocol "\" : \"tcp\", \"" cStorageNetAppNvsIp "\" : \"sp.en-plus.cn\", \"" cStorageNetAppNvsPort "\" : 17746, \"" cStorageNetAppNvsPath "\" : \"null\", \"" cStorageNetAppNvsInfo "\" : {} },"
        "\"" cStorageNetAppNvsWhtdEn "\" : 0,"
        "\"" cStorageNetAppNvsWhtdHost "\" : { \"" cStorageNetAppNvsIp "\" : \"dev.en-plus.cn\", \"" cStorageNetAppNvsPort "\" : 18841 }"
    "}"
"}";


 

typedef enum 
{
    eStorageApCmdFlg                    = 0,                                    //使能标志
    eStorageApCmdSsid,                                                          //热点名称
    eStorageApCmdPassword,                                                      //热点密码
    eStorageApCmdIp,                                                            //IP地址
    eStorageApCmdDefGwIp,                                                       //默认网关地址
    eStorageApCmdMask,                                                          //连接固定子码掩码
    eStorageApCmdValidityTime,                                                  //WEB密码有效时间(4个字节时间戳)
    eStorageApCmdReqCode,                                                       //WEB申请码
    eStorageApCmdWebPassword,                                                   //WEB登录密码
    
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

typedef struct
{
    char SSID[24];
    char Passwd[24];
    char MqttIP[24];
    uint16_t MqttPort;
}ST_SYSPARAM;


//nvs 参数组件 缓存结构
typedef struct
{
    cJSON                               *pJsonParam;                            //NVS 参数区 对应的JSON对象
    SemaphoreHandle_t                   hMutex;                                 //该JSON对象的保护锁
}stNvsCache_t;




bool NVS_init(void);
bool sNvsParamUnlock(void);
bool sNvsParamLock(void);
bool sNvsParamSet(void);
cJSON *sNvsParamGet(void);
#endif
