/*
   nanoem's unicode string factory for builds without Windows (Linux): the
   ext/mbwc.c factory (MIT, Copyright (c) 2015-2023 hkrn) with portable
   conversions in place of MultiByteToWideChar/WideCharToMultiByte. Strings are
   stored as UTF-16, as wchar_t is on Windows, so hashes and comparisons match
   the Windows build exactly. CP932 (Shift-JIS, PMD/VMD names) uses Microsoft's
   mapping (cp932_table.h), including its single-byte private-use code points.
 */

#include "ext/mbwc.h"

#include "nanoem_p.h" /* for nanoem_calloc/nanoem_free */
#include "cp932_table.h"

#include <stdlib.h>
#include <string.h>

typedef uint16_t mmdhl_char16_t;

struct nanoem_unicode_string_mbwc_t {
    mmdhl_char16_t *data;
    int length;
    struct nanoem_unicode_string_mbwc_cache_t {
        nanoem_u8_t *data;
        nanoem_rsize_t length;
    } cache;
};

/* MurmurHash, by Austin Appleby (as in ext/mbwc.c). */
static unsigned int
MurmurHash(const void *key, int len, unsigned int seed)
{
    const unsigned int m = 0xc6a4a793;
    const int r = 16;
    unsigned int h = seed ^ (len * m);
    const unsigned char *data = (const unsigned char *) key;
    while (len >= 4) {
        unsigned int k;
        memcpy(&k, data, 4);
        h += k;
        h *= m;
        h ^= h >> r;
        data += 4;
        len -= 4;
    }
    switch (len) {
    case 3:
        h += data[2] << 16;
        /* fall through */
    case 2:
        h += data[1] << 8;
        /* fall through */
    case 1:
        h += data[0];
        h *= m;
        h ^= h >> r;
        /* fall through */
    default:
        break;
    }
    h *= m;
    h ^= h >> 10;
    h *= m;
    h ^= h >> 17;
    return h;
}

enum { CODEPAGE_SJIS, CODEPAGE_UTF8 };

/* UTF-8 to UTF-16 as MultiByteToWideChar(CP_UTF8, 0) does: invalid sequences become U+FFFD. */
static int
decodeUtf8(const nanoem_u8_t *s, nanoem_rsize_t n, mmdhl_char16_t *out)
{
    static const unsigned long minimum[] = { 0, 0x80, 0x800, 0x10000 };
    int written = 0;
    nanoem_rsize_t at = 0;
    while (at < n) {
        unsigned c = s[at], value;
        int extra = c < 0x80 ? 0 : (c >= 0xc2 && c < 0xe0) ? 1 : (c >= 0xe0 && c < 0xf0) ? 2 : (c >= 0xf0 && c < 0xf5) ? 3 : -1, k;
        if (extra < 0) {
            value = 0xfffd;
            at++;
        }
        else if (extra == 0) {
            value = c;
            at++;
        }
        else {
            value = c & (0x3fu >> extra);
            for (k = 1; k <= extra; k++) {
                if (at + k >= n || (s[at + k] & 0xc0) != 0x80) {
                    break;
                }
                value = value << 6 | (s[at + k] & 0x3f);
            }
            if (k <= extra || value < minimum[extra] || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
                /* One replacement per maximal invalid subpart. */
                value = 0xfffd;
                at += k > 1 ? (nanoem_rsize_t) k : 1;
            }
            else {
                at += (nanoem_rsize_t) extra + 1;
            }
        }
        if (value >= 0x10000) {
            value -= 0x10000;
            if (out) {
                out[written] = (mmdhl_char16_t) (0xd800 + (value >> 10));
                out[written + 1] = (mmdhl_char16_t) (0xdc00 + (value & 0x3ff));
            }
            written += 2;
        }
        else {
            if (out) {
                out[written] = (mmdhl_char16_t) value;
            }
            written++;
        }
    }
    return written;
}

/* CP932 to UTF-16 as Windows does: Microsoft's table, the private-use single
   bytes, and U+30FB for an unassigned or incomplete double-byte character. */
