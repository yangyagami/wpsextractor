cmake_minimum_required(VERSION 3.10)
project(libwpsextract VERSION 0.1.0 LANGUAGES C)

# ================================================================
# 构建选项
# ================================================================
option(BUILD_STATIC_LIBS "Build static library" ON)
option(BUILD_SHARED_LIBS "Build shared library" ON)
option(BUILD_EXAMPLE    "Build example program" ON)

# ================================================================
# 源码 / 头文件列表
# ================================================================
set(LIB_SOURCES
    src/wpsextract.c
    src/zip_reader.c
    src/xml_parser.c
    src/format_detect.c
    src/wps_text.c
    src/wps_binary.c
    src/et_table.c
    src/et_binary.c
    src/ole2_reader.c
    src/strbuf.c
)

set(PUBLIC_HEADER  include/wpsextract.h)
set(PRIVATE_HEADER src/internal.h)

# ================================================================
# 辅助函数：对目标应用通用编译设置（target 级别）
# ================================================================
function(_apply_target_options target)
    target_include_directories(${target}
        PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_compile_options(${target} PRIVATE
        -std=c99
        -Wall -Wextra
    )
    target_compile_definitions(${target} PRIVATE
        _POSIX_C_SOURCE=200809L
    )
    # Debug / Release 差异化（全部 target 级别，无全局设置）
    target_compile_options(${target} PRIVATE
        $<$<CONFIG:Debug>:-g -O0>
    )
    target_compile_options(${target} PRIVATE
        $<$<CONFIG:Release>:-O2>
    )
    target_compile_definitions(${target} PRIVATE
        $<$<CONFIG:Release>:NDEBUG>
    )
    target_compile_definitions(${target} PRIVATE
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
    )
endfunction()

# ================================================================
# 静态库：libwpsextract.a
# ================================================================
if(BUILD_STATIC_LIBS)
    add_library(wpsextract_static STATIC ${LIB_SOURCES} ${PUBLIC_HEADER} ${PRIVATE_HEADER})
    _apply_target_options(wpsextract_static)
    target_link_libraries(wpsextract_static PRIVATE z)
    set_target_properties(wpsextract_static PROPERTIES
        OUTPUT_NAME                 wpsextract
        PUBLIC_HEADER              "${PUBLIC_HEADER}"
        C_STANDARD                 99
        C_STANDARD_REQUIRED        ON
        POSITION_INDEPENDENT_CODE  ON
    )
endif()

# ================================================================
# 共享库：libwpsextract.so
# ================================================================
if(BUILD_SHARED_LIBS)
    add_library(wpsextract_shared SHARED ${LIB_SOURCES} ${PUBLIC_HEADER} ${PRIVATE_HEADER})
    _apply_target_options(wpsextract_shared)
    target_link_libraries(wpsextract_shared PRIVATE z)
    set_target_properties(wpsextract_shared PROPERTIES
        OUTPUT_NAME                wpsextract
        PUBLIC_HEADER             "${PUBLIC_HEADER}"
        C_STANDARD                99
        C_STANDARD_REQUIRED       ON
    )
endif()

# ================================================================
# 示例程序
# ================================================================
if(BUILD_EXAMPLE)
    add_executable(extract examples/extract.c)
    target_include_directories(extract PRIVATE include)
    target_compile_options(extract PRIVATE
        -std=c99
        -Wall -Wextra
    )
    target_compile_definitions(extract PRIVATE
        _POSIX_C_SOURCE=200809L
    )
    target_compile_options(extract PRIVATE
        $<$<CONFIG:Debug>:-g -O0>
    )
    target_compile_options(extract PRIVATE
        $<$<CONFIG:Release>:-O2>
    )
    target_compile_definitions(extract PRIVATE
        $<$<CONFIG:Release>:NDEBUG>
    )
    target_compile_definitions(extract PRIVATE
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
    )

    # 链接静态库（与原始 Makefile 行为一致）
    if(TARGET wpsextract_static)
        target_link_libraries(extract PRIVATE wpsextract_static z)
    else()
        target_link_libraries(extract PRIVATE wpsextract_shared z)
    endif()
endif()

# ================================================================
# 安装规则（target 级别）
# ================================================================
include(GNUInstallDirs)

if(BUILD_STATIC_LIBS)
    install(TARGETS wpsextract_static
        ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif()

if(BUILD_SHARED_LIBS)
    install(TARGETS wpsextract_shared
        LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif()

# ================================================================
# 测试占位
# ================================================================
enable_testing()
# TODO: 后续添加测试
