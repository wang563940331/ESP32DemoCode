#ifndef __SHELL_CMD_LOG_H_
#define __SHELL_CMD_LOG_H_

#include <stdbool.h>

/**
 * @brief 注册 SD 日志模块相关的 Shell 命令（clearlog / readsd）
 * @note 在 sd_fat_log_task_init 成功后调用
 * @return true 全部注册成功，false 有失败
 */
bool shell_cmd_log_register(void);

#endif
