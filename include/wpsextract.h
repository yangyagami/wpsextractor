/*
 * libwpsextract - WPS Office 文件内容提取库
 * 公开头文件
 *
 * Copyright (c) 2026
 * Licensed under MIT License
 */

#ifndef WPSEXTRACT_H
#define WPSEXTRACT_H

#include <stddef.h>   /* size_t */
#include <stdint.h>   /* uint8_t, uint16_t, uint32_t */

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 版本信息
 * ================================================================ */

#define WPSEXT_VERSION_MAJOR 0
#define WPSEXT_VERSION_MINOR 1
#define WPSEXT_VERSION_PATCH 0

/**
 * @brief 获取版本字符串（格式: "MAJOR.MINOR.PATCH"）
 */
const char *wpsext_version(void);

/* ================================================================
 * 错误码
 * ================================================================ */

typedef enum {
    WPSEXT_OK               =  0,
    WPSEXT_ERR_FILE         = -1,   /* 文件打开/读取失败 */
    WPSEXT_ERR_FORMAT       = -2,   /* 格式不支持或文件损坏 */
    WPSEXT_ERR_MEMORY       = -3,   /* 内存分配失败 */
    WPSEXT_ERR_INTERNAL     = -4,   /* 内部错误 */
    WPSEXT_ERR_INVALID_ARG  = -5,   /* 无效参数 */
    WPSEXT_ERR_TOO_LARGE    = -6    /* 文件超过大小限制 */
} wpsext_error_t;

/**
 * @brief 获取错误码对应的描述字符串
 * @param[in] errcode  错误码
 * @return 错误描述字符串（静态存储，不可释放）
 */
const char *wpsext_strerror(int errcode);

/* ================================================================
 * 文件类型
 * ================================================================ */

typedef enum {
    WPSEXT_TYPE_UNKNOWN = 0,
    WPSEXT_TYPE_WPS     = 1,
    WPSEXT_TYPE_ET      = 2,
    WPSEXT_TYPE_DPS     = 3
} wpsext_filetype_t;

/* ================================================================
 * 提取上下文
 * ================================================================ */

/** 不透明上下文句柄 */
typedef struct wpsext_ctx wpsext_ctx_t;

/** 提取选项 */
typedef struct {
    size_t          max_file_size;   /* 0 = 无限制 */
    size_t          max_output_size; /* 0 = 无限制 */
    int             include_headers; /* 是否包含页眉页脚 */
    int             structured;      /* 是否结构化输出（预留） */
} wpsext_options_t;

/**
 * @brief 创建提取上下文
 * @param[in]  opts  提取选项，NULL 表示使用默认值
 * @param[out] ctx   返回上下文句柄
 * @return WPSEXT_OK 成功，WPSEXT_ERR_* 失败
 */
int wpsext_ctx_create(const wpsext_options_t *opts, wpsext_ctx_t **ctx);

/**
 * @brief 销毁上下文，释放关联资源
 * @param[in] ctx  上下文句柄，可为 NULL（无操作）
 */
void wpsext_ctx_destroy(wpsext_ctx_t *ctx);

/* ================================================================
 * 库初始化/清理
 * ================================================================ */

/**
 * @brief 初始化库，必须在其他 API 之前调用
 * @return WPSEXT_OK 成功，WPSEXT_ERR_INTERNAL 失败
 */
int wpsext_init(void);

/**
 * @brief 释放库资源，调用后可以再次 wpsext_init()
 */
void wpsext_cleanup(void);

/* ================================================================
 * 文件类型检测
 * ================================================================ */

/**
 * @brief 检测 WPS 文件类型
 * @param[in]  path  文件路径
 * @param[out] type  返回文件类型
 * @return WPSEXT_OK 成功，WPSEXT_ERR_* 失败
 */
int wpsext_detect_type(const char *path, wpsext_filetype_t *type);

/* ================================================================
 * 文本提取（一次性）
 * ================================================================ */

/**
 * @brief 从 WPS 文件一次性提取全部文本
 * @param[in]  ctx      上下文句柄，NULL 表示使用默认设置
 * @param[in]  path     文件路径
 * @param[out] out_text 返回提取的文本（调用方负责释放）
 * @param[out] out_len  返回文本长度（不含终止符），可为 NULL
 * @return WPSEXT_OK 成功，WPSEXT_ERR_* 失败
 *
 * 行为约定：
 *   - 返回的文本为 UTF-8 编码，以 '\0' 结尾
 *   - 段落之间使用 '\n' 分隔
 *   - 表格单元格之间使用 '\t' 分隔，行之间使用 '\n'
 *   - 幻灯片之间使用 "\n---\n" 分隔
 *   - 调用方必须通过 wpsext_free_text() 释放 out_text
 */
int wpsext_extract_file(wpsext_ctx_t *ctx,
                        const char *path,
                        char **out_text,
                        size_t *out_len);

/**
 * @brief 释放 wpsext_extract_file() 返回的文本
 * @param[in] text  待释放的文本指针，可为 NULL
 */
void wpsext_free_text(char *text);

/* ================================================================
 * 文本提取（流式/回调）
 * ================================================================ */

/**
 * @brief 回调函数类型：每提取到一段文本即调用
 * @param[in] chunk    文本块（可能不完整，不以 '\0' 结尾）
 * @param[in] len      文本块字节长度
 * @param[in] userdata 用户自定义数据
 * @return 0 继续提取，非 0 终止提取
 */
typedef int (*wpsext_callback_t)(const char *chunk, size_t len, void *userdata);

/**
 * @brief 以流式回调方式提取文本
 * @param[in] ctx       上下文句柄
 * @param[in] path      文件路径
 * @param[in] callback  回调函数
 * @param[in] userdata  传递给回调的用户数据
 * @return WPSEXT_OK 成功，WPSEXT_ERR_* 失败，>0 用户终止
 */
int wpsext_extract_stream(wpsext_ctx_t *ctx,
                          const char *path,
                          wpsext_callback_t callback,
                          void *userdata);

#ifdef __cplusplus
}
#endif

#endif /* WPSEXTRACT_H */
