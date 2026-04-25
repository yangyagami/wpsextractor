/*
 * zip_reader.c - 简易 ZIP 文件读取器
 *
 * 支持: store (method=0) 和 deflate (method=8, 需要 zlib)
 *
 * ZIP 文件结构:
 *   [local file header 1]
 *   [file data 1]
 *   [local file header 2]
 *   [file data 2]
 *   ...
 *   [central directory entry 1]
 *   [central directory entry 2]
 *   ...
 *   [end of central directory record]
 */

#include "internal.h"
#include <zlib.h>

/* ---- 内部结构 -------------------------------------------------- */

#define ZIP_LOCAL_SIG       0x04034b50
#define ZIP_CENTRAL_SIG     0x02014b50
#define ZIP_EOCD_SIG        0x06054b50
#define ZIP_MAX_ENTRIES     4096
#define ZIP_MAX_ENTRY_NAME  1024

struct zip_reader {
    FILE        *fp;
    size_t       file_size;
    zip_entry_t *entries;
    size_t       entry_count;
};

/* ---- 辅助: 读取小端序数值 -------------------------------------- */

static uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ---- 定位 EOCD 并解析 ------------------------------------------ */

/*
 * EOCD 结构 (最少 22 字节):
 *   offset 0:  signature (4) = 0x06054b50
 *   offset 4:  disk number (2)
 *   offset 6:  disk with central dir (2)
 *   offset 8:  entries on this disk (2)
 *   offset 10: total entries (2)
 *   offset 12: central dir size (4)
 *   offset 16: central dir offset (4)
 *   offset 20: comment length (2)
 *   ... comment ...
 *
 * 注意: EOCD 可能有 comment，末尾不一定固定长度，
 *       需从文件末尾向前搜索 signature。
 */
static int find_eocd(FILE *fp, size_t file_size,
                     uint16_t *out_total, uint32_t *out_cd_offset,
                     uint32_t *out_cd_size)
{
    uint8_t buf[1024 + 22];  /* 最多搜索 1KB 的尾部 */
    size_t search_size;

    search_size = (file_size < 1024 + 22) ? file_size : 1024 + 22;
    if (search_size < 22) return -1;

    fseek(fp, (long)(file_size - search_size), SEEK_SET);
    if (fread(buf, 1, search_size, fp) != search_size) return -1;

    /* 从后往前搜索 EOCD signature */
    for (size_t i = search_size - 22; ; i--) {
        if (read_u32(buf + i) == ZIP_EOCD_SIG) {
            *out_total     = read_u16(buf + i + 10);
            *out_cd_size   = read_u32(buf + i + 12);
            *out_cd_offset = read_u32(buf + i + 16);

            /* 如果注解长度非 0，检验 i + 22 + comment_len == search_size */
            uint16_t comment_len = read_u16(buf + i + 20);
            if (i + 22 + comment_len == search_size)
                return 0;  /* 找到 */
            /* 否则继续搜索 */
        }
        if (i == 0) break;
    }

    return -1;
}

/* ---- 解析中央目录条目 ------------------------------------------ */

/*
 * Central Directory Entry 结构:
 *   offset 0:  signature (4) = 0x02014b50
 *   offset 4:  version made by (2)
 *   offset 6:  version needed (2)
 *   offset 8:  flags (2)
 *   offset 10: method (2)
 *   offset 12: mod time (2)
 *   offset 14: mod date (2)
 *   offset 16: crc32 (4)
 *   offset 20: compressed size (4)
 *   offset 24: uncompressed size (4)
 *   offset 28: filename length (2)
 *   offset 30: extra field length (2)
 *   offset 32: comment length (2)
 *   offset 34: disk number start (2)
 *   offset 36: internal attrs (2)
 *   offset 38: external attrs (4)
 *   offset 42: local header offset (4)
 *   ... filename ...
 */
