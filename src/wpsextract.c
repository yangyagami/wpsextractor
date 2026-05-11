/*
 * wpsextract.c - 公共 API 实现
 *
 * 包括: 库初始化/清理, 版本信息, 错误描述,
 *       上下文管理, 文件类型检测, 文本提取入口
 */

#include "internal.h"
#include <strings.h>   /* strcasecmp */

/* ================================================================
 * 库初始化 / 清理
 * ================================================================ */

int wpsext_init(void)
{
    /* 当前无全局状态需要初始化，预留 */
    return WPSEXT_OK;
}

void wpsext_cleanup(void)
{
    /* 当前无全局状态需要清理，预留 */
}

/* ================================================================
 * 版本信息
 * ================================================================ */

const char *wpsext_version(void)
{
    return "0.1.0";
}

/* ================================================================
 * 错误码描述
 * ================================================================ */

const char *wpsext_strerror(int errcode)
{
    switch (errcode) {
    case WPSEXT_OK:
        return "Success";
    case WPSEXT_ERR_FILE:
        return "File open/read error";
    case WPSEXT_ERR_FORMAT:
        return "Unsupported or corrupt file format (only OOXML-based .wps/.et/.dps files are supported, old WPS binary format is not supported)";
    case WPSEXT_ERR_MEMORY:
        return "Memory allocation failed";
    case WPSEXT_ERR_INTERNAL:
        return "Internal error";
    case WPSEXT_ERR_INVALID_ARG:
        return "Invalid argument";
    case WPSEXT_ERR_TOO_LARGE:
        return "File exceeds size limit";
    default:
        return "Unknown error";
    }
}

/* ================================================================
 * 上下文管理
 * ================================================================ */

/* 默认选项 */
static const wpsext_options_t k_default_opts = {
    0,      /* max_file_size: 无限制 */
    0,      /* max_output_size: 无限制 */
    0,      /* include_headers: 否 */
    0       /* structured: 否 */
};

int wpsext_ctx_create(const wpsext_options_t *opts, wpsext_ctx_t **ctx)
{
    if (!ctx)
        return WPSEXT_ERR_INVALID_ARG;

    *ctx = (wpsext_ctx_t *)calloc(1, sizeof(wpsext_ctx_t));
    if (!*ctx)
        return WPSEXT_ERR_MEMORY;

    /* 复制或使用默认选项 */
    if (opts)
        memcpy(&(*ctx)->opts, opts, sizeof(wpsext_options_t));
    else
        memcpy(&(*ctx)->opts, &k_default_opts, sizeof(wpsext_options_t));

    return WPSEXT_OK;
}

void wpsext_ctx_destroy(wpsext_ctx_t *ctx)
{
    free(ctx);
}

/* ================================================================
 * 获取默认上下文（当 ctx == NULL 时使用）
 * ================================================================ */

static wpsext_ctx_t *get_effective_ctx(wpsext_ctx_t *ctx)
{
    static wpsext_ctx_t default_ctx = { { 0, 0, 0, 0 } };
    return ctx ? ctx : &default_ctx;
}

/* ================================================================
 * 文件类型检测
 * ================================================================ */

int wpsext_detect_type(const char *path, wpsext_filetype_t *type)
{
    zip_reader_t *zr;
    wpsext_filetype_t detected = WPSEXT_TYPE_UNKNOWN;

    if (!path || !type)
        return WPSEXT_ERR_INVALID_ARG;

    *type = WPSEXT_TYPE_UNKNOWN;

    /* 先检查扩展名 */
    const char *ext = strrchr(path, '.');
    if (ext) {
        if (strcasecmp(ext, ".wps") == 0) {
            /* .wps: 可能是 OOXML 或旧版二进制格式 */
            /* 试试 ZIP（OOXML） */
            if (zip_check_magic(path)) {
                zr = zip_open(path);
                if (zr) {
                    detected = format_detect(zr);
                    zip_close(zr);
                }
            }
            /* 如果 ZIP 不行，试试 OLE2（旧版二进制） */
            if (detected == WPSEXT_TYPE_UNKNOWN && ole2_check_magic(path)) {
                detected = WPSEXT_TYPE_WPS;
            }
        } else if (strcasecmp(ext, ".et") == 0 ||
                   strcasecmp(ext, ".dps") == 0) {
            /* .et / .dps: 目前只支持 OOXML */
            if (zip_check_magic(path)) {
                zr = zip_open(path);
                if (zr) {
                    detected = format_detect(zr);
                    zip_close(zr);
                }
            }
        } else {
            return WPSEXT_ERR_FORMAT;
        }
    }

    *type = detected;
    return (detected != WPSEXT_TYPE_UNKNOWN) ? WPSEXT_OK : WPSEXT_ERR_FORMAT;
}

