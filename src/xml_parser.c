/*
 * xml_parser.c - 简易 SAX 风格 XML 解析器
 *
 * 支持基本 XML 解析:
 *   - 元素: <name attr="val"> 和 </name>
 *   - 自闭合: <name/>
 *   - 文本内容
 *   - 注释: <!-- ... -->
 *   - CDATA: <![CDATA[ ... ]]>
 *   - XML 声明: <?xml ... ?>
 *
 * 不支持:
 *   - DTD/实体引用展开 (仅保留 &amp; &lt; &gt; &quot; &apos;)
 *   - 命名空间解析
 */

#include "internal.h"
#include <ctype.h>

#define XML_BUF_SIZE 4096

/* --- 解析器内部状态 --------------------------------------------- */
typedef enum {
    XS_TEXT,
    XS_LT,            /* 遇到 '<' */
    XS_ELEM_NAME,     /* 正在读取元素名 */
    XS_ELEM_ATTR,     /* 正在读取属性 */
    XS_ELEM_END,      /* 遇到 '>' */
    XS_CLOSE_NAME,    /* 正在读取闭合标签名 </name */
    XS_COMMENT,       /* <!-- ... --> */
    XS_CDATA,         /* <![CDATA[ ... ]]> */
    XS_PI,            /* <? ... ?> */
    XS_ATTR_NAME,     /* 属性名 */
    XS_ATTR_EQ,       /* = */
    XS_ATTR_VAL,      /* 属性值 */
} xml_state_t;

/* --- 解析器上下文 ----------------------------------------------- */
typedef struct {
    const xml_sax_handler_t *handler;
    void                    *handler_ctx;

    xml_state_t  state;
    xml_state_t  prev_state;    /* 用于从子状态返回 */

    char         elem_name[256];
    size_t       elem_name_len;

    char         attr_name[256];
    size_t       attr_name_len;

    char         attr_val[1024];
    size_t       attr_val_len;

    char         text_buf[XML_BUF_SIZE];
    size_t       text_len;

    /* 属性列表: 临时存储，最多 32 对 */
    char        *attr_list[64];   /* [name, val, name, val, ..., NULL] */
    int          attr_count;

    char         quote_char;     /* ' 或 " */
} xml_ctx_t;

/* --- 辅助函数 --------------------------------------------------- */

static void flush_text(xml_ctx_t *xc)
{
    if (xc->text_len > 0 && xc->handler->on_characters) {
        xc->handler->on_characters(xc->handler_ctx, xc->text_buf, xc->text_len);
    }
    xc->text_len = 0;
}

static void add_text(xml_ctx_t *xc, char ch)
{
    if (xc->text_len < XML_BUF_SIZE - 1) {
        xc->text_buf[xc->text_len++] = ch;
    }
}

static void add_to_buf(char *buf, size_t *len, size_t max, char ch)
{
    if (*len < max - 1) {
        buf[(*len)++] = ch;
        buf[*len] = '\0';
    }
}

static void end_buf(char *buf, size_t *len)
{
    buf[*len] = '\0';
}

static void reset_buf(size_t *len)
{
    *len = 0;
}

static void add_attr(xml_ctx_t *xc)
{
    if (xc->attr_count >= 62) return;  /* 最多 31 个属性 */

    end_buf(xc->attr_name, &xc->attr_name_len);
    end_buf(xc->attr_val, &xc->attr_val_len);

    xc->attr_list[xc->attr_count] = strdup(xc->attr_name);
    xc->attr_list[xc->attr_count + 1] = strdup(xc->attr_val);
    xc->attr_count += 2;

    reset_buf(&xc->attr_name_len);
    reset_buf(&xc->attr_val_len);
}

static void clear_attrs(xml_ctx_t *xc)
{
    for (int i = 0; i < xc->attr_count; i++) {
        free(xc->attr_list[i]);
    }
    xc->attr_count = 0;
    xc->attr_list[0] = NULL;  /* NULL 终止 */
}

static void emit_start(xml_ctx_t *xc)
{
    end_buf(xc->elem_name, &xc->elem_name_len);
    xc->attr_list[xc->attr_count] = NULL;

    if (xc->handler->on_start_element) {
        xc->handler->on_start_element(xc->handler_ctx,
                                      xc->elem_name,
                                      (const char **)xc->attr_list);
    }

    clear_attrs(xc);
    reset_buf(&xc->elem_name_len);
}

