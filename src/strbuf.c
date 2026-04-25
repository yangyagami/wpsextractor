/*
 * strbuf.c - 简易动态字符串构建器
 */

#include "internal.h"

#define STRBUF_INIT_CAP 256

void strbuf_init(strbuf_t *sb)
{
    sb->data = (char *)malloc(STRBUF_INIT_CAP);
    if (sb->data) {
        sb->data[0] = '\0';
        sb->len = 0;
        sb->cap = STRBUF_INIT_CAP;
    } else {
        sb->len = 0;
        sb->cap = 0;
    }
}

void strbuf_free(strbuf_t *sb)
{
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static int strbuf_reserve(strbuf_t *sb, size_t need)
{
    if (sb->len + need + 1 <= sb->cap)
        return 0;

    size_t new_cap = sb->cap ? sb->cap : STRBUF_INIT_CAP;
    while (new_cap < sb->len + need + 1) {
        if (new_cap > (size_t)-1 / 2)
            return -1;  /* 溢出 */
        new_cap *= 2;
    }

    char *new_data = (char *)realloc(sb->data, new_cap);
    if (!new_data)
        return -1;

    sb->data = new_data;
    sb->cap = new_cap;
    return 0;
}

int strbuf_append(strbuf_t *sb, const char *data, size_t len)
{
    if (!data || len == 0)
        return 0;
    if (strbuf_reserve(sb, len) != 0)
        return -1;

    memcpy(sb->data + sb->len, data, len);
    sb->len += len;
    sb->data[sb->len] = '\0';
    return 0;
}

int strbuf_append_str(strbuf_t *sb, const char *str)
{
    return strbuf_append(sb, str, strlen(str));
}

int strbuf_append_char(strbuf_t *sb, char ch)
{
    return strbuf_append(sb, &ch, 1);
}

char *strbuf_detach(strbuf_t *sb, size_t *out_len)
{
    char *ret = sb->data;

    /* 确保有容量存 '\0' */
    if (!ret || sb->cap == 0) {
        ret = (char *)malloc(1);
        if (ret) ret[0] = '\0';
    } else {
        ret[sb->len] = '\0';
    }

    if (out_len) *out_len = sb->len;

    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;

    return ret;
}
