# common：编译期配置与版本信息，供 main/APP 共享（无运行时基础设施）
set(srcs
        version.c
        )

set(include_dirs
        .
        )

set(requires
        log
        )

idf_component_register(SRCS ${srcs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})