static int parse_central_entry(const uint8_t *data, zip_entry_t *entry)
{
    uint16_t name_len, extra_len, comment_len;

    if (read_u32(data) != ZIP_CENTRAL_SIG)
        return -1;

    entry->method        = read_u16(data + 10);
    entry->crc32         = read_u32(data + 16);
    entry->compressed    = read_u32(data + 20);
    entry->uncompressed  = read_u32(data + 24);
    name_len             = read_u16(data + 28);
    extra_len            = read_u16(data + 30);
    comment_len          = read_u16(data + 32);
    entry->offset        = read_u32(data + 42);

    if (name_len == 0 || name_len >= ZIP_MAX_ENTRY_NAME)
        return -1;

    entry->name = (char *)malloc(name_len + 1);
    if (!entry->name) return -1;

    memcpy(entry->name, data + 46, name_len);
    entry->name[name_len] = '\0';

    return 46 + name_len + extra_len + comment_len;
}

static int load_central_dir(zip_reader_t *zr,
                            uint32_t cd_offset, uint32_t cd_size,
                            uint16_t total_entries)
{
    uint8_t *cd_data;
    size_t n;
    int consumed;

    if (total_entries == 0 || total_entries > ZIP_MAX_ENTRIES)
        return -1;

    cd_data = (uint8_t *)malloc(cd_size);
    if (!cd_data) return -1;

    fseek(zr->fp, cd_offset, SEEK_SET);
    n = fread(cd_data, 1, cd_size, zr->fp);
    if (n != cd_size) {
        free(cd_data);
        return -1;
    }

    zr->entries = (zip_entry_t *)calloc(total_entries, sizeof(zip_entry_t));
    if (!zr->entries) {
        free(cd_data);
        return -1;
    }

    size_t pos = 0;
    uint16_t loaded = 0;
    while (pos + 46 <= cd_size && loaded < total_entries) {
        if (read_u32(cd_data + pos) != ZIP_CENTRAL_SIG)
            break;

        consumed = parse_central_entry(cd_data + pos, &zr->entries[loaded]);
        if (consumed < 0)
            break;

        pos += (size_t)consumed;
        loaded++;
    }

    zr->entry_count = loaded;
    free(cd_data);
    return 0;
}

/* ---- ZIP 打开 / 关闭 ------------------------------------------- */

zip_reader_t *zip_open(const char *path)
{
    zip_reader_t *zr;
    uint16_t total_entries;
    uint32_t cd_offset, cd_size;
    size_t fsize;

    if (!path) return NULL;

    zr = (zip_reader_t *)calloc(1, sizeof(zip_reader_t));
    if (!zr) return NULL;

    zr->fp = fopen(path, "rb");
    if (!zr->fp) { free(zr); return NULL; }

    fseek(zr->fp, 0, SEEK_END);
    fsize = (size_t)ftell(zr->fp);
    zr->file_size = fsize;

    if (find_eocd(zr->fp, fsize, &total_entries, &cd_offset, &cd_size) != 0) {
        zip_close(zr);
        return NULL;
    }

    if (load_central_dir(zr, cd_offset, cd_size, total_entries) != 0) {
        zip_close(zr);
        return NULL;
    }

    return zr;
}

void zip_close(zip_reader_t *zr)
{
    if (!zr) return;

    if (zr->entries) {
        for (size_t i = 0; i < zr->entry_count; i++)
            free(zr->entries[i].name);
        free(zr->entries);
    }
    if (zr->fp) fclose(zr->fp);
    free(zr);
}

/* ---- ZIP 遍历 -------------------------------------------------- */

int zip_foreach(zip_reader_t *zr,
                int (*callback)(const zip_entry_t *entry, void *ctx),
                void *ctx)
{
    if (!zr || !callback)
        return WPSEXT_ERR_INVALID_ARG;

    for (size_t i = 0; i < zr->entry_count; i++) {
        int r = callback(&zr->entries[i], ctx);
        if (r != 0) return r;
    }
    return 0;
}

/* ---- 按名称查找条目 -------------------------------------------- */

static zip_entry_t *find_entry(zip_reader_t *zr, const char *name)
{
    for (size_t i = 0; i < zr->entry_count; i++) {
        if (strcmp(zr->entries[i].name, name) == 0)
            return &zr->entries[i];
    }
    return NULL;
}