/* ================================================================
 * 文本提取（一次性）
 * ================================================================ */

int wpsext_extract_file(wpsext_ctx_t *ctx,
                        const char *path,
                        char **out_text,
                        size_t *out_len)
{
    int rc = WPSEXT_ERR_FORMAT;
    wpsext_filetype_t ftype;

    if (!path || !out_text)
        return WPSEXT_ERR_INVALID_ARG;

    *out_text = NULL;
    if (out_len) *out_len = 0;

    ctx = get_effective_ctx(ctx);

    /* 检查文件大小限制 */
    if (ctx->opts.max_file_size > 0) {
        FILE *fp = fopen(path, "rb");
        if (!fp)
            return WPSEXT_ERR_FILE;
        fseek(fp, 0, SEEK_END);
        long fsize = ftell(fp);
        fclose(fp);
        if (fsize < 0)
            return WPSEXT_ERR_FILE;
        if ((size_t)fsize > ctx->opts.max_file_size)
            return WPSEXT_ERR_TOO_LARGE;
    }

    /* 判断是 ZIP (OOXML) 还是 OLE2 (二进制) */
    if (zip_check_magic(path)) {
        /* ---- OOXML 格式 ---- */
        zip_reader_t *zr = zip_open(path);
        if (!zr)
            return WPSEXT_ERR_FORMAT;

        ftype = format_detect(zr);
        if (ftype == WPSEXT_TYPE_UNKNOWN) {
            zip_close(zr);
            return WPSEXT_ERR_FORMAT;
        }

        switch (ftype) {
        case WPSEXT_TYPE_WPS:
            rc = wps_text_extract(zr, out_text, out_len);
            break;
        case WPSEXT_TYPE_ET:
            rc = et_table_extract(zr, out_text, out_len);
            break;
        case WPSEXT_TYPE_DPS:
            rc = WPSEXT_ERR_FORMAT;
            break;
        default:
            rc = WPSEXT_ERR_FORMAT;
            break;
        }

        zip_close(zr);
    } else if (ole2_check_magic(path)) {
        /* ---- 旧版 WPS 二进制格式 ---- */
        rc = wps_binary_extract_file(path, out_text, out_len);
    } else {
        return WPSEXT_ERR_FORMAT;
    }

    /* 检查输出大小限制 */
    if (rc == WPSEXT_OK && ctx->opts.max_output_size > 0 && out_len) {
        if (*out_len > ctx->opts.max_output_size) {
            free(*out_text);
            *out_text = NULL;
            *out_len = 0;
            return WPSEXT_ERR_TOO_LARGE;
        }
    }

    return rc;
}

void wpsext_free_text(char *text)
{
    free(text);
}

/* ================================================================
 * 文本提取（流式）
 * ================================================================ */

int wpsext_extract_stream(wpsext_ctx_t *ctx,
                          const char *path,
                          wpsext_callback_t callback,
                          void *userdata)
{
    char *text = NULL;
    size_t len = 0;
    int rc;

    if (!path || !callback)
        return WPSEXT_ERR_INVALID_ARG;

    /* 先用一次性提取获取全部文本，再分块回调 */
    /* 后续 v0.5.0 实现真正的流式 */
    rc = wpsext_extract_file(ctx, path, &text, &len);
    if (rc != WPSEXT_OK)
        return rc;

    int cb_rc = callback(text, len, userdata);

    free(text);
    return (cb_rc == 0) ? WPSEXT_OK : cb_rc;
}
