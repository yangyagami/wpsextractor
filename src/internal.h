/*
 * 内部头文件 - 不对外暴露
 */

#ifndef WPSEXT_INTERNAL_H
#define WPSEXT_INTERNAL_H

#include "wpsextract.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ================================================================
 * 通用宏
 * ================================================================ */

#define WPSEXT_UNUSED(x)    ((void)(x))
#define WPSEXT_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* 安全的内存分配，失败时返回 NULL */
#define WPSEXT_MALLOC(sz)   malloc(sz)
#define WPSEXT_CALLOC(n,sz) calloc(n, sz)
#define WPSEXT_FREE(p)      do { free(p); (p) = NULL; } while(0)

/* ================================================================
 * 上下文结构体定义
 * ================================================================ */

struct wpsext_ctx {
    wpsext_options_t    opts;
};

/* ================================================================
 * ZIP Reader 内部接口
 * ================================================================ */

typedef struct zip_reader zip_reader_t;

typedef struct {
    char        *name;           /* 条目路径名 */
    size_t       compressed;     /* 压缩后大小 */
    size_t       uncompressed;   /* 解压后大小 */
    uint16_t     method;         /* 压缩方法: 0=store, 8=deflate */
    uint32_t     crc32;          /* CRC32 校验值 */
    uint32_t     offset;         /* 本地文件头在归档中的偏移 */
} zip_entry_t;

zip_reader_t *zip_open(const char *path);
void          zip_close(zip_reader_t *zr);
int           zip_foreach(zip_reader_t *zr,
                          int (*callback)(const zip_entry_t *entry, void *ctx),
                          void *ctx);
int           zip_read_entry(zip_reader_t *zr,
                             const char *entry_name,
                             uint8_t **out_data,
                             size_t *out_size);

/**
 * @brief 快速检查文件是否以 ZIP magic (PK\x03\x04) 开头
 * @return 1 是 ZIP 文件，0 不是
 */
int           zip_check_magic(const char *path);

/* ================================================================
 * XML Parser 内部接口
 * ================================================================ */

typedef struct {
    void (*on_start_element)(void *ctx, const char *name, const char **attrs);
    void (*on_end_element)(void *ctx, const char *name);
    void (*on_characters)(void *ctx, const char *data, size_t len);
} xml_sax_handler_t;

int xml_parse_sax(const xml_sax_handler_t *handler,
                  void *handler_ctx,
                  const uint8_t *data,
                  size_t size);

/* ================================================================
 * 格式检测 内部接口
 * ================================================================ */

wpsext_filetype_t format_detect(zip_reader_t *zr);

/* ================================================================
 * 各格式提取器 内部接口
 * ================================================================ */

/**
 * @brief 从 .wps 文件的 word/document.xml 提取段落文本
 * @param zr         已打开的 ZIP reader
 * @param out_text   输出文本（调用方 free）
 * @param out_len    输出长度
 * @return WPSEXT_OK / 错误码
 */
int wps_text_extract(zip_reader_t *zr, char **out_text, size_t *out_len);

/**
 * @brief 从 .et 文件的 xl/worksheets/sheet*.xml 提取单元格文本
 * @param zr         已打开的 ZIP reader
 * @param out_text   输出文本（调用方 free）
 * @param out_len    输出长度
 * @return WPSEXT_OK / 错误码
 */
int et_table_extract(zip_reader_t *zr, char **out_text, size_t *out_len);

/* ================================================================
 * OLE2 复合文档读取器 (共享接口)
 * ================================================================ */

/* OLE2 上下文（不透明，仅声明）*/
typedef struct ole2_ctx {
    FILE    *fp;
    size_t   file_size;
    size_t   sect_size;
    size_t   mini_sect_size;
    uint32_t *fat;
    size_t   fat_count;
    uint32_t *mini_fat;
    size_t   mini_fat_count;
    uint8_t  *dir_entries;
    size_t   dir_entry_count;
    uint8_t  *mini_stream;
    size_t   mini_stream_size;
    uint32_t first_dir_secid;
    uint32_t mini_stream_start;
} ole2_ctx_t;

/**
 * @brief 打开 OLE2 文件
 * @param path  文件路径
 * @return OLE2 上下文句柄，失败返回 NULL
 */
ole2_ctx_t *ole2_open(const char *path);

/**
 * @brief 关闭 OLE2 文件，释放资源
 */
void ole2_close(ole2_ctx_t *ole);

/**
 * @brief 在目录中按名称查找流
 * @param[in]  ole         OLE2 上下文
 * @param[in]  name        流名称（ASCII）
 * @param[out] out_start   返回起始 SECID
 * @param[out] out_size    返回流大小
 * @return 0 找到，-1 未找到
 */
int ole2_find_stream(ole2_ctx_t *ole, const char *name,
                     uint32_t *out_start, uint64_t *out_size);

/**
 * @brief 读取流的全部内容
 * @param[in]  ole          OLE2 上下文
 * @param[in]  start_secid  流起始 SECID
 * @param[in]  stream_size  流大小
 * @param[out] out_data     返回数据（调用方 free）
 * @param[out] out_size     返回数据大小
 * @return WPSEXT_OK 或错误码
 */
int ole2_read_stream(ole2_ctx_t *ole, uint32_t start_secid,
                     uint64_t stream_size, uint8_t **out_data,
                     size_t *out_size);

/**
 * @brief 检查文件是否为 OLE2 格式
 * @return 1 是 OLE2，0 不是
 */
int ole2_check_magic(const char *path);

/* ================================================================
 * 旧版二进制格式 (.wps / .et) 提取
 * ================================================================ */

/**
 * @brief 从旧版二进制 .wps 文件提取文本
 */
int wps_binary_extract_file(const char *path, char **out_text, size_t *out_len);

/**
 * @brief 从旧版二进制 .et 文件提取文本
 */
int et_binary_extract_file(const char *path, char **out_text, size_t *out_len);

/* ================================================================
 * 动态字符串构建器 (简易)
 * ================================================================ */

typedef struct {
    char   *data;
    size_t  len;
    size_t  cap;
} strbuf_t;

void strbuf_init(strbuf_t *sb);
void strbuf_free(strbuf_t *sb);
int  strbuf_append(strbuf_t *sb, const char *data, size_t len);
int  strbuf_append_str(strbuf_t *sb, const char *str);
int  strbuf_append_char(strbuf_t *sb, char ch);
char *strbuf_detach(strbuf_t *sb, size_t *out_len);

#endif /* WPSEXT_INTERNAL_H */
