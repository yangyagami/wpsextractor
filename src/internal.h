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
