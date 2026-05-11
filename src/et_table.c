/*
 * et_table.c - 从 .et 文件中提取表格内容
 *
 * .et 文件 (OOXML) 内部结构:
 *   xl/sharedStrings.xml     - 共享字符串表
 *   xl/worksheets/sheet*.xml - 工作表
 *
 * 关键 XML 元素（s 命名空间 = http://schemas.openxmlformats.org/spreadsheetml/2006/main）:
 *   <sst>          - 共享字符串表根元素
 *   <si>           - 共享字符串条目
 *   <t>            - 文本内容
 *   <r>            - 富文本运行 (Run)
 *   <worksheet>    - 工作表根元素
 *   <sheetData>    - 行和列数据
 *   <row>          - 行
 *   <c>            - 单元格 (r="A1", t="s"|"inlineStr"|"str"|"b"|"e")
 *   <v>            - 单元格值（对于 t="s"，值为共享字符串索引）
 *   <is>           - 内联字符串
 *
 * 文本提取规则:
 *   - 每个 <row> 结束追加 '\n'
 *   - 每个单元格内容后追加 '\t'
 *   - 空单元格输出空字符串 + '\t'
 *   - 若 t="s"，从共享字符串表取索引对应的文本
 *   - 若 t="inlineStr"，取 <is><t> 文本
 *   - 若 t="str" 或无 t，取 <v> 内容
 *   - 每个工作表之间追加 "\n--- Sheet N ---\n"
 */

#include "internal.h"
#include <ctype.h>

/* ================================================================
 * 共享字符串表 (SST)
 * ================================================================ */

typedef struct {
    char  **strings;
    size_t  count;
    size_t  capacity;
} sst_t;

static void sst_init(sst_t *sst)
{
    sst->strings  = NULL;
    sst->count    = 0;
    sst->capacity = 0;
}

static void sst_free(sst_t *sst)
{
    if (!sst) return;
    for (size_t i = 0; i < sst->count; i++)
        free(sst->strings[i]);
    free(sst->strings);
    sst->strings  = NULL;
    sst->count    = 0;
    sst->capacity = 0;
}

static int sst_add(sst_t *sst, const char *str, size_t len)
{
    if (sst->count >= sst->capacity) {
        size_t new_cap = sst->capacity ? sst->capacity * 2 : 64;
        char **new_arr = (char **)realloc(sst->strings, new_cap * sizeof(char *));
        if (!new_arr) return -1;
        sst->strings  = new_arr;
        sst->capacity = new_cap;
    }

    sst->strings[sst->count] = (char *)malloc(len + 1);
    if (!sst->strings[sst->count]) return -1;

    memcpy(sst->strings[sst->count], str, len);
    sst->strings[sst->count][len] = '\0';
    sst->count++;
    return 0;
}

/* ================================================================
 * 辅助函数
 * ================================================================ */

/* 判断标签名（忽略命名空间前缀）是否匹配本地名 */
static int tag_is(const char *name, const char *local)
{
    const char *colon = strchr(name, ':');
    const char *actual = colon ? colon + 1 : name;
    return strcmp(actual, local) == 0;
}

/* 判断属性名（忽略命名空间前缀）是否匹配本地名 */
static int attr_is(const char *attr_name, const char *local)
{
    const char *colon = strchr(attr_name, ':');
    const char *actual = colon ? colon + 1 : attr_name;
    return strcmp(actual, local) == 0;
}

/* ================================================================
 * 共享字符串解析器 SAX 回调
 * ================================================================ */

typedef struct {
    sst_t    *sst;
    strbuf_t  current_text;   /* 当前 <si> 中累积的文本 */
    int       in_si;          /* 是否在 <si> 内 */
    int       in_t;           /* 是否在 <t> 内 */
} sst_sax_ctx_t;