static int
decodeCp932(const nanoem_u8_t *s, nanoem_rsize_t n, mmdhl_char16_t *out)
{
    int written = 0;
    nanoem_rsize_t at = 0;
    while (at < n) {
        unsigned c = s[at], value;
        if (c < 0x80) {
            value = c;
            at++;
        }
        else if (c == 0x80) {
            value = 0x80;
            at++;
        }
        else if (c == 0xa0) {
            value = 0xf8f0;
            at++;
        }
        else if (c >= 0xa1 && c <= 0xdf) {
            value = 0xff61 + (c - 0xa1);
            at++;
        }
        else if (c >= 0xfd) {
            value = 0xf8f1 + (c - 0xfd);
            at++;
        }
        else {
            int row = c < 0xa0 ? (int) c - 0x81 : (int) c - 0xe0 + 31;
            if (at + 1 < n && s[at + 1] >= 0x40 && s[at + 1] <= 0xfc) {
                value = mmdhl_cp932_decode[row][s[at + 1] - 0x40];
                if (!value) {
                    value = 0x30fb;
                }
                at += 2;
            }
            else {
                value = 0x30fb;
                at++;
            }
        }
        if (out) {
            out[written] = (mmdhl_char16_t) value;
        }
        written++;
    }
    return written;
}

static int
encodeUtf8(const mmdhl_char16_t *s, int n, nanoem_u8_t *out)
{
    int written = 0, i;
    for (i = 0; i < n; i++) {
        unsigned long c = s[i];
        if (c >= 0xd800 && c <= 0xdbff && i + 1 < n && s[i + 1] >= 0xdc00 && s[i + 1] <= 0xdfff) {
            c = 0x10000 + ((c - 0xd800) << 10) + (s[i + 1] - 0xdc00);
            i++;
        }
        else if (c >= 0xd800 && c <= 0xdfff) {
            c = 0xfffd;
        }
        if (c < 0x80) {
            if (out) out[written] = (nanoem_u8_t) c;
            written += 1;
        }
        else if (c < 0x800) {
            if (out) {
                out[written] = (nanoem_u8_t) (0xc0 | c >> 6);
                out[written + 1] = (nanoem_u8_t) (0x80 | (c & 0x3f));
            }
            written += 2;
        }
        else if (c < 0x10000) {
            if (out) {
                out[written] = (nanoem_u8_t) (0xe0 | c >> 12);
                out[written + 1] = (nanoem_u8_t) (0x80 | (c >> 6 & 0x3f));
                out[written + 2] = (nanoem_u8_t) (0x80 | (c & 0x3f));
            }
            written += 3;
        }
        else {
            if (out) {
                out[written] = (nanoem_u8_t) (0xf0 | c >> 18);
                out[written + 1] = (nanoem_u8_t) (0x80 | (c >> 12 & 0x3f));
                out[written + 2] = (nanoem_u8_t) (0x80 | (c >> 6 & 0x3f));
                out[written + 3] = (nanoem_u8_t) (0x80 | (c & 0x3f));
            }
            written += 4;
        }
    }
    return written;
}

static int
encodeCp932(const mmdhl_char16_t *s, int n, nanoem_u8_t *out)
{
    int written = 0, i;
    const int count = (int) (sizeof(mmdhl_cp932_encode) / sizeof(mmdhl_cp932_encode[0]));
    for (i = 0; i < n; i++) {
        unsigned c = s[i], value = '?';
        if (c < 0x80) {
            value = c;
        }
        else if (c == 0xf8f0) {
            value = 0xa0;
        }
        else if (c >= 0xf8f1 && c <= 0xf8f3) {
            value = 0xfd + (c - 0xf8f1);
        }
        else {
            int lo = 0, hi = count - 1;
            while (lo <= hi) {
                int mid = (lo + hi) / 2;
                if (mmdhl_cp932_encode[mid][0] < c) {
                    lo = mid + 1;
                }
                else if (mmdhl_cp932_encode[mid][0] > c) {
                    hi = mid - 1;
                }
                else {
                    value = mmdhl_cp932_encode[mid][1];
                    break;
                }
            }
            if (c >= 0xd800 && c <= 0xdbff && i + 1 < n && s[i + 1] >= 0xdc00 && s[i + 1] <= 0xdfff) {
                i++; /* one '?' for a character outside the BMP */
            }
        }
        if (value > 0xff) {
            if (out) {
                out[written] = (nanoem_u8_t) (value >> 8);
                out[written + 1] = (nanoem_u8_t) (value & 0xff);
            }
            written += 2;
        }
        else {
            if (out) out[written] = (nanoem_u8_t) value;
            written++;
        }
    }
    return written;
}

