/*
 * ole2_reader.c - OLE2 复合文档读取器
 *
 * 提供 OLE2 文件的打开、关闭、流查找和读取功能。
 * 被 wps_binary.c 和 et_binary.c 共享使用。
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
 */

#include "internal.h"

#define OLE_MAGIC       "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1"
#define OLE_MAGIC_LEN   8
#define OLE_HEADER_SIZE 512
#define SECID_FREESECT  0xFFFFFFFF
#define SECID_ENDOFCHAIN 0xFFFFFFFE
#define SECID_FATSECT   0xFFFFFFFD
#define SECID_DIFSECT   0xFFFFFFFC

/* ---- 辅助: 小端序读取 ------------------------------------------ */

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

/* ---- 读取一个扇区 ---------------------------------------------- */

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

/* ---- 通过 FAT 链读取数据 --------------------------------------- */

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

/* ---- OLE2 打开 ------------------------------------------------- */

ole2_ctx_t *ole2_open(const char *path)
{
    ole2_ctx_t *ole;
    uint8_t header[OLE_HEADER_SIZE];

    if (!path) return NULL;

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
    uint16_t sect_shift_val = r_u16(header + 0x1E);
    uint16_t mini_shift_val = r_u16(header + 0x20);
    ole->sect_size = (size_t)1 << sect_shift_val;
    ole->mini_sect_size = (size_t)1 << mini_shift_val;

    uint32_t num_fat       = r_u32(header + 0x2C);
    ole->first_dir_secid   = r_u32(header + 0x30);
    uint32_t mini_cutoff   = r_u32(header + 0x38);
    uint32_t first_minifat = r_u32(header + 0x3C);
    uint32_t num_minifat   = r_u32(header + 0x40);
    (void)mini_cutoff;

    /* ---- 读取 FAT ---- */
    #define DIFAT_ARRAY_OFFSET 0x4C
    uint32_t difat[109];
    memcpy(difat, header + DIFAT_ARRAY_OFFSET, 109 * 4);

    uint32_t total_fat_sectors = num_fat;
    if (total_fat_sectors == 0) { ole2_close(ole); return NULL; }

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
        for (size_t j = 0; j < entries && fat_offset < ole->fat_count; j++)
            ole->fat[fat_offset++] = r_u32(sector_buf + j * 4);
        free(sector_buf);
    }

    /* ---- 读取 Mini FAT ---- */
    if (num_minifat > 0) {
        ole->mini_fat_count = ole->sect_size / 4 * num_minifat;
        ole->mini_fat = (uint32_t *)malloc(ole->mini_fat_count * sizeof(uint32_t));
        if (!ole->mini_fat) { ole2_close(ole); return NULL; }

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
        ole->dir_entry_count = dir_size / 128;
    }

    /* ---- 读取 Mini Stream ---- */
    ole->mini_stream = NULL;
    ole->mini_stream_size = 0;
    ole->mini_stream_start = SECID_ENDOFCHAIN;

    if (ole->dir_entry_count > 0) {
        uint8_t *root = ole->dir_entries;
        uint32_t root_start = r_u32(root + 0x74);
        uint64_t root_size  = r_u64(root + 0x78);

        if (root_start < SECID_ENDOFCHAIN && root_size > 0 &&
            root_size <= 100 * 1024 * 1024) {
            ole->mini_stream = (uint8_t *)malloc((size_t)root_size);
            if (ole->mini_stream) {
                if (read_fat_chain(ole, root_start, (size_t)root_size,
                                   ole->mini_stream) == 0) {
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

/* ---- OLE2 关闭 ------------------------------------------------- */

void ole2_close(ole2_ctx_t *ole)
{
    if (!ole) return;
    if (ole->fp) fclose(ole->fp);
    free(ole->fat);
    free(ole->mini_fat);
    free(ole->dir_entries);
    free(ole->mini_stream);
    free(ole);
}

/* ---- 在目录中按名称查找流 -------------------------------------- */

int ole2_find_stream(ole2_ctx_t *ole, const char *name,
                     uint32_t *out_start, uint64_t *out_size)
{
    size_t name_len = strlen(name);

    if (!ole || !name || !out_start || !out_size)
        return -1;

    for (size_t i = 0; i < ole->dir_entry_count; i++) {
        uint8_t *entry = ole->dir_entries + i * 128;
        uint16_t entry_name_len = r_u16(entry + 0x40);
        if (entry_name_len < 2 || entry_name_len > 64)
            continue;

        size_t entry_char_len = (entry_name_len - 2) / 2;
        if (entry_char_len != name_len)
            continue;

        int match = 1;
        for (size_t j = 0; j < name_len; j++) {
            uint16_t ch = r_u16(entry + j * 2);
            if ((char)ch != name[j]) { match = 0; break; }
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

/* ---- 读取流的全部内容 ------------------------------------------ */

int ole2_read_stream(ole2_ctx_t *ole, uint32_t start_secid,
                     uint64_t stream_size, uint8_t **out_data,
                     size_t *out_size)
{
    if (!ole || !out_data || !out_size)
        return WPSEXT_ERR_INVALID_ARG;

    if (stream_size == 0 || stream_size > 1024 * 1024 * 100)
        return WPSEXT_ERR_FORMAT;

    *out_data = (uint8_t *)malloc((size_t)stream_size + 1);
    if (!*out_data)
        return WPSEXT_ERR_MEMORY;

    int rc;
    if (stream_size < 4096 && start_secid < ole->mini_fat_count &&
        ole->mini_stream) {
        /* 小流：通过 Mini FAT 从 Mini Stream 读取 */
        size_t remain = (size_t)stream_size;
        size_t offset = 0;
        uint32_t secid = start_secid;

        while (secid != SECID_ENDOFCHAIN && remain > 0) {
            size_t copy_size = (remain < ole->mini_sect_size) ?
                               remain : ole->mini_sect_size;
            size_t src_off = (size_t)secid * ole->mini_sect_size;

            if (src_off + copy_size > ole->mini_stream_size) {
                free(*out_data);
                *out_data = NULL;
                return WPSEXT_ERR_FILE;
            }

            memcpy(*out_data + offset, ole->mini_stream + src_off, copy_size);
            offset += copy_size;
            remain -= copy_size;

            if (secid >= ole->mini_fat_count) break;
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

/* ---- OLE2 magic 检查 ------------------------------------------- */

int ole2_check_magic(const char *path)
{
    FILE *fp;
    char magic[8];

    if (!path) return 0;
    fp = fopen(path, "rb");
    if (!fp) return 0;

    int ret = 0;
    if (fread(magic, 1, 8, fp) == 8 &&
        memcmp(magic, OLE_MAGIC, 8) == 0)
        ret = 1;

    fclose(fp);
    return ret;
}
