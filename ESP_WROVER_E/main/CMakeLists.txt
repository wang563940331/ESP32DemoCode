# SRCS "main.c" 把 main.c 添加到组件中
# INCLUDE_DIRS "." REQUIRES BSP APP common 把 main.c 的依赖添加到组件中
# main 依赖 BSP/APP/中间件（FTP 等）及公共配置
idf_component_register(SRCS "main.c" INCLUDE_DIRS "." REQUIRES BSP APP common UTILITY MIDDLEWARE)