static nanoem_unicode_string_mbwc_t *
nanoemUnicodeStringFactoryFromStringMBWC(const nanoem_u8_t *string, nanoem_rsize_t length, int codepage, nanoem_status_t *status)
{
    nanoem_unicode_string_mbwc_t *s;
    int capacity = codepage == CODEPAGE_UTF8 ? decodeUtf8(string, length, NULL) : decodeCp932(string, length, NULL);
    s = (nanoem_unicode_string_mbwc_t *) nanoem_calloc(1, sizeof(*s), status);
    if (nanoem_is_not_null(s)) {
        s->data = (mmdhl_char16_t *) nanoem_calloc(capacity + 1, sizeof(*s->data), status);
        if (nanoem_is_not_null(s->data)) {
            s->length = codepage == CODEPAGE_UTF8 ? decodeUtf8(string, length, s->data) : decodeCp932(string, length, s->data);
            nanoem_status_ptr_assign_succeeded(status);
        }
    }
    return s;
}

static nanoem_u8_t *
nanoemUnicodeStringFactoryToStringMBWC(const nanoem_unicode_string_t *string, int codepage, nanoem_rsize_t *length, nanoem_status_t *status)
{
    const nanoem_unicode_string_mbwc_t *s = (const nanoem_unicode_string_mbwc_t *) string;
    int capacity = 0;
    nanoem_u8_t *buffer = NULL;
    if (s) {
        capacity = codepage == CODEPAGE_UTF8 ? encodeUtf8(s->data, s->length, NULL) : encodeCp932(s->data, s->length, NULL);
        buffer = (nanoem_u8_t *) nanoem_calloc(capacity + 1, sizeof(*buffer), status);
        if (nanoem_is_not_null(buffer)) {
            *length = (nanoem_rsize_t) (codepage == CODEPAGE_UTF8 ? encodeUtf8(s->data, s->length, buffer) : encodeCp932(s->data, s->length, buffer));
            nanoem_status_ptr_assign_succeeded(status);
        }
        else {
            *length = 0;
        }
    }
    else {
        *length = 0;
        nanoem_status_ptr_assign_null_object(status);
    }
    return buffer;
}

static nanoem_unicode_string_t *
nanoemUnicodeStringFactoryFromCp932CallbackMBWC(void *opaque, const nanoem_u8_t *string, nanoem_rsize_t length, nanoem_status_t *status)
{
    nanoem_mark_unused(opaque);
    return (nanoem_unicode_string_t *) nanoemUnicodeStringFactoryFromStringMBWC(string, length, CODEPAGE_SJIS, status);
}

static nanoem_unicode_string_t *
nanoemUnicodeStringFactoryFromUtf8CallbackMBWC(void *opaque, const nanoem_u8_t *string, nanoem_rsize_t length, nanoem_status_t *status)
{
    nanoem_mark_unused(opaque);
    return (nanoem_unicode_string_t *) nanoemUnicodeStringFactoryFromStringMBWC(string, length, CODEPAGE_UTF8, status);
}

