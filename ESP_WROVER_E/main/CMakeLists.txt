# SRCS "main.c" 把 main.c 添加到组件中
# INCLUDE_DIRS "." REQUIRES BSP APP common 把 main.c 的依赖添加到组件中
idf_component_register(SRCS "main.c" INCLUDE_DIRS "." REQUIRES BSP APP common UTILITY)