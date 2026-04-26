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
#include <zlib.h>

/* ================================================================
 * OLE2 / Compound File 基础结构
 *
 * OLE2 header (512 bytes):
 *   0x000: magic (8 bytes): D0 CF 11 E0 A1 B1 1A E1
 *   0x01E: minor version (2)
 *   0x020: major version (2)
 *   0x02E: sector size power (2)  → 实际大小 = 2^val
 *   0x030: mini sector size power (2)
 *   0x038: number of FAT sectors (4)
 *   0x044: first directory sector SECID (4)
 *   0x048: mini stream cutoff size (4)
 *   0x04C: first mini FAT sector SECID (4)
 *   0x050: number of mini FAT sectors (4)
 *   0x054: first DIFAT sector SECID (4)
 *   0x058: number of DIFAT sectors (4)
 *   0x05C: DIFAT array (109 entries × 4 bytes)
 *
 *   SECID = sector index (4 bytes, signed)
 *   FREESECT = 0xFFFFFFFF, ENDOFCHAIN = 0xFFFFFFFE,
 *   FATSECT = 0xFFFFFFFD, DIFSECT = 0xFFFFFFFC
 * ================================================================ */

#define OLE_MAGIC       "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1"
#define OLE_MAGIC_LEN   8
#define OLE_HEADER_SIZE 512
#define SECID_FREESECT  0xFFFFFFFF
#define SECID_ENDOFCHAIN 0xFFFFFFFE
#define SECID_FATSECT   0xFFFFFFFD
#define SECID_DIFSECT   0xFFFFFFFC

/* OLE2 上下文 */
typedef struct {
    FILE   *fp;
    size_t  file_size;

    /* 扇区参数 */
    size_t  sect_size;
    size_t  mini_sect_size;

    /* FAT 表 */
    uint32_t *fat;
    size_t    fat_count;

    /* Mini FAT */
    uint32_t *mini_fat;
    size_t    mini_fat_count;

    /* 目录入口 */
    uint8_t  *dir_entries;
    size_t    dir_entry_count;

    /* Mini stream (小文件数据) */
    uint8_t  *mini_stream;
    size_t    mini_stream_size;

    /* 第一个目录 sector */
    uint32_t  first_dir_secid;
    /* Mini stream 位置 */
    uint32_t  mini_stream_start;
} ole2_ctx_t;

/* ---- 辅助函数 -------------------------------------------------- */

static uint16_t r_u16(const uint8_t *p);
static uint32_t r_u32(const uint8_t *p);
static uint64_t r_u64(const uint8_t *p);
static void ole2_close(ole2_ctx_t *ole);
static int wps_binary_extract_int(ole2_ctx_t *ole, char **out_text, size_t *out_len);

static uint16_t r_u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t r_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t r_u64(const uint8_t *p) {
    return (uint64_t)r_u32(p) | ((uint64_t)r_u32(p+4) << 32);
}

/* 读取一个扇区 */
static int read_sector(ole2_ctx_t *ole, uint32_t secid,
                       uint8_t *buf, size_t buf_size)
{
    if (secid >= SECID_ENDOFCHAIN) return -1;
    size_t offset = OLE_HEADER_SIZE + (size_t)secid * ole->sect_size;
    if (offset + ole->sect_size > ole->file_size) return -1;
    if (ole->sect_size > buf_size) return -1;

    fseek(ole->fp, (long)offset, SEEK_SET);
    if (fread(buf, 1, ole->sect_size, ole->fp) != ole->sect_size)
        return -1;
    return 0;
}

/* ================================================================
 * OLE2 打开/关闭
 * ================================================================ */
static int read_fat_chain(ole2_ctx_t *ole, uint32_t start_secid,
                          size_t len, uint8_t *out)
{
    uint32_t secid = start_secid;
    size_t remain = len;
    size_t offset = 0;

    while (secid != SECID_ENDOFCHAIN && remain > 0) {
        size_t copy_size = (remain < ole->sect_size) ? remain : ole->sect_size;

        size_t src_off = OLE_HEADER_SIZE + (size_t)secid * ole->sect_size;
        if (src_off + copy_size > ole->file_size)
            return -1;

        fseek(ole->fp, (long)src_off, SEEK_SET);
        if (fread(out + offset, 1, copy_size, ole->fp) != copy_size)
            return -1;

        offset += copy_size;
        remain -= copy_size;

        if (secid >= ole->fat_count)
            break;
        secid = ole->fat[secid];
    }

    return (remain == 0) ? 0 : -1;
}