static nanoem_unicode_string_t *
nanoemUnicodeStringFactoryFromUtf16CallbackMBWC(void *opaque, const nanoem_u8_t *string, nanoem_rsize_t length, nanoem_status_t *status)
{
    nanoem_unicode_string_mbwc_t *s;
    nanoem_mark_unused(opaque);
    s = (nanoem_unicode_string_mbwc_t *) nanoem_calloc(1, sizeof(*s), status);
    if (nanoem_is_not_null(s)) {
        /* One more unit than ext/mbwc.c allocates: the comparison below reads up to a terminator. */
        s->data = (mmdhl_char16_t *) nanoem_calloc(length / sizeof(*s->data) + 1, sizeof(*s->data), status);
        if (nanoem_is_not_null(s->data)) {
            s->length = (int) (length / sizeof(*s->data));
            memcpy(s->data, string, (size_t) s->length * sizeof(*s->data));
            nanoem_status_ptr_assign_succeeded(status);
        }
    }
    return (nanoem_unicode_string_t *) s;
}

static nanoem_u8_t *
nanoemUnicodeStringFactoryToCp932CallbackMBWC(void *opaque, const nanoem_unicode_string_t *string, nanoem_rsize_t *length, nanoem_status_t *status)
{
    nanoem_mark_unused(opaque);
    return nanoemUnicodeStringFactoryToStringMBWC(string, CODEPAGE_SJIS, length, status);
}

static nanoem_u8_t *
nanoemUnicodeStringFactoryToUtf8CallbackMBWC(void *opaque, const nanoem_unicode_string_t *string, nanoem_rsize_t *length, nanoem_status_t *status)
{
    nanoem_mark_unused(opaque);
    return nanoemUnicodeStringFactoryToStringMBWC(string, CODEPAGE_UTF8, length, status);
}

static nanoem_u8_t *
nanoemUnicodeStringFactoryToUtf16CallbackMBWC(void *opaque, const nanoem_unicode_string_t *string, nanoem_rsize_t *length, nanoem_status_t *status)
{
    const nanoem_unicode_string_mbwc_t *s = (const nanoem_unicode_string_mbwc_t *) string;
    int capacity = 0;
    nanoem_u8_t *buffer = NULL;
    nanoem_mark_unused(opaque);
    if (s) {
        capacity = s->length * (int) sizeof(*s->data);
        buffer = (nanoem_u8_t *) nanoem_calloc(capacity + 1, sizeof(*buffer), status);
        if (nanoem_is_not_null(buffer)) {
            memcpy(buffer, s->data, (size_t) capacity);
            buffer[capacity] = '\0';
            *length = (nanoem_rsize_t) capacity;
            nanoem_status_ptr_assign_succeeded(status);
        }
        else {
            *length = 0;
        }
    }
    else {
        *length = 0;
        nanoem_status_ptr_assign_null_object(status);
    }
    return buffer;
}

static nanoem_i32_t
nanoemUnicodeStringFactoryHashCallbackMBWC(void *opaque, const nanoem_unicode_string_t *string)
{
    const nanoem_unicode_string_mbwc_t *s = (const nanoem_unicode_string_mbwc_t *) string;
    nanoem_i32_t hash = s ? (nanoem_i32_t) MurmurHash(s->data, s->length * (int) sizeof(*s->data), 0) : -1;
    nanoem_mark_unused(opaque);
    return hash;
}

/* wcscmp over Windows' 16-bit wchar_t: unsigned units up to the first terminator. */
static int
compareUtf16(const mmdhl_char16_t *a, const mmdhl_char16_t *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a < *b ? -1 : *a > *b ? 1 : 0;
}

static int
nanoemUnicodeStringFactoryCompareCallbackMBWC(void *opaque, const nanoem_unicode_string_t *left, const nanoem_unicode_string_t *right)
{
    const nanoem_unicode_string_mbwc_t *lvalue = (const nanoem_unicode_string_mbwc_t *) left,
                                       *rvalue = (const nanoem_unicode_string_mbwc_t *) right;
    nanoem_mark_unused(opaque);
    return (nanoem_is_not_null(rvalue) && nanoem_is_not_null(lvalue)) && nanoem_is_not_null(rvalue->data) && nanoem_is_not_null(lvalue->data) ? compareUtf16(lvalue->data, rvalue->data) : -1;
}

