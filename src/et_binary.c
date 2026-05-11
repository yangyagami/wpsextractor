/*
 * et_binary.c - 从旧版 WPS 二进制格式 (.et) 提取文本
 *
 * 格式说明:
 *   WPS 表格早期版本使用 OLE2 复合文档格式，内部包含
 *   "Workbook" 流，使用类似 Excel BIFF 的记录结构。
 *
 *   关键流 (stream):
 *     - Workbook: 包含所有工作表数据的 BIFF 记录序列
 *
 *   关键 BIFF 记录类型:
 *     0x0809  WPS_BOF      - 工作表开始（WPS 自定义 BOF）
 *     0x000A  EOF          - 工作表结束
 *     0x00FC  SST          - 共享字符串表
 *     0x00FD  LABELSST     - 单元格（引用共享字符串）
 *     0x0204  LABEL        - 单元格（内联字符串）
 *     0x0208  ROW          - 行定义
 *     0x0200  DIMENSION    - 工作表范围
 *
 *   文本提取规则:
 *     - 按行组织单元格，行之间以 '\n' 分隔
 *     - 单元格之间以 '\t' 分隔
 *     - 每个工作表之间以 "\n--- Sheet N ---\n" 分隔
 *     - 空单元格输出空字符串
 */

#include "internal.h"

/* ---- BIFF 记录类型 -------------------------------------------- */

#define BIFF_WPS_BOF     0x0809
#define BIFF_EOF         0x000A
#define BIFF_SST         0x00FC
#define BIFF_LABELSST    0x00FD
#define BIFF_LABEL       0x0204
#define BIFF_ROW         0x0208
#define BIFF_DIMENSION   0x0200

/* ---- 小端序辅助 ------------------------------------------------ */

static uint16_t r_u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t r_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ================================================================
 * 共享字符串表
 * ================================================================ */

typedef struct {
    char  **strings;
    size_t  count;
    size_t  capacity;
} etb_sst_t;

static void sst_init(etb_sst_t *sst)
{
    sst->strings  = NULL;
    sst->count    = 0;
    sst->capacity = 0;
}

static void sst_free(etb_sst_t *sst)
{
    if (!sst) return;
    for (size_t i = 0; i < sst->count; i++)
        free(sst->strings[i]);
    free(sst->strings);
    sst->strings  = NULL;
    sst->count    = 0;
    sst->capacity = 0;
}

static int sst_add(etb_sst_t *sst, const char *str, size_t len)
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

/* ---- 解析 SST 记录（WPS 格式: char_count(2 LE) + flags(1) + text） - */

static int parse_sst_record(const uint8_t *data, size_t len,
                            etb_sst_t *sst)
{
    if (len < 8) return -1;

    size_t pos = 8;  /* 跳过 unique_count(4) + total_count(4) */

    while (pos + 3 <= len) {  /* 至少需要 char_count(2) + flags(1) */
        uint16_t char_count = r_u16(data + pos);
        uint8_t  flags      = data[pos + 2];
        /* WPS 中 flags&1 == 1 表示 Unicode (2 字节/字符) */
        size_t bytes_per_char = (flags & 1) ? 2 : 1;
        size_t byte_len       = (size_t)char_count * bytes_per_char;
        size_t text_pos       = pos + 3;

        if (text_pos + byte_len > len)
            break;

        /* 将文本转为 UTF-8 */
        size_t utf8_len = 0;
        for (uint16_t ci = 0; ci < char_count; ci++) {
            if (bytes_per_char == 2) {
                uint16_t ch = r_u16(data + text_pos + ci * 2);
                if (ch < 0x80)      utf8_len += 1;
                else if (ch < 0x800) utf8_len += 2;
                else                 utf8_len += 3;
            } else {
                unsigned char c = data[text_pos + ci];
                utf8_len += (c < 0x80) ? 1 : 2;  /* 假设是 Latin-1 */
            }
        }

        char *utf8 = (char *)malloc(utf8_len + 1);
        if (!utf8) return -1;

        size_t out_pos = 0;
        for (uint16_t ci = 0; ci < char_count; ci++) {
            if (bytes_per_char == 2) {
                uint16_t ch = r_u16(data + text_pos + ci * 2);
                if (ch < 0x80) {
                    utf8[out_pos++] = (char)ch;
                } else if (ch < 0x800) {
                    utf8[out_pos++] = (char)(0xC0 | (ch >> 6));
                    utf8[out_pos++] = (char)(0x80 | (ch & 0x3F));
                } else {
                    utf8[out_pos++] = (char)(0xE0 | (ch >> 12));
                    utf8[out_pos++] = (char)(0x80 | ((ch >> 6) & 0x3F));
                    utf8[out_pos++] = (char)(0x80 | (ch & 0x3F));
                }
            } else {
                unsigned char c = data[text_pos + ci];
                if (c < 0x80) {
                    utf8[out_pos++] = (char)c;
                } else {
                    utf8[out_pos++] = (char)(0xC0 | (c >> 6));
                    utf8[out_pos++] = (char)(0x80 | (c & 0x3F));
                }
            }
        }
        utf8[out_pos] = '\0';

        sst_add(sst, utf8, out_pos);
        free(utf8);

        pos = text_pos + byte_len;
    }

    return 0;
}

