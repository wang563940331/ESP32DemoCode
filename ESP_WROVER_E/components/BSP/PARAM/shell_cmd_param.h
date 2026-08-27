#ifndef __SHELL_CMD_PARAM_H_
#define __SHELL_CMD_PARAM_H_

#include <stdbool.h>

/**
 * @brief 注册 PARAM 模块相关的 Shell 命令（cleardata / read parm）
 * @note 在 NVS_init 成功后调用，命令全局生效
 * @return true 全部注册成功，false 有失败
 */
bool shell_cmd_param_register(void);

#endif
