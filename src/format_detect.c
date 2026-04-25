/*
 * format_detect.c - WPS 文件格式自动识别
 *
 * 检测策略:
 *   1. 查找 [Content_Types].xml 条目
 *   2. 解析 ContentType 匹配目标格式
 *
 * WPS 文字 (.wps):
 *   ContentType: application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml
 *   (对应 word/document.xml)
 *
 * WPS 表格 (.et):
 *   ContentType: application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml
 *   (对应 xl/workbook.xml)
 *
 * WPS 演示 (.dps):
 *   ContentType: application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml
 *   (对应 ppt/presentation.xml)
 */

#include "internal.h"

/* 查找 ContentTypes 的回调上下文 */
typedef struct {
    int found;
    uint8_t *data;
    size_t  size;
} ct_callback_ctx_t;

static int ct_callback(const zip_entry_t *entry, void *ctx)
{
    ct_callback_ctx_t *c = (ct_callback_ctx_t *)ctx;
    if (strcmp(entry->name, "[Content_Types].xml") == 0)
        c->found = 1;
    return entry->name[0] == '[' ? 0 : 0;  /* 继续遍历 */
}

/*
 * 在 [Content_Types].xml 内容中查找特征字符串
 * 简化实现：搜索关键字
 */
static wpsext_filetype_t parse_content_types(const char *xml, size_t len)
{
    WPSEXT_UNUSED(len);

    /* WPS 文字特征: wordprocessingml.document */
    if (strstr(xml, "wordprocessingml.document") ||
        strstr(xml, "wordprocessingml.document.main"))
        return WPSEXT_TYPE_WPS;

    /* WPS 表格特征: spreadsheetml.sheet */
    if (strstr(xml, "spreadsheetml.sheet") ||
        strstr(xml, "spreadsheetml.sheet.main"))
        return WPSEXT_TYPE_ET;

    /* WPS 演示特征: presentationml.presentation */
    if (strstr(xml, "presentationml.presentation") ||
        strstr(xml, "presentationml.presentation.main"))
        return WPSEXT_TYPE_DPS;

    return WPSEXT_TYPE_UNKNOWN;
}

wpsext_filetype_t format_detect(zip_reader_t *zr)
{
    ct_callback_ctx_t ctx = { 0, NULL, 0 };
    uint8_t *xml_data = NULL;
    size_t xml_size = 0;

    /* 先确认 [Content_Types].xml 存在 */
    zip_foreach(zr, ct_callback, &ctx);
    if (!ctx.found)
        return WPSEXT_TYPE_UNKNOWN;

    /* 读取并解析 */
    if (zip_read_entry(zr, "[Content_Types].xml", &xml_data, &xml_size) != WPSEXT_OK)
        return WPSEXT_TYPE_UNKNOWN;

    wpsext_filetype_t type = parse_content_types((const char *)xml_data, xml_size);

    free(xml_data);
    return type;
}
