/*
 * bdf.h - freestanding stb-styled BDF parser, no stdlib.
 * Requires <stddef.h>, <stdint.h>, <stdbool.h> (freestanding POSIX).
 * Zero allocation - caller supplies all storage.
 * bitmap_start points into the source buffer; keep it alive.
 *
 * Under public domain, feel free to user it anywhere when you need this cursed font (you should prob use PSF)
 */
#ifndef UTIL_BDF_H
#define UTIL_BDF_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef BDF_MAX_UNKNOWN_PROPS
#define BDF_MAX_UNKNOWN_PROPS 64
#endif
#ifndef BDF_MAX_UNKNOWN_GLYPH_FIELDS
#define BDF_MAX_UNKNOWN_GLYPH_FIELDS 16
#endif
#ifndef BDF_PROP_VALUE_MAX
#define BDF_PROP_VALUE_MAX 256
#endif
#ifndef BDF_PROP_KEY_MAX
#define BDF_PROP_KEY_MAX 64
#endif

typedef struct
{
    int32_t w, h, x, y;
} BDF_BBox;

typedef struct
{
    char key[BDF_PROP_KEY_MAX];
    char value[BDF_PROP_VALUE_MAX];
} BDF_UnknownProp;
typedef struct
{
    char key[BDF_PROP_KEY_MAX];
    char value[BDF_PROP_VALUE_MAX];
} BDF_UnknownGlyphField;

typedef struct
{
    char foundry[64], family_name[128], weight_name[64], slant[16];
    char setwidth_name[64], add_style_name[64];
    int32_t pixel_size, point_size, resolution_x, resolution_y;
    char spacing[8];
    int32_t average_width;
    char charset_registry[64], charset_encoding[64];
    int32_t font_ascent, font_descent, default_char, min_space;
    char font_version[32], face_name[128], full_name[128];
    char copyright[256], notice[256], destination[32];
    int32_t cap_height, x_height;
    int32_t underline_position, underline_thickness;
    int32_t strikeout_ascent, strikeout_descent;
    int32_t italic_angle, weight, norm_space, quad_width;
    int32_t superscript_x, superscript_y, subscript_x, subscript_y;
    int32_t superscript_size, subscript_size, small_caps_size;
    int32_t raw_ascent, raw_descent;
    BDF_UnknownProp unknowns[BDF_MAX_UNKNOWN_PROPS];
    uint32_t n_unknowns;
} BDF_Properties;

typedef struct
{
    char name[64];
    int32_t encoding, encoding2;
    int32_t swidth_x, swidth_y, dwidth_x, dwidth_y;
    int32_t swidth2_x, swidth2_y, dwidth2_x, dwidth2_y;
    int32_t vvector_x, vvector_y;
    bool has_vvector;
    BDF_BBox bbox;
    const char *bitmap_start;
    uint32_t bitmap_rows, bitmap_stride;
    BDF_UnknownGlyphField unknowns[BDF_MAX_UNKNOWN_GLYPH_FIELDS];
    uint32_t n_unknowns;
} BDF_Glyph;

typedef struct
{
    char version[16], font_name[256];
    int32_t point_size, x_res, y_res;
    BDF_BBox bbox;
    int32_t metrics_set;
    int32_t swidth2_x, swidth2_y, dwidth2_x, dwidth2_y;
    int32_t vvector_x, vvector_y;
    bool has_global_vvector;
    BDF_Properties props;
    bool has_props;
    uint32_t chars_declared, chars_parsed;
    BDF_Glyph *glyphs;
    uint32_t glyphs_capacity;
} BDF_Font;

typedef enum
{
    BDF_OK = 0,
    BDF_ERR_NULL = -1,
    BDF_ERR_SYNTAX = -2,
    BDF_ERR_OVERFLOW = -3,
    BDF_ERR_BITMAP = -4,
    BDF_ERR_TRUNCATED = -5,
} BDF_Result;