static const nanoem_u8_t *
nanoemUnicodeStringFactoryGetCacheCallbackMBWC(void *opaque, const nanoem_unicode_string_t *string, nanoem_rsize_t *length, nanoem_codec_type_t codec, nanoem_status_t *status)
{
    const nanoem_unicode_string_mbwc_t *s = (const nanoem_unicode_string_mbwc_t *) string;
    nanoem_mark_unused(opaque);
    nanoem_mark_unused(codec);
    if (s->cache.data) {
        *length = s->cache.length;
        nanoem_status_ptr_assign_succeeded(status);
    }
    else {
        nanoem_status_ptr_assign_null_object(status);
    }
    return s->cache.data;
}

static void
nanoemUnicodeStringFactorySetCacheCallbackMBWC(void *opaque, nanoem_unicode_string_t *string, nanoem_rsize_t *length, nanoem_codec_type_t codec, nanoem_status_t *status)
{
    nanoem_unicode_string_mbwc_t *s = (nanoem_unicode_string_mbwc_t *) string;
    nanoem_mark_unused(opaque);
    if (s->cache.data) {
        nanoem_free(s->cache.data);
        s->cache.data = 0;
        *length = 0;
    }
    switch (codec) {
    case NANOEM_CODEC_TYPE_SJIS:
        s->cache.data = nanoemUnicodeStringFactoryToCp932CallbackMBWC(opaque, string, length, status);
        break;
    case NANOEM_CODEC_TYPE_UTF8:
        s->cache.data = nanoemUnicodeStringFactoryToUtf8CallbackMBWC(opaque, string, length, status);
        break;
    case NANOEM_CODEC_TYPE_UTF16:
        s->cache.data = nanoemUnicodeStringFactoryToUtf16CallbackMBWC(opaque, string, length, status);
        break;
    default:
        break;
    }
    s->cache.length = *length;
}

static void
nanoemUnicodeStringFactoryDestroyStringCallbackMBWC(void *opaque, nanoem_unicode_string_t *string)
{
    nanoem_unicode_string_mbwc_t *s = (nanoem_unicode_string_mbwc_t *) string;
    nanoem_mark_unused(opaque);
    if (s) {
        if (s->cache.data) {
            nanoem_free(s->cache.data);
        }
        nanoem_free(s->data);
        nanoem_free(s);
    }
}

static void
nanoemUnicodeStringFactoryDestroyByteArrayCallbackMBWC(void *opaque, nanoem_u8_t *string)
{
    nanoem_mark_unused(opaque);
    if (string) {
        nanoem_free(string);
    }
}

nanoem_unicode_string_factory_t *APIENTRY
nanoemUnicodeStringFactoryCreateMBWC(nanoem_status_t *status)
{
    nanoem_unicode_string_factory_t *factory;
    factory = nanoemUnicodeStringFactoryCreate(status);
    nanoemUnicodeStringFactorySetGetCacheCallback(factory, nanoemUnicodeStringFactoryGetCacheCallbackMBWC);
    nanoemUnicodeStringFactorySetSetCacheCallback(factory, nanoemUnicodeStringFactorySetCacheCallbackMBWC);
    nanoemUnicodeStringFactorySetCompareCallback(factory, nanoemUnicodeStringFactoryCompareCallbackMBWC);
    nanoemUnicodeStringFactorySetConvertFromCp932Callback(factory, nanoemUnicodeStringFactoryFromCp932CallbackMBWC);
    nanoemUnicodeStringFactorySetConvertFromUtf8Callback(factory, nanoemUnicodeStringFactoryFromUtf8CallbackMBWC);
    nanoemUnicodeStringFactorySetConvertFromUtf16Callback(factory, nanoemUnicodeStringFactoryFromUtf16CallbackMBWC);
    nanoemUnicodeStringFactorySetConvertToCp932Callback(factory, nanoemUnicodeStringFactoryToCp932CallbackMBWC);
    nanoemUnicodeStringFactorySetConvertToUtf8Callback(factory, nanoemUnicodeStringFactoryToUtf8CallbackMBWC);
    nanoemUnicodeStringFactorySetConvertToUtf16Callback(factory, nanoemUnicodeStringFactoryToUtf16CallbackMBWC);
    nanoemUnicodeStringFactorySetDestroyStringCallback(factory, nanoemUnicodeStringFactoryDestroyStringCallbackMBWC);
    nanoemUnicodeStringFactorySetDestroyByteArrayCallback(factory, nanoemUnicodeStringFactoryDestroyByteArrayCallbackMBWC);
    nanoemUnicodeStringFactorySetHashCallback(factory, nanoemUnicodeStringFactoryHashCallbackMBWC);
    return factory;
}