/* ================================================================
 * 单元格数据存储
 * ================================================================ */

/* 一个单元格：行、列、文本 */
typedef struct {
    uint16_t row;
    uint16_t col;
    char    *text;
} etb_cell_t;

/* 按行分组的单元格集合（最大支持的行数） */
#define ETB_MAX_ROWS 65536

/* 单元格列表：每个条目标记该行第一个单元格的索引 */
typedef struct {
    etb_cell_t *cells;
    size_t      cell_count;
    size_t      cell_capacity;
    size_t      max_row;   /* 最大行号 */
    size_t      max_col;   /* 最大列号 */
} etb_cell_list_t;

static void cell_list_init(etb_cell_list_t *cl)
{
    cl->cells        = NULL;
    cl->cell_count   = 0;
    cl->cell_capacity = 0;
    cl->max_row      = 0;
    cl->max_col      = 0;
}

static void cell_list_free(etb_cell_list_t *cl)
{
    if (!cl) return;
    for (size_t i = 0; i < cl->cell_count; i++)
        free(cl->cells[i].text);
    free(cl->cells);
    cl->cells        = NULL;
    cl->cell_count   = 0;
    cl->cell_capacity = 0;
}

static int cell_list_add(etb_cell_list_t *cl,
                         uint16_t row, uint16_t col,
                         const char *text, size_t text_len)
{
    if (cl->cell_count >= cl->cell_capacity) {
        size_t new_cap = cl->cell_capacity ? cl->cell_capacity * 2 : 128;
        etb_cell_t *new_arr = (etb_cell_t *)realloc(
            cl->cells, new_cap * sizeof(etb_cell_t));
        if (!new_arr) return -1;
        cl->cells         = new_arr;
        cl->cell_capacity = new_cap;
    }

    cl->cells[cl->cell_count].row  = row;
    cl->cells[cl->cell_count].col  = col;
    cl->cells[cl->cell_count].text = (char *)malloc(text_len + 1);
    if (!cl->cells[cl->cell_count].text) return -1;
    memcpy(cl->cells[cl->cell_count].text, text, text_len);
    cl->cells[cl->cell_count].text[text_len] = '\0';
    cl->cell_count++;

    if ((size_t)row + 1 > cl->max_row) cl->max_row = (size_t)row + 1;
    if ((size_t)col + 1 > cl->max_col) cl->max_col = (size_t)col + 1;

    return 0;
}