static void emit_end(xml_ctx_t *xc)
{
    end_buf(xc->elem_name, &xc->elem_name_len);

    if (xc->handler->on_end_element) {
        xc->handler->on_end_element(xc->handler_ctx, xc->elem_name);
    }

    reset_buf(&xc->elem_name_len);
}

static int is_name_char(char ch)
{
    return isalnum((unsigned char)ch) || ch == '_' || ch == '-' || ch == ':' || ch == '.';
}

static int is_name_start(char ch)
{
    return isalpha((unsigned char)ch) || ch == '_' || ch == ':';
}

/* --- XML 实体解码 ----------------------------------------------- */
static const char *decode_entity(const char *s, char *out)
{
    if (s[0] == 'a' && s[1] == 'm' && s[2] == 'p' && s[3] == ';') {
        *out = '&'; return s + 4;
    }
    if (s[0] == 'l' && s[1] == 't' && s[2] == ';') {
        *out = '<'; return s + 3;
    }
    if (s[0] == 'g' && s[1] == 't' && s[2] == ';') {
        *out = '>'; return s + 3;
    }
    if (s[0] == 'q' && s[1] == 'u' && s[2] == 'o' && s[3] == 't' && s[4] == ';') {
        *out = '"'; return s + 5;
    }
    if (s[0] == 'a' && s[1] == 'p' && s[2] == 'o' && s[3] == 's' && s[4] == ';') {
        *out = '\''; return s + 5;
    }
    /* 未知实体，原样返回 & */
    return s;
}