void APIENTRY
nanoemUnicodeStringFactoryDestroyMBWC(nanoem_unicode_string_factory_t *factory)
{
    nanoemUnicodeStringFactoryDestroy(factory);
}

void APIENTRY
nanoemUnicodeStringFactoryToUtf8OnStackMBWC(nanoem_unicode_string_factory_t *factory, const nanoem_unicode_string_t *string, nanoem_rsize_t *length, nanoem_u8_t *buffer, nanoem_rsize_t capacity, nanoem_status_t *status)
{
    const nanoem_unicode_string_mbwc_t *s = (const nanoem_unicode_string_mbwc_t *) string;
    nanoem_mark_unused(factory);
    if (s && capacity > 0) {
        int needed = encodeUtf8(s->data, s->length, NULL);
        if ((nanoem_rsize_t) needed <= capacity) {
            *length = (nanoem_rsize_t) encodeUtf8(s->data, s->length, buffer);
            nanoem_status_ptr_assign_succeeded(status);
        }
        else {
            /* WideCharToMultiByte fails with ERROR_INSUFFICIENT_BUFFER and writes nothing usable. */
            *length = 0;
            nanoem_status_ptr_assign(status, NANOEM_STATUS_ERROR_DECODE_UNICODE_STRING_FAILED);
        }
    }
    else {
        *length = 0;
        nanoem_status_ptr_assign_null_object(status);
    }
    if (capacity > 0) {
        buffer[*length >= capacity ? (capacity - 1) : *length] = '\0';
    }
}

const wchar_t *APIENTRY
nanoemUnicodeStringGetData(const nanoem_unicode_string_t *string)
{
    /* UTF-16 units, not wchar_t (UTF-32) on this platform; nothing here reads them as wchar_t. */
    const nanoem_unicode_string_mbwc_t *s = (const nanoem_unicode_string_mbwc_t *) string;
    return nanoem_is_not_null(s) ? (const wchar_t *) s->data : NULL;
}

nanoem_rsize_t APIENTRY
nanoemUnicodeStringGetLength(const nanoem_unicode_string_t *string)
{
    const nanoem_unicode_string_mbwc_t *s = (const nanoem_unicode_string_mbwc_t *) string;
    return nanoem_is_not_null(s) ? (nanoem_rsize_t) s->length : 0;
}

nanoem_unicode_string_factory_t *APIENTRY
nanoemUnicodeStringFactoryCreateEXT(nanoem_status_t *status)
{
    return nanoemUnicodeStringFactoryCreateMBWC(status);
}

void APIENTRY
nanoemUnicodeStringFactoryDestroyEXT(nanoem_unicode_string_factory_t *factory)
{
    nanoemUnicodeStringFactoryDestroyMBWC(factory);
}

void APIENTRY
nanoemUnicodeStringFactoryToUtf8OnStackEXT(nanoem_unicode_string_factory_t *factory, const nanoem_unicode_string_t *string, nanoem_rsize_t *length, nanoem_u8_t *buffer, nanoem_rsize_t capacity, nanoem_status_t *status)
{
    nanoemUnicodeStringFactoryToUtf8OnStackMBWC(factory, string, length, buffer, capacity, status);
}