/* ---- OLE2 打开/关闭 -------------------------------------------- */

static ole2_ctx_t *ole2_open(const char *path)
{
    ole2_ctx_t *ole;
    uint8_t header[OLE_HEADER_SIZE];

    ole = (ole2_ctx_t *)calloc(1, sizeof(ole2_ctx_t));
    if (!ole) return NULL;

    ole->fp = fopen(path, "rb");
    if (!ole->fp) { free(ole); return NULL; }

    fseek(ole->fp, 0, SEEK_END);
    ole->file_size = (size_t)ftell(ole->fp);
    rewind(ole->fp);

    /* 读 header */
    if (fread(header, 1, OLE_HEADER_SIZE, ole->fp) != OLE_HEADER_SIZE) {
        ole2_close(ole);
        return NULL;
    }

    /* 验证 magic */
    if (memcmp(header, OLE_MAGIC, OLE_MAGIC_LEN) != 0) {
        ole2_close(ole);
        return NULL;
    }

    /* 扇区参数 */
    /* OLE2 header offsets:
     * 0x18: minor version (2)
     * 0x1A: major version (2)
     * 0x1C: byte order (2) = 0xFFFE (little-endian)
     * 0x1E: sector size power (2)  -> 实际大小 = 2^val
     * 0x20: mini sector size power (2)
     */
    uint16_t sect_shift_val = r_u16(header + 0x1E);
    uint16_t mini_shift_val = r_u16(header + 0x20);

    ole->sect_size = (size_t)1 << sect_shift_val;
    ole->mini_sect_size = (size_t)1 << mini_shift_val;

    /* FAT 数量、第一个目录 sector */
    uint32_t num_fat = r_u32(header + 0x2C);
    ole->first_dir_secid = r_u32(header + 0x30);
    uint32_t mini_cutoff = r_u32(header + 0x38);
    uint32_t first_minifat = r_u32(header + 0x3C);
    uint32_t num_minifat = r_u32(header + 0x40);

    (void)mini_cutoff;

    /* ---- 读取 FAT ---- */
    /* DIFAT: 前 109 个在 header 中 */
    #define DIFAT_ARRAY_OFFSET 0x4C
    uint32_t difat[109];
    memcpy(difat, header + DIFAT_ARRAY_OFFSET, 109 * 4);

    /* 统计 FAT 扇区总数 */
    uint32_t total_fat_sectors = num_fat;
    /* 如果还有额外的 DIFAT 扇区... 简化处理 */
    /* 对于小文件，109 个 DIFAT 就够了 */
    if (total_fat_sectors == 0) {
        ole2_close(ole);
        return NULL;
    }

    /* 读取 FAT 到内存 */
    ole->fat_count = ole->sect_size / 4 * total_fat_sectors;
    ole->fat = (uint32_t *)malloc(ole->fat_count * sizeof(uint32_t));
    if (!ole->fat) { ole2_close(ole); return NULL; }

    size_t fat_offset = 0;
    for (uint32_t f = 0; f < total_fat_sectors; f++) {
        uint32_t s = difat[f];
        if (s >= SECID_ENDOFCHAIN) break;

        uint8_t *sector_buf = (uint8_t *)malloc(ole->sect_size);
        if (!sector_buf) { ole2_close(ole); return NULL; }

        if (read_sector(ole, s, sector_buf, ole->sect_size) != 0) {
            free(sector_buf);
            ole2_close(ole);
            return NULL;
        }

        size_t entries = ole->sect_size / 4;
        for (size_t j = 0; j < entries && fat_offset < ole->fat_count; j++) {
            ole->fat[fat_offset++] = r_u32(sector_buf + j * 4);
        }
        free(sector_buf);
    }

    /* ---- 读取 Mini FAT ---- */
    if (num_minifat > 0) {
        ole->mini_fat_count = ole->sect_size / 4 * num_minifat;
        ole->mini_fat = (uint32_t *)malloc(ole->mini_fat_count * sizeof(uint32_t));
        if (!ole->mini_fat) { ole2_close(ole); return NULL; }

        /* 读 Mini FAT 链 */
        /* 需要先读 Mini FAT 数据（它自己是存储在一个标准流中的） */
        size_t mf_size = num_minifat * ole->sect_size;
        uint8_t *mf_data = (uint8_t *)malloc(mf_size);
        if (!mf_data) { ole2_close(ole); return NULL; }
        if (read_fat_chain(ole, first_minifat, mf_size, mf_data) != 0) {
            free(mf_data);
            ole2_close(ole);
            return NULL;
        }

        size_t mf_entries = mf_size / 4;
        for (size_t j = 0; j < mf_entries && j < ole->mini_fat_count; j++)
            ole->mini_fat[j] = r_u32(mf_data + j * 4);

        free(mf_data);
    }

    /* ---- 读取目录 ---- */
    /* 先获取目录大小 */
    {
        uint32_t secid = ole->first_dir_secid;
        size_t dir_size = 0;
        while (secid != SECID_ENDOFCHAIN) {
            dir_size += ole->sect_size;
            if (secid >= ole->fat_count) break;
            secid = ole->fat[secid];
        }

        ole->dir_entries = (uint8_t *)malloc(dir_size);
        if (!ole->dir_entries) { ole2_close(ole); return NULL; }

        if (read_fat_chain(ole, ole->first_dir_secid, dir_size, ole->dir_entries) != 0) {
            ole2_close(ole);
            return NULL;
        }

        /* 每个目录条目 128 字节 */
        ole->dir_entry_count = dir_size / 128;
    }

    /* ---- 读取 Mini Stream ---- */
    /* 根目录条目（第一个）的流数据是 Mini Stream */
    ole->mini_stream = NULL;
    ole->mini_stream_size = 0;
    ole->mini_stream_start = SECID_ENDOFCHAIN;

    if (ole->dir_entry_count > 0) {
        uint8_t *root = ole->dir_entries;  /* 128 bytes */

        /* 根目录的流存储在标准 FAT 链中 */
        uint32_t root_start = r_u32(root + 0x74);
        uint64_t root_size  = r_u64(root + 0x78);

        if (root_start < SECID_ENDOFCHAIN && root_size > 0 && root_size <= 100 * 1024 * 1024) {
            ole->mini_stream = (uint8_t *)malloc((size_t)root_size);
            if (ole->mini_stream) {
                if (read_fat_chain(ole, root_start, (size_t)root_size, ole->mini_stream) == 0) {
                    ole->mini_stream_size = (size_t)root_size;
                    ole->mini_stream_start = root_start;
                } else {
                    free(ole->mini_stream);
                    ole->mini_stream = NULL;
                }
            }
        }
    }

    return ole;
}