static void sst_on_start(void *ctx, const char *name, const char **attrs)
{
    sst_sax_ctx_t *sc = (sst_sax_ctx_t *)ctx;
    (void)attrs;

    if (tag_is(name, "si")) {
        sc->in_si = 1;
        strbuf_init(&sc->current_text);
    } else if (tag_is(name, "t") && sc->in_si) {
        sc->in_t = 1;
    }
}

static void sst_on_end(void *ctx, const char *name)
{
    sst_sax_ctx_t *sc = (sst_sax_ctx_t *)ctx;

    if (tag_is(name, "si")) {
        sc->in_si = 0;
        sst_add(sc->sst, sc->current_text.data, sc->current_text.len);
        strbuf_free(&sc->current_text);
    } else if (tag_is(name, "t")) {
        sc->in_t = 0;
    }
}

static void sst_on_chars(void *ctx, const char *data, size_t len)
{
    sst_sax_ctx_t *sc = (sst_sax_ctx_t *)ctx;

    if (sc->in_si && sc->in_t) {
        strbuf_append(&sc->current_text, data, len);
    }
}

/* ---- 解析共享字符串表 ------------------------------------------ */

static int parse_shared_strings(zip_reader_t *zr, sst_t *sst)
{
    uint8_t *xml_data = NULL;
    size_t   xml_size = 0;
    sst_sax_ctx_t sc;
    int rc;

    rc = zip_read_entry(zr, "xl/sharedStrings.xml", &xml_data, &xml_size);
    if (rc != WPSEXT_OK) {
        /* 没有共享字符串表也是合法的（可能全是内联字符串或数值） */
        return WPSEXT_OK;
    }

    memset(&sc, 0, sizeof(sc));
    sc.sst = sst;

    xml_sax_handler_t handler;
    memset(&handler, 0, sizeof(handler));
    handler.on_start_element = sst_on_start;
    handler.on_end_element   = sst_on_end;
    handler.on_characters    = sst_on_chars;

    rc = xml_parse_sax(&handler, &sc, xml_data, xml_size);
    free(xml_data);

    if (rc != WPSEXT_OK) {
        strbuf_free(&sc.current_text);
        return rc;
    }

    return WPSEXT_OK;
}

/* ================================================================
 * 工作表解析器 SAX 回调
 * ================================================================ */

typedef struct {
    sst_t     *sst;
    strbuf_t  *out;

    strbuf_t   cell_text;      /* 当前单元格累积的文本 */

    int        in_sheetData;
    int        in_row;
    int        in_c;
    int        in_v;           /* <v> 单元格值 */
    int        in_is;          /* <is> 内联字符串 */
    int        in_is_t;        /* <is> 内的 <t> */

    char       cell_type[16];  /* 单元格类型: "s", "inlineStr", "str", "b", "" */
    int        has_cell_value; /* 当前单元格是否已产出内容 */
} sheet_sax_ctx_t;

static void sheet_on_start(void *ctx, const char *name, const char **attrs)
{
    sheet_sax_ctx_t *sc = (sheet_sax_ctx_t *)ctx;

    if (tag_is(name, "sheetData")) {
        sc->in_sheetData = 1;

    } else if (tag_is(name, "row") && sc->in_sheetData) {
        sc->in_row = 1;

    } else if (tag_is(name, "c") && sc->in_row) {
        sc->in_c = 1;
        sc->cell_type[0] = '\0';
        sc->has_cell_value = 0;
        strbuf_init(&sc->cell_text);

        /* 读取单元格属性: r(引用), t(类型), s(样式) */
        if (attrs) {
            for (int i = 0; attrs[i] && attrs[i + 1]; i += 2) {
                if (attr_is(attrs[i], "t")) {
                    strncpy(sc->cell_type, attrs[i + 1],
                            sizeof(sc->cell_type) - 1);
                    sc->cell_type[sizeof(sc->cell_type) - 1] = '\0';
                }
            }
        }

    } else if (tag_is(name, "v") && sc->in_c) {
        sc->in_v = 1;

    } else if (tag_is(name, "is") && sc->in_c) {
        sc->in_is = 1;

    } else if (tag_is(name, "t") && sc->in_is) {
        sc->in_is_t = 1;
    }
}