BDF_Result bdf_parse(const char *src, size_t src_len, BDF_Font *font);
const BDF_Glyph *bdf_glyph_for_codepoint(const BDF_Font *font, int32_t cp);
int32_t bdf_bitmap_row(const BDF_Glyph *g, uint32_t row, uint8_t *out, size_t cap);
const char *bdf_result_str(BDF_Result r);

#ifdef BDF_IMPLEMENTATION

static size_t bdf__strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}
static const char *bdf__skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t')
        p++;
    return p;
}
static void bdf__memset(void *d, int c, size_t n)
{
    unsigned char *p = d;
    while (n--)
        *p++ = (unsigned char)c;
}

static int bdf__strncmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        unsigned char ca = (unsigned char)a[i], cb = (unsigned char)b[i];
        if (ca != cb)
            return (int)ca - (int)cb;
        if (!ca)
            return 0;
    }
    return 0;
}

static bool bdf__kw(const char *l, const char *k)
{
    size_t n = bdf__strlen(k);
    if (bdf__strncmp(l, k, n))
        return false;
    char c = l[n];
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\0';
}

static int32_t bdf__int(const char **pp)
{
    const char *p = bdf__skip_ws(*pp);
    int32_t s = 1, v = 0;
    if (*p == '-')
    {
        s = -1;
        p++;
    }
    else if (*p == '+')
        p++;
    while (*p >= '0' && *p <= '9')
        v = v * 10 + (*p++ - '0');
    *pp = p;
    return s * v;
}

static void bdf__rest(const char *p, char *dst, size_t cap)
{
    p = bdf__skip_ws(p);
    size_t len = bdf__strlen(p);
    while (len && (p[len - 1] == '\r' || p[len - 1] == '\n' || p[len - 1] == ' '))
        len--;
    if (len >= cap)
        len = cap - 1;
    for (size_t i = 0; i < len; i++)
        dst[i] = p[i];
    dst[len] = '\0';
    size_t dlen = bdf__strlen(dst);
    if (dlen >= 2 && dst[0] == '"' && dst[dlen - 1] == '"')
    {
        for (size_t i = 0; i < dlen - 2; i++)
            dst[i] = dst[i + 1];
        dst[dlen - 2] = '\0';
    }
}