static void ole2_close(ole2_ctx_t *ole)
{
    if (!ole) return;
    if (ole->fp) fclose(ole->fp);
    free(ole->fat);
    free(ole->mini_fat);
    free(ole->dir_entries);
    free(ole->mini_stream);
    free(ole);
}

/* ---- 目录条目结构 ---- */
/* 每个目录条目 128 字节:
 *   0x00: 名称 (64 字节, UTF-16LE, 以 0 终止)
 *   0x40: 名称长度 (2)
 *   0x42: 对象类型 (1): 0=unknown, 1=storage, 2=stream, 5=root
 *   0x43: 颜色标志 (1)
 *   0x44: 左兄弟 (4)
 *   0x48: 右兄弟 (4)
 *   0x4C: 子节点 (4)
 *   0x50: CLSID (16)
 *   0x60: 状态位 (4)
 *   0x64: 创建时间 (8)
 *   0x6C: 修改时间 (8)
 *   0x74: 起始 SECID (4)  — for streams
 *   0x78: 流大小 (8)      — for streams
 */

/* 在目录中按名称查找流 */
static int ole2_find_stream(ole2_ctx_t *ole, const char *name,
                            uint32_t *out_start, uint64_t *out_size)
{
    size_t name_len = strlen(name);

    for (size_t i = 0; i < ole->dir_entry_count; i++) {
        uint8_t *entry = ole->dir_entries + i * 128;

        /* 名称在目录条目中存储为 UTF-16LE，最多 32 个字符 (64 字节) */
        uint16_t entry_name_len = r_u16(entry + 0x40);
        if (entry_name_len < 2 || entry_name_len > 64)
            continue;

        /* 将 UTF-16LE 名称转换为 UTF-8 比较 */
        /* 简化: 假设 ASCII 名称 */
        size_t entry_char_len = (entry_name_len - 2) / 2;  /* 减去结尾的 0x0000 */
        if (entry_char_len != name_len)
            continue;

        int match = 1;
        for (size_t j = 0; j < name_len; j++) {
            uint16_t ch = r_u16(entry + j * 2);
            if ((char)ch != name[j]) {
                match = 0;
                break;
            }
        }

        if (match) {
            uint8_t type = entry[0x42];
            if (type == 2) {  /* stream */
                *out_start = r_u32(entry + 0x74);
                *out_size  = r_u64(entry + 0x78);
                return 0;
            }
        }
    }

    return -1;
}

