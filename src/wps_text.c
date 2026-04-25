/*
 * wps_text.c - 从 .wps 文件中提取文字内容
 *
 * .wps 文件 (OOXML) 内部结构:
 *   word/document.xml 包含文档正文
 *   关键 XML 元素（w 命名空间 = http://schemas.openxmlformats.org/wordprocessingml/2006/main）:
 *     <w:p>         - 段落 (Paragraph)
 *     <w:r>         - 文本运行 (Run)，包含格式相同的连续文本
 *     <w:t>         - 文本内容 (Text)
 *     <w:br/>       - 换行符
 *     <w:tab/>      - 制表符
 *
 * 文本提取规则:
 *   - 每个 <w:p> 结束追加 '\n'
 *   - <w:br/> 追加 '\n'
 *   - <w:tab/> 追加 '\t'
 *   - 合并同一段落内的 <w:r> 文本
 */

#include "internal.h"

/* ---- 提取器 SAX 回调上下文 ------------------------------------- */
typedef struct {
    strbuf_t   *out;
    int         in_paragraph;   /* 是否在 <w:p> 内 */
    int         in_run;         /* 是否在 <w:r> 内 */
    int         in_text;        /* 是否在 <w:t> 内 */
    int         preserve_space; /* xml:space="preserve" */
    size_t      para_start_len; /* 段落开始时的输出长度，用于判断空段落 */
} wps_sax_ctx_t;

/* 判断标签是否为某命名空间元素（忽略命名空间前缀） */
static int tag_is(const char *name, const char *local)
{
    /* 跳过命名空间前缀 */
    const char *colon = strchr(name, ':');
    const char *actual = colon ? colon + 1 : name;
    return strcmp(actual, local) == 0;
}

/* ---- SAX 回调 -------------------------------------------------- */

static void wps_on_start(void *ctx, const char *name, const char **attrs)
{
    wps_sax_ctx_t *wsc = (wps_sax_ctx_t *)ctx;

    if (tag_is(name, "p")) {
        /* 段落开始 */
        wsc->in_paragraph = 1;
        wsc->para_start_len = wsc->out->len;

    } else if (tag_is(name, "r")) {
        /* 文本运行开始 */
        wsc->in_run = 1;

    } else if (tag_is(name, "t")) {
        /* 文本开始 */
        wsc->in_text = 1;
        /* 检查 xml:space="preserve" */
        wsc->preserve_space = 0;
        if (attrs) {
            for (int i = 0; attrs[i] && attrs[i+1]; i += 2) {
                if (strstr(attrs[i], "space") &&
                    strcmp(attrs[i+1], "preserve") == 0) {
                    wsc->preserve_space = 1;
                }
            }
        }

    } else if (tag_is(name, "br")) {
        /* 换行符 */
        strbuf_append_char(wsc->out, '\n');

    } else if (tag_is(name, "tab")) {
        /* 制表符 */
        strbuf_append_char(wsc->out, '\t');
    }
}

static void wps_on_end(void *ctx, const char *name)
{
    wps_sax_ctx_t *wsc = (wps_sax_ctx_t *)ctx;

    if (tag_is(name, "p")) {
        /* 段落结束，追加换行 */
        strbuf_append_char(wsc->out, '\n');
        wsc->in_paragraph = 0;

    } else if (tag_is(name, "r")) {
        wsc->in_run = 0;

    } else if (tag_is(name, "t")) {
        wsc->in_text = 0;
    }
}

static void wps_on_chars(void *ctx, const char *data, size_t len)
{
    wps_sax_ctx_t *wsc = (wps_sax_ctx_t *)ctx;

    if (!wsc->in_text) return;

    /* 如果不是 preserve 模式，折叠空白 */
    if (!wsc->preserve_space) {
        /* 跳过前导空白 */
        while (len > 0 && (data[0] == ' ' || data[0] == '\t' ||
                           data[0] == '\n' || data[0] == '\r')) {
            data++; len--;
        }
        /* 跳过后缀空白 */
        while (len > 0 && (data[len-1] == ' ' || data[len-1] == '\t' ||
                           data[len-1] == '\n' || data[len-1] == '\r')) {
            len--;
        }
        if (len == 0) return;
    }

    strbuf_append(wsc->out, data, len);
}

/* ---- 公开接口 -------------------------------------------------- */

int wps_text_extract(zip_reader_t *zr, char **out_text, size_t *out_len)
{
    uint8_t *xml_data = NULL;
    size_t xml_size = 0;
    strbuf_t sb;
    wps_sax_ctx_t wsc;
    int rc;

    if (!zr || !out_text)
        return WPSEXT_ERR_INVALID_ARG;

    *out_text = NULL;
    if (out_len) *out_len = 0;

    /* 读取 word/document.xml */
    rc = zip_read_entry(zr, "word/document.xml", &xml_data, &xml_size);
    if (rc != WPSEXT_OK)
        return rc;

    /* 初始化输出缓冲 */
    strbuf_init(&sb);

    /* 设置 SAX 处理器 */
    memset(&wsc, 0, sizeof(wsc));
    wsc.out = &sb;

    xml_sax_handler_t handler;
    memset(&handler, 0, sizeof(handler));
    handler.on_start_element = wps_on_start;
    handler.on_end_element   = wps_on_end;
    handler.on_characters    = wps_on_chars;

    /* 解析 XML */
    rc = xml_parse_sax(&handler, &wsc, xml_data, xml_size);
    free(xml_data);

    if (rc != WPSEXT_OK) {
        strbuf_free(&sb);
        return rc;
    }

    /* 去掉末尾多余的换行 */
    while (sb.len > 0 && sb.data[sb.len - 1] == '\n') {
        sb.len--;
    }
    if (sb.len > 0) {
        /* 重新以单个 \n 结束 */
        sb.data[sb.len] = '\0';
    }

    /* 移交缓冲区所有权 */
    *out_text = strbuf_detach(&sb, out_len);

    return WPSEXT_OK;
}
