/*
 * extract.c - libwpsextract 使用示例
 *
 * 用法: extract <file.wps|file.et|file.dps>
 *
 * 输出提取的文本内容到 stdout
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wpsextract.h"

int main(int argc, char *argv[])
{
    wpsext_filetype_t ftype;
    char *text = NULL;
    size_t len = 0;
    int rc;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.wps|file.et|file.dps>\n", argv[0]);
        return 1;
    }

    /* 初始化库 */
    rc = wpsext_init();
    if (rc != WPSEXT_OK) {
        fprintf(stderr, "Failed to initialize: %s\n", wpsext_strerror(rc));
        return 1;
    }

    /* 检测文件类型 */
    rc = wpsext_detect_type(argv[1], &ftype);
    if (rc != WPSEXT_OK) {
        fprintf(stderr, "Cannot detect file type: %s\n", wpsext_strerror(rc));
        wpsext_cleanup();
        return 1;
    }

    const char *type_names[] = { "Unknown", "WPS", "ET", "DPS" };
    fprintf(stderr, "File type: %s\n", type_names[ftype]);

    /* 提取文本 */
    rc = wpsext_extract_file(NULL, argv[1], &text, &len);
    if (rc != WPSEXT_OK) {
        fprintf(stderr, "Extraction failed: %s\n", wpsext_strerror(rc));
        wpsext_cleanup();
        return 1;
    }

    /* 输出 */
    if (text) {
        fwrite(text, 1, len, stdout);
        fprintf(stdout, "\n");
        wpsext_free_text(text);
    }

    fprintf(stderr, "Extracted %zu bytes.\n", len);

    wpsext_cleanup();
    return 0;
}