static void bdf__token(const char *p, char *dst, size_t cap)
{
    p = bdf__skip_ws(p);
    size_t i = 0;
    while (i < cap - 1 && *p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
        dst[i++] = *p++;
    dst[i] = '\0';
}

static uint8_t bdf__xdigit(char c)
{
    if (c >= '0' && c <= '9')
        return (uint8_t)(c - '0');
    if (c >= 'a' && c <= 'f')
        return (uint8_t)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F')
        return (uint8_t)(c - 'A' + 10);
    return 0xff;
}

typedef struct
{
    const char *src;
    size_t len, pos;
} BDF__Buf;
#define BDF__LINE_CAP 1280

static bool bdf__getline(BDF__Buf *b, char *line, const char **rs)
{
    if (b->pos >= b->len)
        return false;
    if (rs)
        *rs = b->src + b->pos;
    size_t i = 0;
    while (b->pos < b->len)
    {
        char c = b->src[b->pos++];
        if (c == '\n')
            break;
        if (i < BDF__LINE_CAP - 1)
            line[i++] = c;
    }
    if (i && line[i - 1] == '\r')
        i--;
    line[i] = '\0';
    return true;
}

static void bdf__parse_props(BDF__Buf *b, BDF_Properties *pr)
{
    char line[BDF__LINE_CAP];
    pr->pixel_size = pr->point_size = pr->resolution_x = pr->resolution_y = INT32_MIN;
    pr->average_width = pr->font_ascent = pr->font_descent = pr->default_char = INT32_MIN;
    pr->min_space = pr->cap_height = pr->x_height = pr->underline_position = INT32_MIN;
    pr->underline_thickness = pr->strikeout_ascent = pr->strikeout_descent = INT32_MIN;
    pr->italic_angle = pr->weight = pr->norm_space = pr->quad_width = INT32_MIN;
    pr->superscript_x = pr->superscript_y = pr->subscript_x = pr->subscript_y = INT32_MIN;
    pr->superscript_size = pr->subscript_size = pr->small_caps_size = INT32_MIN;
    pr->raw_ascent = pr->raw_descent = INT32_MIN;

#define KW(k) bdf__kw(p, k)
#define P(k) (p + bdf__strlen(k))
#define SI(f, k)              \
    else if (KW(k))           \
    {                         \
        const char *v = P(k); \
        pr->f = bdf__int(&v); \
    }
#define SS(f, k) else if (KW(k)) bdf__rest(P(k), pr->f, sizeof pr->f);

    while (bdf__getline(b, line, NULL))
    {
        const char *p = line;
        if (!*p || *p == '#')
            continue;
        if (KW("ENDPROPERTIES"))
            return;
        if (0)
            ;
        SS(foundry, "FOUNDRY")
        SS(family_name, "FAMILY_NAME")
        SS(weight_name, "WEIGHT_NAME")
        SS(slant, "SLANT")
        SS(setwidth_name, "SETWIDTH_NAME")
        SS(add_style_name, "ADD_STYLE_NAME")
        SI(pixel_size, "PIXEL_SIZE")
        SI(point_size, "POINT_SIZE")
        SI(resolution_x, "RESOLUTION_X")
        SI(resolution_y, "RESOLUTION_Y")
        SS(spacing, "SPACING")
        SI(average_width, "AVERAGE_WIDTH")
        SS(charset_registry, "CHARSET_REGISTRY")
        SS(charset_encoding, "CHARSET_ENCODING")
        SI(font_ascent, "FONT_ASCENT")
        SI(font_descent, "FONT_DESCENT")
        SI(default_char, "DEFAULT_CHAR")
        SI(min_space, "MIN_SPACE")
        SI(cap_height, "CAP_HEIGHT")
        SI(x_height, "X_HEIGHT")
        SI(underline_position, "UNDERLINE_POSITION")
        SI(underline_thickness, "UNDERLINE_THICKNESS")
        SI(strikeout_ascent, "STRIKEOUT_ASCENT")
        SI(strikeout_descent, "STRIKEOUT_DESCENT")
        SI(italic_angle, "ITALIC_ANGLE")
        SI(weight, "WEIGHT")
        SI(norm_space, "NORM_SPACE")
        SI(quad_width, "QUAD_WIDTH")
        SI(superscript_x, "SUPERSCRIPT_X")
        SI(superscript_y, "SUPERSCRIPT_Y")
        SI(subscript_x, "SUBSCRIPT_X")
        SI(subscript_y, "SUBSCRIPT_Y")
        SI(superscript_size, "SUPERSCRIPT_SIZE")
        SI(subscript_size, "SUBSCRIPT_SIZE")
        SI(small_caps_size, "SMALL_CAPS_SIZE")
        SI(raw_ascent, "RAW_ASCENT")
        SI(raw_descent, "RAW_DESCENT")
        SS(font_version, "FONT_VERSION")
        SS(face_name, "FACE_NAME")
        SS(full_name, "FULL_NAME")
        SS(copyright, "COPYRIGHT")
        SS(notice, "NOTICE")
        SS(destination, "DESTINATION")
        else if (pr->n_unknowns < BDF_MAX_UNKNOWN_PROPS)
        {
            BDF_UnknownProp *u = &pr->unknowns[pr->n_unknowns++];
            bdf__token(p, u->key, sizeof u->key);
            bdf__rest(p + bdf__strlen(u->key), u->value, sizeof u->value);
        }
    }
#undef SS
#undef SI
#undef P
#undef KW
}

typedef enum
{
    PS_HEADER,
    PS_CHARS,
    PS_GLYPH,
    PS_BITMAP,
    PS_DONE
} BDF__PState;

BDF_Result bdf_parse(const char *src, size_t src_len, BDF_Font *font)
{
    if (!src || !font)
        return BDF_ERR_NULL;
    if (font->glyphs_capacity && !font->glyphs)
        return BDF_ERR_NULL;

    BDF_Glyph *gp = font->glyphs;
    uint32_t gc = font->glyphs_capacity;
    bdf__memset(font, 0, sizeof *font);
    font->glyphs = gp;
    font->glyphs_capacity = gc;

    BDF__Buf buf = {src, src_len, 0};
    BDF__PState state = PS_HEADER;
    BDF_Result ret = BDF_OK;
    char line[BDF__LINE_CAP];
    BDF_Glyph cur;
    bdf__memset(&cur, 0, sizeof cur);

#define V(k) (p + bdf__strlen(k))
#define I2(a, b, k)       \
    const char *v = V(k); \
    a = bdf__int(&v);     \
    b = bdf__int(&v);

    while (bdf__getline(&buf, line, NULL))
    {
        const char *p = line;
        if (!*p || *p == '#')
            continue;
        switch (state)
        {
        case PS_HEADER:
            if (bdf__kw(p, "STARTFONT"))
                bdf__token(V("STARTFONT"), font->version, sizeof font->version);
            else if (bdf__kw(p, "FONT"))
                bdf__rest(V("FONT"), font->font_name, sizeof font->font_name);
            else if (bdf__kw(p, "SIZE"))
            {
                const char *v = V("SIZE");
                font->point_size = bdf__int(&v);
                font->x_res = bdf__int(&v);
                font->y_res = bdf__int(&v);
            }
            else if (bdf__kw(p, "FONTBOUNDINGBOX"))
            {
                const char *v = V("FONTBOUNDINGBOX");
                font->bbox.w = bdf__int(&v);
                font->bbox.h = bdf__int(&v);
                font->bbox.x = bdf__int(&v);
                font->bbox.y = bdf__int(&v);
            }
            else if (bdf__kw(p, "METRICSSET"))
            {
                const char *v = V("METRICSSET");
                font->metrics_set = bdf__int(&v);
            }
            else if (bdf__kw(p, "SWIDTH2"))
            {
                I2(font->swidth2_x, font->swidth2_y, "SWIDTH2")
            }
            else if (bdf__kw(p, "DWIDTH2"))
            {
                I2(font->dwidth2_x, font->dwidth2_y, "DWIDTH2")
            }
            else if (bdf__kw(p, "VVECTOR"))
            {
                I2(font->vvector_x, font->vvector_y, "VVECTOR")
                font->has_global_vvector = true;
            }
            else if (bdf__kw(p, "STARTPROPERTIES"))
            {
                bdf__memset(&font->props, 0, sizeof font->props);
                bdf__parse_props(&buf, &font->props);
                font->has_props = true;
            }
            else if (bdf__kw(p, "CHARS"))
            {
                const char *v = V("CHARS");
                font->chars_declared = (uint32_t)bdf__int(&v);
                state = PS_CHARS;
            }
            else if (bdf__kw(p, "ENDFONT"))
                state = PS_DONE;
            break;
        case PS_CHARS:
            if (bdf__kw(p, "STARTCHAR"))
            {
                bdf__memset(&cur, 0, sizeof cur);
                cur.encoding = cur.encoding2 = -1;
                cur.bbox = font->bbox;
                bdf__rest(V("STARTCHAR"), cur.name, sizeof cur.name);
                state = PS_GLYPH;
            }
            else if (bdf__kw(p, "ENDFONT"))
                state = PS_DONE;
            break;
        case PS_GLYPH:
            if (bdf__kw(p, "ENCODING"))
            {
                const char *v = V("ENCODING");
                cur.encoding = bdf__int(&v);
                v = bdf__skip_ws(v);
                if (*v >= '0' && *v <= '9')
                    cur.encoding2 = bdf__int(&v);
            }
            else if (bdf__kw(p, "SWIDTH2"))
            {
                I2(cur.swidth2_x, cur.swidth2_y, "SWIDTH2")
            }
            else if (bdf__kw(p, "SWIDTH"))
            {
                I2(cur.swidth_x, cur.swidth_y, "SWIDTH")
            }
            else if (bdf__kw(p, "DWIDTH2"))
            {
                I2(cur.dwidth2_x, cur.dwidth2_y, "DWIDTH2")
            }
            else if (bdf__kw(p, "DWIDTH"))
            {
                I2(cur.dwidth_x, cur.dwidth_y, "DWIDTH")
            }
            else if (bdf__kw(p, "VVECTOR"))
            {
                I2(cur.vvector_x, cur.vvector_y, "VVECTOR")
                cur.has_vvector = true;
            }
            else if (bdf__kw(p, "BBX"))
            {
                const char *v = V("BBX");
                cur.bbox.w = bdf__int(&v);
                cur.bbox.h = bdf__int(&v);
                cur.bbox.x = bdf__int(&v);
                cur.bbox.y = bdf__int(&v);
            }
            else if (bdf__kw(p, "BITMAP"))
            {
                cur.bitmap_stride = ((uint32_t)cur.bbox.w + 7u) / 8u * 2u;
                cur.bitmap_rows = (uint32_t)cur.bbox.h;
                cur.bitmap_start = src + buf.pos;
                state = PS_BITMAP;
            }
            else if (bdf__kw(p, "ENDCHAR"))
            {
                if (font->chars_parsed < font->glyphs_capacity)
                    font->glyphs[font->chars_parsed++] = cur;
                else
                    ret = BDF_ERR_OVERFLOW;
                state = PS_CHARS;
            }
            else if (cur.n_unknowns < BDF_MAX_UNKNOWN_GLYPH_FIELDS)
            {
                BDF_UnknownGlyphField *u = &cur.unknowns[cur.n_unknowns++];
                bdf__token(p, u->key, sizeof u->key);
                bdf__rest(p + bdf__strlen(u->key), u->value, sizeof u->value);
            }
            break;
        case PS_BITMAP:
            if (bdf__kw(p, "ENDCHAR"))
            {
                if (font->chars_parsed < font->glyphs_capacity)
                    font->glyphs[font->chars_parsed++] = cur;
                else
                    ret = BDF_ERR_OVERFLOW;
                state = PS_CHARS;
            }
            break;
        case PS_DONE:
            break;
        }
    }
#undef I2
#undef V
    if (state != PS_DONE && ret == BDF_OK)
        ret = BDF_ERR_TRUNCATED;
    return ret;
}

const BDF_Glyph *bdf_glyph_for_codepoint(const BDF_Font *font, int32_t cp)
{
    if (!font || !font->glyphs)
        return NULL;
    for (uint32_t i = 0; i < font->chars_parsed; i++)
        if (font->glyphs[i].encoding == cp)
            return &font->glyphs[i];
    return NULL;
}

int32_t bdf_bitmap_row(const BDF_Glyph *g, uint32_t row, uint8_t *out, size_t cap)
{
    if (!g || !out || !g->bitmap_start || row >= g->bitmap_rows)
        return -1;
    uint32_t nb = g->bitmap_stride / 2u;
    if (cap < nb)
        return -1;
    const char *p = g->bitmap_start;
    for (uint32_t r = 0; r < row; r++)
    {
        while (*p && *p != '\n')
            p++;
        if (*p == '\n')
            p++;
    }
    for (uint32_t b = 0; b < nb; b++)
    {
        uint8_t hi = bdf__xdigit(p[b * 2]), lo = bdf__xdigit(p[b * 2 + 1]);
        if (hi == 0xff || lo == 0xff)
            return -1;
        out[b] = (uint8_t)((hi << 4) | lo);
    }
    return (int32_t)nb;
}

const char *bdf_result_str(BDF_Result r)
{
    switch (r)
    {
    case BDF_OK:
        return "success";
    case BDF_ERR_NULL:
        return "null pointer argument";
    case BDF_ERR_SYNTAX:
        return "unexpected keyword";
    case BDF_ERR_OVERFLOW:
        return "glyph array too small";
    case BDF_ERR_BITMAP:
        return "malformed bitmap data";
    case BDF_ERR_TRUNCATED:
        return "file ends before ENDFONT";
    default:
        return "unknown error";
    }
}

#endif /* BDF_IMPLEMENTATION */
#endif /* UTIL_BDF_H */