/* --- 主解析循环 ------------------------------------------------- */
int xml_parse_sax(const xml_sax_handler_t *handler,
                  void *handler_ctx,
                  const uint8_t *data,
                  size_t size)
{
    xml_ctx_t xc;

    if (!handler || !data)
        return WPSEXT_ERR_INVALID_ARG;

    memset(&xc, 0, sizeof(xc));
    xc.handler     = handler;
    xc.handler_ctx = handler_ctx;
    xc.state       = XS_TEXT;

    for (size_t pos = 0; pos < size; pos++) {
        char ch = (char)data[pos];

        switch (xc.state) {

        /* ---------------------------------------------------- */
        case XS_TEXT:
            if (ch == '<') {
                flush_text(&xc);
                xc.state = XS_LT;
            } else if (ch == '&') {
                /* 实体解码 */
                char decoded;
                const char *next = decode_entity((const char *)data + pos + 1, &decoded);
                if (next != (const char *)data + pos + 1) {
                    add_text(&xc, decoded);
                    pos += (size_t)(next - ((const char *)data + pos + 1));
                } else {
                    add_text(&xc, ch);
                }
            } else {
                add_text(&xc, ch);
            }
            break;

        /* ---------------------------------------------------- */
        case XS_LT:   /* 刚遇到 '<' */
            if (ch == '/') {
                /* 闭合标签 </ */
                xc.state = XS_CLOSE_NAME;
                reset_buf(&xc.elem_name_len);
            } else if (ch == '!') {
                /* 可能是注释或 CDATA */
                xc.prev_state = XS_TEXT;
                /* 向前看判断 <!-- 还是 <![CDATA[ */
                if (pos + 3 < size && data[pos+1] == '-' && data[pos+2] == '-') {
                    xc.state = XS_COMMENT;
                    pos += 2;  /* 跳过 -- */
                } else if (pos + 8 < size && memcmp(data + pos + 1, "[CDATA[", 7) == 0) {
                    xc.state = XS_CDATA;
                    pos += 7;  /* 跳过 [CDATA[ */
                } else {
                    /* 未知，回退为文本 */
                    xc.state = XS_TEXT;
                }
            } else if (ch == '?') {
                xc.state = XS_PI;
            } else if (is_name_start(ch)) {
                /* 开始标签 */
                reset_buf(&xc.elem_name_len);
                add_to_buf(xc.elem_name, &xc.elem_name_len, sizeof(xc.elem_name), ch);
                xc.state = XS_ELEM_NAME;
            } else {
                /* 无效字符，回退为文本 */
                add_text(&xc, '<');
                add_text(&xc, ch);
                xc.state = XS_TEXT;
            }
            break;

        /* ---------------------------------------------------- */
        case XS_ELEM_NAME:
            if (isspace((unsigned char)ch)) {
                /* 元素名结束，接下来可能是属性或 > */
                xc.state = XS_ELEM_ATTR;
            } else if (ch == '>') {
                emit_start(&xc);
                xc.state = XS_TEXT;
            } else if (ch == '/') {
                /* <name/> 自闭合 */
                emit_start(&xc);
                emit_end(&xc);
                xc.state = XS_ELEM_END;
            } else if (is_name_char(ch)) {
                add_to_buf(xc.elem_name, &xc.elem_name_len, sizeof(xc.elem_name), ch);
            } else {
                /* 无效 */
                xc.state = XS_TEXT;
            }
            break;

        /* ---------------------------------------------------- */
        case XS_ELEM_ATTR:
            if (ch == '>') {
                emit_start(&xc);
                xc.state = XS_TEXT;
            } else if (ch == '/') {
                emit_start(&xc);
                emit_end(&xc);
                xc.state = XS_ELEM_END;
            } else if (is_name_start(ch)) {
                reset_buf(&xc.attr_name_len);
                add_to_buf(xc.attr_name, &xc.attr_name_len, sizeof(xc.attr_name), ch);
                xc.state = XS_ATTR_NAME;
            }
            /* 忽略空白 */
            break;

        /* ---------------------------------------------------- */
        case XS_ATTR_NAME:
            if (isspace((unsigned char)ch)) {
                xc.state = XS_ATTR_EQ;
            } else if (ch == '=') {
                xc.state = XS_ATTR_EQ;
            } else if (is_name_char(ch)) {
                add_to_buf(xc.attr_name, &xc.attr_name_len, sizeof(xc.attr_name), ch);
            } else if (ch == '>' || ch == '/') {
                /* 无值属性 */
                add_attr(&xc);
                if (ch == '>') {
                    emit_start(&xc);
                    xc.state = XS_TEXT;
                } else {
                    emit_start(&xc);
                    emit_end(&xc);
                    xc.state = XS_ELEM_END;
                }
            }
            break;

        /* ---------------------------------------------------- */
        case XS_ATTR_EQ:
            if (ch == '"' || ch == '\'') {
                xc.quote_char = ch;
                reset_buf(&xc.attr_val_len);
                xc.state = XS_ATTR_VAL;
            } else if (ch == '=') {
                /* 跳过 */
            }
            /* 忽略空白 */
            break;

        /* ---------------------------------------------------- */
        case XS_ATTR_VAL:
            if (ch == xc.quote_char) {
                add_attr(&xc);
                xc.state = XS_ELEM_ATTR;
            } else if (ch == '&') {
                char decoded;
                const char *next = decode_entity((const char *)data + pos + 1, &decoded);
                if (next != (const char *)data + pos + 1) {
                    add_to_buf(xc.attr_val, &xc.attr_val_len, sizeof(xc.attr_val), decoded);
                    pos += (size_t)(next - ((const char *)data + pos + 1));
                } else {
                    add_to_buf(xc.attr_val, &xc.attr_val_len, sizeof(xc.attr_val), ch);
                }
            } else {
                add_to_buf(xc.attr_val, &xc.attr_val_len, sizeof(xc.attr_val), ch);
            }
            break;

        /* ---------------------------------------------------- */
        case XS_ELEM_END:
            if (ch == '>') {
                xc.state = XS_TEXT;
            }
            break;

        /* ---------------------------------------------------- */
        case XS_CLOSE_NAME:
            if (ch == '>') {
                emit_end(&xc);
                xc.state = XS_TEXT;
            } else if (!isspace((unsigned char)ch)) {
                add_to_buf(xc.elem_name, &xc.elem_name_len, sizeof(xc.elem_name), ch);
            }
            break;

        /* ---------------------------------------------------- */
        case XS_COMMENT:
            if (ch == '-' && pos + 2 < size && data[pos+1] == '-' && data[pos+2] == '>') {
                xc.state = XS_TEXT;
                pos += 2;
            }
            break;

        /* ---------------------------------------------------- */
        case XS_CDATA:
            if (ch == ']' && pos + 2 < size && data[pos+1] == ']' && data[pos+2] == '>') {
                /* 将 CDATA 内容作为文本发出 */
                flush_text(&xc);
                xc.state = XS_TEXT;
                pos += 2;
            } else {
                add_text(&xc, ch);
            }
            break;

        /* ---------------------------------------------------- */
        case XS_PI:
            if (ch == '?' && pos + 1 < size && data[pos+1] == '>') {
                xc.state = XS_TEXT;
                pos += 1;
            }
            break;
        }
    }

    /* 刷新剩余文本 */
    flush_text(&xc);
    clear_attrs(&xc);

    return WPSEXT_OK;
}