/* ---- 读取并解压条目 -------------------------------------------- */

/*
 * 读取 local file header 并返回数据起始偏移
 * Local File Header:
 *   offset 0:  signature (4)
 *   offset 4:  version needed (2)
 *   offset 6:  flags (2)
 *   offset 8:  method (2)
 *   offset 26: filename length (2)
 *   offset 28: extra field length (2)
 *   ... filename ...
 *   ... extra ...
 *   [data]
 */
static size_t skip_local_header(FILE *fp)
{
    uint8_t buf[30];
    uint16_t name_len, extra_len;

    if (fread(buf, 1, 30, fp) != 30) return 0;
    if (read_u32(buf) != ZIP_LOCAL_SIG) return 0;

    name_len  = read_u16(buf + 26);
    extra_len = read_u16(buf + 28);

    fseek(fp, name_len + extra_len, SEEK_CUR);
    return 1;
}

int zip_read_entry(zip_reader_t *zr,
                   const char *entry_name,
                   uint8_t **out_data,
                   size_t *out_size)
{
    zip_entry_t *entry;
    uint8_t *comp_buf = NULL;
    uint8_t *data = NULL;
    int rc = WPSEXT_OK;

    if (!zr || !entry_name || !out_data || !out_size)
        return WPSEXT_ERR_INVALID_ARG;

    *out_data = NULL;
    *out_size = 0;

    entry = find_entry(zr, entry_name);
    if (!entry)
        return WPSEXT_ERR_FORMAT;

    /* 定位到 local file header */
    fseek(zr->fp, (long)entry->offset, SEEK_SET);
    if (!skip_local_header(zr->fp))
        return WPSEXT_ERR_FORMAT;

    if (entry->method == 0) {
        /* ---- store（无压缩） ---- */
        if (entry->compressed != entry->uncompressed)
            return WPSEXT_ERR_FORMAT;

        data = (uint8_t *)malloc(entry->uncompressed + 1);
        if (!data)
            return WPSEXT_ERR_MEMORY;

        if (fread(data, 1, entry->uncompressed, zr->fp) != entry->uncompressed) {
            free(data);
            return WPSEXT_ERR_FILE;
        }
        data[entry->uncompressed] = '\0';  /* 方便作为字符串处理 */

        *out_data = data;
        *out_size = entry->uncompressed;
        return WPSEXT_OK;

    } else if (entry->method == 8) {
        /* ---- deflate（需要 zlib） ---- */
        comp_buf = (uint8_t *)malloc(entry->compressed);
        data     = (uint8_t *)malloc(entry->uncompressed + 1);

        if (!comp_buf || !data) {
            free(comp_buf);
            free(data);
            return WPSEXT_ERR_MEMORY;
        }

        if (fread(comp_buf, 1, entry->compressed, zr->fp) != entry->compressed) {
            rc = WPSEXT_ERR_FILE;
            goto deflate_cleanup;
        }

        z_stream strm;
        memset(&strm, 0, sizeof(strm));
        strm.next_in   = comp_buf;
        strm.avail_in  = (uInt)entry->compressed;
        strm.next_out  = data;
        strm.avail_out = (uInt)entry->uncompressed;

        int ret = inflateInit2(&strm, -MAX_WBITS);  /* raw deflate */
        if (ret != Z_OK) {
            rc = WPSEXT_ERR_FORMAT;
            goto deflate_cleanup;
        }

        ret = inflate(&strm, Z_FINISH);
        inflateEnd(&strm);

        if (ret != Z_STREAM_END) {
            rc = WPSEXT_ERR_FORMAT;
            goto deflate_cleanup;
        }

        data[entry->uncompressed] = '\0';
        free(comp_buf);

        *out_data = data;
        *out_size = entry->uncompressed;
        return WPSEXT_OK;

    deflate_cleanup:
        free(comp_buf);
        free(data);
        return rc;

    } else {
        /* 不支持的压缩方法 */
        return WPSEXT_ERR_FORMAT;
    }
}