static void sheet_on_end(void *ctx, const char *name)
{
    sheet_sax_ctx_t *sc = (sheet_sax_ctx_t *)ctx;

    if (tag_is(name, "sheetData")) {
        sc->in_sheetData = 0;

    } else if (tag_is(name, "row")) {
        /* 行结束：去掉行末多余的制表符，追加换行 */
        if (sc->out->len > 0 && sc->out->data[sc->out->len - 1] == '\t')
            sc->out->len--;
        strbuf_append_char(sc->out, '\n');
        sc->in_row = 0;

    } else if (tag_is(name, "c")) {
        /* 单元格结束：输出文本 + 制表符 */
        if (sc->has_cell_value) {
            strbuf_append(sc->out, sc->cell_text.data, sc->cell_text.len);
        }
        strbuf_append_char(sc->out, '\t');
        strbuf_free(&sc->cell_text);
        sc->in_c    = 0;
        sc->in_v    = 0;
        sc->in_is   = 0;
        sc->in_is_t = 0;

    } else if (tag_is(name, "v")) {
        sc->in_v = 0;

    } else if (tag_is(name, "is")) {
        sc->in_is = 0;

    } else if (tag_is(name, "t") && sc->in_is) {
        sc->in_is_t = 0;
    }
}

static void sheet_on_chars(void *ctx, const char *data, size_t len)
{
    sheet_sax_ctx_t *sc = (sheet_sax_ctx_t *)ctx;

    /* ---- <v> 内的值 ---- */
    if (sc->in_v && !sc->has_cell_value) {
        /* 跳过空白 */
        while (len > 0 && (data[0] == ' ' || data[0] == '\n' ||
                           data[0] == '\r' || data[0] == '\t')) {
            data++; len--;
        }
        if (len == 0) return;

        /* 如果单元格类型是 "s"，值是共享字符串的索引 */
        if (sc->cell_type[0] == 's' && sc->cell_type[1] == '\0') {
            unsigned long idx = 0;
            for (size_t i = 0; i < len; i++) {
                if (data[i] >= '0' && data[i] <= '9')
                    idx = idx * 10 + (unsigned long)(data[i] - '0');
                else
                    break;
            }
            if (idx < sc->sst->count) {
                strbuf_append_str(&sc->cell_text, sc->sst->strings[idx]);
                sc->has_cell_value = 1;
            }
        } else {
            /* 直接取 <v> 的文本（数值等） */
            strbuf_append(&sc->cell_text, data, len);
            sc->has_cell_value = 1;
        }
        return;
    }

    /* ---- <is><t> 内的内联文本 ---- */
    if (sc->in_is_t) {
        strbuf_append(&sc->cell_text, data, len);
        sc->has_cell_value = 1;
        return;
    }
}

/* ---- 解析单个工作表 -------------------------------------------- */

static int parse_worksheet(zip_reader_t *zr, const char *entry_name,
                           sst_t *sst, strbuf_t *out)
{
    uint8_t *xml_data = NULL;
    size_t   xml_size = 0;
    sheet_sax_ctx_t sc;
    int rc;

    rc = zip_read_entry(zr, entry_name, &xml_data, &xml_size);
    if (rc != WPSEXT_OK) {
        /* 跳过无法读取的工作表 */
        return WPSEXT_OK;
    }

    memset(&sc, 0, sizeof(sc));
    sc.sst = sst;
    sc.out = out;

    xml_sax_handler_t handler;
    memset(&handler, 0, sizeof(handler));
    handler.on_start_element = sheet_on_start;
    handler.on_end_element   = sheet_on_end;
    handler.on_characters    = sheet_on_chars;

    rc = xml_parse_sax(&handler, &sc, xml_data, xml_size);

    /* 如果最后一行末尾是制表符，清理一下 */
    if (rc == WPSEXT_OK && out->len > 0 && out->data[out->len - 1] == '\t') {
        out->len--;
        out->data[out->len] = '\0';
    }

    /* 清理未关闭的单元格文本缓冲 */
    if (sc.in_c) {
        strbuf_free(&sc.cell_text);
    }

    free(xml_data);
    return rc;
}

