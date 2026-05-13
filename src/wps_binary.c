/*
 * wps_binary.c - 从旧版 WPS 二进制格式 (.wps) 提取文本
 *
 * 格式说明:
 *   WPS 文字早期版本（v8 及更早）使用 OLE2 复合文档格式，
 *   内部结构类似 Microsoft Word 97-2003 二进制格式。
 *
 *   关键流 (stream):
 *     - WordDocument: FIB (File Information Block) + 文本数据
 *     - 0Table: CLX (含 Pcdt 分段表)
 *
 *   文本提取流程:
 *     1. 打开 OLE2 文件，读取 WordDocument 和 0Table 流
 *     2. 解析 FIB 获取 ccpText (正文文本字符数)
 *     3. 在 0Table 中找到 Pcdt (type=0x02)
 *     4. Pcdt 含 CP (字符位置) 和 PCD (文件偏移描述)
 *     5. 根据 fc + ccpText 从 WordDocument 读取 UTF-16LE 文本
 *     6. 清洗文本（\r → \n, \x0B → \n）
 */

#include "internal.h"

/* ---- 小端序读取（局部辅助） ------------------------------------ */

static uint16_t r_u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t r_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ================================================================
 * 核心提取函数
 * ================================================================ */

static int wps_binary_extract_int(ole2_ctx_t *ole, char **out_text, size_t *out_len)
{
    uint8_t *wd_data = NULL;   /* WordDocument stream */
    size_t   wd_size = 0;
    uint8_t *tbl_data = NULL;  /* 0Table stream */
    size_t   tbl_size = 0;

    uint32_t start;
    uint64_t stream_size;

    strbuf_t sb;
    int rc;

    if (!ole || !out_text)
        return WPSEXT_ERR_INVALID_ARG;

    *out_text = NULL;
    if (out_len) *out_len = 0;

    /* 1. 读取 WordDocument stream */
    if (ole2_find_stream(ole, "WordDocument", &start, &stream_size) != 0)
        return WPSEXT_ERR_FORMAT;
    rc = ole2_read_stream(ole, start, stream_size, &wd_data, &wd_size);
    if (rc != WPSEXT_OK) return rc;

    /* 2. 读取 0Table stream */
    if (ole2_find_stream(ole, "0Table", &start, &stream_size) != 0) {
        free(wd_data);
        return WPSEXT_ERR_FORMAT;
    }
    rc = ole2_read_stream(ole, start, stream_size, &tbl_data, &tbl_size);
    if (rc != WPSEXT_OK) { free(wd_data); return rc; }

    /* 3. 解析 FIB 获取 ccpText */
    if (wd_size < 64) {
        free(wd_data); free(tbl_data);
        return WPSEXT_ERR_FORMAT;
    }

    /* FIB Base: 32 bytes */
    uint16_t csw = r_u16(wd_data + 32);
    /* 跳过 FibRgW */
    size_t rgw_end = 34 + csw * 2;
    /* 读取 cslw */
    if (rgw_end + 2 > wd_size) {
        free(wd_data); free(tbl_data);
        return WPSEXT_ERR_FORMAT;
    }
    /* ccpText = FibRgLw[3] */
    if (rgw_end + 2 + 4 * 4 > wd_size) {
        free(wd_data); free(tbl_data);
        return WPSEXT_ERR_FORMAT;
    }
    uint32_t ccpText = r_u32(wd_data + rgw_end + 2 + 3 * 4);

    if (ccpText == 0 || ccpText > 1000000) {
        free(wd_data); free(tbl_data);
        return WPSEXT_ERR_FORMAT;
    }

    /* 4. 在 0Table 中找到 Pcdt (type = 0x02) */
    int found_pcdt = 0;
    uint32_t fc = 0;
    int is_compressed = 0;

    for (size_t i = 0; i + 5 < tbl_size; i++) {
        if (tbl_data[i] == 0x02) {
            uint32_t lcb = r_u32(tbl_data + i + 1);
            if (lcb < 8 || lcb > tbl_size - i)
                continue;

            int n = (int)((lcb - 4) / 12);
            if (n < 0 || n > 1000) continue;
            if ((lcb - 4) % 12 != 0) continue;

            size_t cp_start = i + 5;
            size_t pcd_start = cp_start + (size_t)(n + 1) * 4;

            if (pcd_start + (size_t)n * 8 > tbl_size)
                continue;

            uint32_t cp0 = r_u32(tbl_data + cp_start);
            uint32_t cp1 = r_u32(tbl_data + cp_start + 4);

            if (cp0 == 0) {
                uint8_t *pcd = tbl_data + pcd_start;
                uint32_t flda = r_u32(pcd + 2);

                uint32_t fc_candidate = flda & 0x3FFFFFFF;
                if (fc_candidate > 0 && fc_candidate < wd_size) {
                    fc = fc_candidate;
                    is_compressed = (flda & 0x40000000) ? 1 : 0;
                    ccpText = cp1;  /* 使用 Pcdt 的 cp1 作为有效字符数 */
                    found_pcdt = 1;
                    break;
                }

                fc_candidate = r_u32(pcd + 2);
                if (fc_candidate > 0 && fc_candidate < wd_size) {
                    fc = fc_candidate;
                    is_compressed = 0;
                    ccpText = cp1;
                    found_pcdt = 1;
                    break;
                }

                fc_candidate = r_u32(pcd);
                if (fc_candidate > 0 && fc_candidate < wd_size) {
                    fc = fc_candidate;
                    is_compressed = 0;
                    ccpText = cp1;
                    found_pcdt = 1;
                    break;
                }
            }
        }
    }

    if (!found_pcdt) {
        free(wd_data); free(tbl_data);
        return WPSEXT_ERR_FORMAT;
    }

    /* 5. 读取文本 */
    strbuf_init(&sb);

    if (is_compressed) {
        if (fc + ccpText > wd_size) {
            strbuf_free(&sb); free(wd_data); free(tbl_data);
            return WPSEXT_ERR_FORMAT;
        }
        for (uint32_t j = 0; j < ccpText; j++) {
            char ch = (char)wd_data[fc + j];
            if (ch == '\r' || ch == '\x0B')
                strbuf_append_char(&sb, '\n');
            else if (ch == '\x07')
                strbuf_append_char(&sb, '\t');
            else if ((unsigned char)ch >= 0x20)
                strbuf_append_char(&sb, ch);
        }
    } else {
        uint32_t byte_len = ccpText * 2;
        if (fc + byte_len > wd_size) {
            strbuf_free(&sb); free(wd_data); free(tbl_data);
            return WPSEXT_ERR_FORMAT;
        }
        for (uint32_t j = 0; j < ccpText; j++) {
            uint16_t ch = r_u16(wd_data + fc + j * 2);
            if (ch == 0x000D || ch == 0x000B)
                strbuf_append_char(&sb, '\n');
            else if (ch == 0x0007)
                strbuf_append_char(&sb, '\t');
            else if (ch >= 0x20 && ch <= 0x7E)
                strbuf_append_char(&sb, (char)ch);
            else if (ch >= 0x80) {
                if (ch < 0x800) {
                    strbuf_append_char(&sb, (char)(0xC0 | (ch >> 6)));
                    strbuf_append_char(&sb, (char)(0x80 | (ch & 0x3F)));
                } else {
                    strbuf_append_char(&sb, (char)(0xE0 | (ch >> 12)));
                    strbuf_append_char(&sb, (char)(0x80 | ((ch >> 6) & 0x3F)));
                    strbuf_append_char(&sb, (char)(0x80 | (ch & 0x3F)));
                }
            }
        }
    }

    while (sb.len > 0 && sb.data[sb.len - 1] == '\n')
        sb.len--;
    sb.data[sb.len] = '\0';

    *out_text = strbuf_detach(&sb, out_len);

    free(wd_data);
    free(tbl_data);

    return WPSEXT_OK;
}

/* ---- 公开接口 -------------------------------------------------- */

int wps_binary_extract_file(const char *path, char **out_text, size_t *out_len)
{
    ole2_ctx_t *ole;
    int rc;

    if (!path || !out_text)
        return WPSEXT_ERR_INVALID_ARG;

    *out_text = NULL;
    if (out_len) *out_len = 0;

    ole = ole2_open(path);
    if (!ole)
        return WPSEXT_ERR_FORMAT;

    rc = wps_binary_extract_int(ole, out_text, out_len);

    ole2_close(ole);
    return rc;
}