/* ================================================================
 * WPS Binary 文本提取
 * ================================================================ */

/* 读取流的全部内容 */
static int ole2_read_stream(ole2_ctx_t *ole, uint32_t start_secid,
                            uint64_t stream_size, uint8_t **out_data,
                            size_t *out_size)
{
    if (stream_size == 0 || stream_size > 1024 * 1024 * 100)  /* max 100MB */
        return WPSEXT_ERR_FORMAT;

    *out_data = (uint8_t *)malloc((size_t)stream_size + 1);
    if (!*out_data)
        return WPSEXT_ERR_MEMORY;

    int rc;
    /* Mini stream cutoff size: 流大小小于 4096 时使用 Mini Storage */
    /* 也可以用 start_secid 是否属于 mini FAT 范围来判断 */
    if (stream_size < 4096 && start_secid < ole->mini_fat_count && ole->mini_stream) {
        /* 小流：通过 Mini FAT 从 Mini Stream 读取 */
        size_t remain = (size_t)stream_size;
        size_t offset = 0;
        uint32_t secid = start_secid;

        while (secid != SECID_ENDOFCHAIN && remain > 0) {
            size_t copy_size = (remain < ole->mini_sect_size) ? remain : ole->mini_sect_size;
            size_t src_off = (size_t)secid * ole->mini_sect_size;

            if (src_off + copy_size > ole->mini_stream_size) {
                free(*out_data);
                *out_data = NULL;
                return WPSEXT_ERR_FILE;
            }

            memcpy(*out_data + offset, ole->mini_stream + src_off, copy_size);
            offset += copy_size;
            remain -= copy_size;

            if (secid >= ole->mini_fat_count)
                break;
            secid = ole->mini_fat[secid];
        }

        rc = (remain == 0) ? WPSEXT_OK : WPSEXT_ERR_FORMAT;
    } else {
        /* 标准流：通过 FAT 链读取 */
        rc = read_fat_chain(ole, start_secid, (size_t)stream_size, *out_data);
    }

    if (rc != WPSEXT_OK) {
        free(*out_data);
        *out_data = NULL;
        return WPSEXT_ERR_FILE;
    }

    (*out_data)[(size_t)stream_size] = '\0';
    *out_size = (size_t)stream_size;
    return WPSEXT_OK;
}

/* ---- 核心提取函数 ---- */

/**
 * @brief 从 OLE2 路径提取旧版 WPS 二进制格式文本
 */
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