/* 输出单元格列表为表格文本 */
static void cell_list_output(etb_cell_list_t *cl, strbuf_t *out)
{
    if (cl->cell_count == 0) return;

    /* 按行、列排序（冒泡排序，因为数据量通常很小） */
    /* 对每个单元格，找到它的位置然后输出 */
    for (size_t r = 0; r < cl->max_row; r++) {
        int has_cells_in_row = 0;

        for (size_t c = 0; c < cl->max_col; c++) {
            /* 查找 (r, c) 位置的单元格 */
            const char *text = NULL;
            for (size_t i = 0; i < cl->cell_count; i++) {
                if (cl->cells[i].row == r && cl->cells[i].col == c) {
                    text = cl->cells[i].text;
                    break;
                }
            }

            if (text) {
                strbuf_append_str(out, text);
                has_cells_in_row = 1;
            }

            /* 列间追加制表符 */
            if (c + 1 < cl->max_col) {
                strbuf_append_char(out, '\t');
            }
        }

        if (has_cells_in_row) {
            strbuf_append_char(out, '\n');
        }
    }
}

/* ================================================================
 * 核心提取函数
 * ================================================================ */

static int et_binary_extract_int(ole2_ctx_t *ole, char **out_text,
                                 size_t *out_len)
{
    uint8_t *wb_data = NULL;
    size_t   wb_size = 0;
    uint32_t start;
    uint64_t stream_size;
    int rc;

    etb_sst_t sst;
    etb_cell_list_t cells;

    strbuf_t sb;
    int sheet_index = 0;
    int in_sheet = 0;

    if (!ole || !out_text)
        return WPSEXT_ERR_INVALID_ARG;

    *out_text = NULL;
    if (out_len) *out_len = 0;

    /* 1. 读取 Workbook stream */
    if (ole2_find_stream(ole, "Workbook", &start, &stream_size) != 0)
        return WPSEXT_ERR_FORMAT;
    rc = ole2_read_stream(ole, start, stream_size, &wb_data, &wb_size);
    if (rc != WPSEXT_OK) return rc;

    /* 2. 初始化 */
    sst_init(&sst);
    cell_list_init(&cells);
    strbuf_init(&sb);

    /* 3. 一次性遍历所有 BIFF 记录 */
    size_t pos = 0;
    while (pos + 4 <= wb_size) {
        uint16_t rec_type = r_u16(wb_data + pos);
        uint16_t rec_len  = r_u16(wb_data + pos + 2);
        const uint8_t *data = wb_data + pos + 4;

        /* 检查记录是否超出边界 */
        if ((size_t)rec_len > wb_size - pos - 4)
            break;

        switch (rec_type) {

        case BIFF_WPS_BOF:
        {
            /* WPS BOF: 前 4 字节含版本(2) + 类型(2) */
            /* 类型: 0x0005 = 全局限, 0x0010 = 工作表 */
            uint16_t bof_type = 0;
            if (rec_len >= 4)
                bof_type = r_u16(data + 2);

            /* 跳过全局限（仅处理工作表） */
            if (bof_type != 0x0010)
                break;

            /* 遇到新的 BOF 且不在 sheet 中 = 新工作表开始 */
            /* 遇到 BOF 但已经在 sheet 中 = 前一个 sheet 自动结束 */
            if (in_sheet && cells.cell_count > 0) {
                if (sheet_index > 1) {
                    strbuf_append_str(&sb, "\n--- Sheet ");
                    char idx_buf[16];
                    int idx_len = snprintf(idx_buf, sizeof(idx_buf), "%d",
                                           sheet_index);
                    if (idx_len > 0)
                        strbuf_append(&sb, idx_buf, (size_t)idx_len);
                    strbuf_append_str(&sb, " ---\n");
                }
                cell_list_output(&cells, &sb);
                cell_list_free(&cells);
                cell_list_init(&cells);
                sheet_index++;
            } else if (!in_sheet) {
                sheet_index++;
            }
            in_sheet = 1;
            break;
        }

        case BIFF_EOF:
            /* 工作表结束 */
            if (in_sheet && cells.cell_count > 0) {
                if (sheet_index > 1) {
                    strbuf_append_str(&sb, "\n--- Sheet ");
                    char idx_buf[16];
                    int idx_len = snprintf(idx_buf, sizeof(idx_buf), "%d",
                                           sheet_index);
                    if (idx_len > 0)
                        strbuf_append(&sb, idx_buf, (size_t)idx_len);
                    strbuf_append_str(&sb, " ---\n");
                }
                cell_list_output(&cells, &sb);
                cell_list_free(&cells);
                cell_list_init(&cells);
            }
            in_sheet = 0;
            break;

        case BIFF_SST:
            /* 共享字符串表 */
            parse_sst_record(data, (size_t)rec_len, &sst);
            break;

        case BIFF_LABELSST:
            /* 单元格引用共享字符串 */
            if (rec_len >= 10) {
                uint16_t row = r_u16(data);
                uint16_t col = r_u16(data + 2);
                uint32_t sst_idx = r_u32(data + 6);

                if (sst_idx < sst.count) {
                    cell_list_add(&cells, row, col,
                                  sst.strings[sst_idx],
                                  strlen(sst.strings[sst_idx]));
                }
            }
            break;

        case BIFF_LABEL:
            /* 单元格内联字符串 */
            if (rec_len >= 7) {
                uint16_t row = r_u16(data);
                uint16_t col = r_u16(data + 2);
                size_t text_byte_len = (size_t)rec_len - 6;

                /* UTF-16LE 文本，转为 UTF-8 */
                uint16_t char_count = (uint16_t)(text_byte_len / 2);

                /* 计算 UTF-8 长度 */
                size_t utf8_len = 0;
                for (uint16_t ci = 0; ci < char_count; ci++) {
                    uint16_t ch = r_u16(data + 6 + ci * 2);
                    if (ch == 0) break; /* 终止符 */
                    if (ch < 0x80)
                        utf8_len += 1;
                    else if (ch < 0x800)
                        utf8_len += 2;
                    else
                        utf8_len += 3;
                }

                char *utf8 = (char *)malloc(utf8_len + 1);
                if (utf8) {
                    size_t out_pos = 0;
                    for (uint16_t ci = 0; ci < char_count; ci++) {
                        uint16_t ch = r_u16(data + 6 + ci * 2);
                        if (ch == 0) break;
                        if (ch < 0x80) {
                            utf8[out_pos++] = (char)ch;
                        } else if (ch < 0x800) {
                            utf8[out_pos++] = (char)(0xC0 | (ch >> 6));
                            utf8[out_pos++] = (char)(0x80 | (ch & 0x3F));
                        } else {
                            utf8[out_pos++] = (char)(0xE0 | (ch >> 12));
                            utf8[out_pos++] = (char)(0x80 | ((ch >> 6) & 0x3F));
                            utf8[out_pos++] = (char)(0x80 | (ch & 0x3F));
                        }
                    }
                    utf8[out_pos] = '\0';
                    cell_list_add(&cells, row, col, utf8, out_pos);
                    free(utf8);
                }
            }
            break;

        default:
            break;
        }

        pos += 4 + (size_t)rec_len;
    }

    /* 如果最后一个 sheet 还在打开状态，输出它 */
    if (in_sheet && cells.cell_count > 0) {
        if (sheet_index > 1) {
            strbuf_append_str(&sb, "\n--- Sheet ");
            char idx_buf[16];
            int idx_len = snprintf(idx_buf, sizeof(idx_buf), "%d", sheet_index);
            if (idx_len > 0)
                strbuf_append(&sb, idx_buf, (size_t)idx_len);
            strbuf_append_str(&sb, " ---\n");
        }
        cell_list_output(&cells, &sb);
    }

    /* 清理末尾空白 */
    while (sb.len > 0 && (sb.data[sb.len - 1] == '\n' ||
                          sb.data[sb.len - 1] == '\t')) {
        sb.len--;
    }
    sb.data[sb.len] = '\0';

    sst_free(&sst);
    cell_list_free(&cells);
    free(wb_data);

    *out_text = strbuf_detach(&sb, out_len);
    return WPSEXT_OK;
}

/* ================================================================
 * 公开接口
 * ================================================================ */

int et_binary_extract_file(const char *path, char **out_text, size_t *out_len)
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

    rc = et_binary_extract_int(ole, out_text, out_len);

    ole2_close(ole);
    return rc;
}