/* ================================================================
 * 收集工作表条目名称的回调
 * ================================================================ */

typedef struct {
    char  **names;
    size_t  count;
    size_t  capacity;
} ws_list_t;

static int ws_list_callback(const zip_entry_t *entry, void *ctx)
{
    ws_list_t *wl = (ws_list_t *)ctx;
    const char *prefix = "xl/worksheets/sheet";
    size_t plen = strlen(prefix);

    if (strncmp(entry->name, prefix, plen) != 0)
        return 0;  /* 不匹配，继续遍历 */

    size_t nlen = strlen(entry->name);
    if (nlen < 5 || strcmp(entry->name + nlen - 4, ".xml") != 0)
        return 0;  /* 不以 .xml 结尾 */

    /* 加入列表 */
    if (wl->count >= wl->capacity) {
        size_t new_cap = wl->capacity ? wl->capacity * 2 : 8;
        char **new_arr = (char **)realloc(wl->names, new_cap * sizeof(char *));
        if (!new_arr) return -1;
        wl->names    = new_arr;
        wl->capacity = new_cap;
    }

    wl->names[wl->count] = strdup(entry->name);
    if (!wl->names[wl->count]) return -1;
    wl->count++;

    return 0;  /* 继续遍历 */
}

static void ws_list_free(ws_list_t *wl)
{
    for (size_t i = 0; i < wl->count; i++)
        free(wl->names[i]);
    free(wl->names);
    wl->names    = NULL;
    wl->count    = 0;
    wl->capacity = 0;
}

/* ================================================================
 * 公开接口
 * ================================================================ */

int et_table_extract(zip_reader_t *zr, char **out_text, size_t *out_len)
{
    sst_t    sst;
    strbuf_t sb;
    ws_list_t wl;
    int rc;

    if (!zr || !out_text)
        return WPSEXT_ERR_INVALID_ARG;

    *out_text = NULL;
    if (out_len) *out_len = 0;

    /* 1. 解析共享字符串表 */
    sst_init(&sst);
    rc = parse_shared_strings(zr, &sst);
    if (rc != WPSEXT_OK)
        return rc;

    /* 2. 收集所有工作表条目 */
    memset(&wl, 0, sizeof(wl));
    rc = zip_foreach(zr, ws_list_callback, &wl);
    if (rc != 0 || wl.count == 0) {
        sst_free(&sst);
        return (rc == 0) ? WPSEXT_ERR_FORMAT : WPSEXT_ERR_MEMORY;
    }

    /* 3. 初始化输出缓冲 */
    strbuf_init(&sb);

    /* 4. 逐个解析工作表 */
    for (size_t i = 0; i < wl.count; i++) {
        /* 工作表之间加分隔符 */
        if (i > 0) {
            strbuf_append_str(&sb, "\n--- Sheet ");
            /* 提取 sheet 序号 */
            const char *p = wl.names[i] + strlen("xl/worksheets/sheet");
            while (*p && isdigit((unsigned char)*p)) {
                strbuf_append_char(&sb, *p);
                p++;
            }
            strbuf_append_str(&sb, " ---\n");
        }

        rc = parse_worksheet(zr, wl.names[i], &sst, &sb);
        if (rc != WPSEXT_OK) {
            /* 某个工作表解析失败，继续处理后面的 */
            continue;
        }
    }

    /* 5. 去除末尾多余的空白 */
    while (sb.len > 0 && (sb.data[sb.len - 1] == '\n' ||
                          sb.data[sb.len - 1] == '\t')) {
        sb.len--;
    }
    sb.data[sb.len] = '\0';

    /* 6. 清理并返回 */
    sst_free(&sst);
    ws_list_free(&wl);

    *out_text = strbuf_detach(&sb, out_len);
    return WPSEXT_OK;
}