/* 内部提取函数（需要已打开的 OLE2 上下文） */
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
    if (ole2_find_stream(ole, "WordDocument", &start, &stream_size) != 0) {
        return WPSEXT_ERR_FORMAT;
    }
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
    if (rgw_end + 2 + 4 * 4 > wd_size) {  /* at least 4 longs after cslw */
        free(wd_data); free(tbl_data);
        return WPSEXT_ERR_FORMAT;
    }
    uint32_t ccpText = r_u32(wd_data + rgw_end + 2 + 3 * 4);

    if (ccpText == 0 || ccpText > 1000000) {
        /* 可能是空的或超大 */
        free(wd_data); free(tbl_data);
        return WPSEXT_ERR_FORMAT;
    }

    /* 4. 在 0Table 中找到 Pcdt (type = 0x02) */
    /* 搜索所有 type=0x02 的 Pcdt */
    int found_pcdt = 0;
    uint32_t fc = 0;
    int is_compressed = 0;

    for (size_t i = 0; i + 5 < tbl_size; i++) {
        if (tbl_data[i] == 0x02) {
            uint32_t lcb = r_u32(tbl_data + i + 1);
            if (lcb < 8 || lcb > tbl_size - i)
                continue;

            /* 尝试计算 piece 数 */
            /* WPS 格式: lcb = 4 + n * 12, 所以 n = (lcb - 4) / 12 */
            int n = (int)((lcb - 4) / 12);
            if (n < 0 || n > 1000) continue;
            if ((lcb - 4) % 12 != 0) continue;

            size_t cp_start = i + 5;
            size_t pcd_start = cp_start + (size_t)(n + 1) * 4;

            if (pcd_start + (size_t)n * 8 > tbl_size)
                continue;

            /* 检查 CP[0] == 0 */
            uint32_t cp0 = r_u32(tbl_data + cp_start);
            uint32_t cp1 = r_u32(tbl_data + cp_start + 4);

            if (cp0 == 0 && cp1 == ccpText) {
                /* 找到了匹配的 Pcdt */
                /* 读取 PCD[0] */
                uint8_t *pcd = tbl_data + pcd_start;
                uint32_t flda = r_u32(pcd + 2);
                /* 或者 WPS PCD 结构可能是不同的字节排列 */
                /* 检查几种可能性 */

                /* WPS format: PCD = [2 bytes prm][4 bytes fc][2 bytes flags] */
                /* 或者 [6 bytes data][2 bytes ...] */
                /* 从数据中我们看到: 0x0000000800000000
                 * 这意味着 bytes 2-5 = 0x00000800 = 2048 */
                uint32_t fc_candidate = r_u32(pcd + 2);
                if (fc_candidate > 0 && fc_candidate < wd_size) {
                    fc = fc_candidate;
                    is_compressed = 0;  /* WPS uses uncompressed (UTF-16LE) */
                    found_pcdt = 1;
                    break;
                }

                /* 也试试 bytes 0-3 */
                fc_candidate = r_u32(pcd);
                if (fc_candidate > 0 && fc_candidate < wd_size) {
                    fc = fc_candidate;
                    is_compressed = 0;
                    found_pcdt = 1;
                    break;
                }

                /* 试试 flda 格式 (bit 30 = compressed flag) */
                fc_candidate = flda & 0x3FFFFFFF;
                if (fc_candidate > 0 && fc_candidate < wd_size) {
                    fc = fc_candidate;
                    is_compressed = (flda & 0x40000000) ? 1 : 0;
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
        /* 1 byte per char (Latin-1 / ANSI) */
        if (fc + ccpText > wd_size) {
            strbuf_free(&sb);
            free(wd_data); free(tbl_data);
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
        /* 2 bytes per char (UTF-16LE) — WPS 使用这种模式 */
        uint32_t byte_len = ccpText * 2;
        if (fc + byte_len > wd_size) {
            strbuf_free(&sb);
            free(wd_data); free(tbl_data);
            return WPSEXT_ERR_FORMAT;
        }
        for (uint32_t j = 0; j < ccpText; j++) {
            uint16_t ch = r_u16(wd_data + fc + j * 2);
            if (ch == 0x000D || ch == 0x000B)  /* CR 或分页符 */
                strbuf_append_char(&sb, '\n');
            else if (ch == 0x0007)  /* 制表符 */
                strbuf_append_char(&sb, '\t');
            else if (ch >= 0x20 && ch <= 0x7E)
                strbuf_append_char(&sb, (char)ch);
            else if (ch >= 0x80) {
                /* 非 ASCII 字符（中文字符等），编码为 UTF-8 */
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

    /* 去除末尾多余的换行 */
    while (sb.len > 0 && sb.data[sb.len - 1] == '\n')
        sb.len--;
    sb.data[sb.len] = '\0';

    *out_text = strbuf_detach(&sb, out_len);

    free(wd_data);
    free(tbl_data);

    return WPSEXT_OK;
}

/* ---- OLE2 magic 检查 ---- */
int ole2_check_magic(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;

    char magic[8];
    int ret = 0;
    if (fread(magic, 1, 8, fp) == 8 &&
        memcmp(magic, OLE_MAGIC, 8) == 0)
        ret = 1;

    fclose(fp);
    return ret;
}
