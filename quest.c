/* quest.c  Treaty of Babel module for Quest files
 *
 * Quest 5+ packages are ZIP archives (".quest" extension) whose directory
 * contains game.aslx plus resources. Newly published packages also brand
 * UUID://…// in the ZIP comment and embed metadata.iFiction. Legacy
 * packages expose <gameid> inside the deflated game.aslx XML. Raw .aslx
 * files are also claimed.
 *
 * Older Quest 1–4 story files use ".cas" (compiled) or ".asl" (text).
 * Those have no embedded IFID; babel falls back to "QUEST-" plus the file MD5.
 *
 * This file depends on treaty_builder.h and tinfl.h
 *
 * This file is public domain, but note that any changes to this file
 * may render it noncompliant with the Treaty of Babel
 */

#define FORMAT quest
#define HOME_PAGE "https://textadventures.co.uk/quest"
#define FORMAT_EXT ".quest,.aslx,.cas,.asl"

#include "treaty_builder.h"
#include "ifiction.h"
#include "tinfl.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------
 * Minimal ZIP reader (central directory) + tinfl raw inflate.
 * ---------------------------------------------------------------------- */

static uint32 rd16(const unsigned char *p)
{
    return (uint32)p[0] | ((uint32)p[1] << 8);
}

static uint32 rd32(const unsigned char *p)
{
    return (uint32)p[0] | ((uint32)p[1] << 8) | ((uint32)p[2] << 16) |
           ((uint32)p[3] << 24);
}

static bool zip_find_entry(const unsigned char *zip, int32 extent,
                           const char *name, int32 *off, int32 *comp,
                           int32 *raw, int *method)
{
    const unsigned char *eocd = NULL;
    int32 maxback, i;
    int count;
    uint32 cd_off;
    const unsigned char *p;
    size_t namelen;
    const unsigned char *ci_p = NULL;
    int ci_method = 0;
    int32 ci_comp = 0, ci_raw = 0;
    uint32 ci_local = 0;
    int e;

    if (extent < 22 || memcmp(zip, "PK\003\004", 4) != 0)
        return false;

    maxback = extent < 65557 ? extent : 65557;
    for (i = extent - 22; i >= 0 && i + maxback + 22 >= extent; --i) {
        if (zip[i] == 'P' && zip[i + 1] == 'K' && zip[i + 2] == 5 &&
            zip[i + 3] == 6) {
            eocd = zip + i;
            break;
        }
    }
    if (!eocd)
        return false;

    count = (int)rd16(eocd + 10);
    cd_off = rd32(eocd + 16);
    if (cd_off >= (uint32)extent)
        return false;
    p = zip + cd_off;
    namelen = strlen(name);

    for (e = 0; e < count && p + 46 <= zip + extent; ++e) {
        int m, nl, el, cl;
        int32 c, r;
        uint32 local;
        const unsigned char *ename;
        bool exact, ci;
        size_t k;

        if (!(p[0] == 'P' && p[1] == 'K' && p[2] == 1 && p[3] == 2))
            break;
        m = (int)rd16(p + 10);
        c = (int32)rd32(p + 20);
        r = (int32)rd32(p + 24);
        nl = (int)rd16(p + 28);
        el = (int)rd16(p + 30);
        cl = (int)rd16(p + 32);
        local = rd32(p + 42);
        ename = p + 46;
        if (ename + nl > zip + extent)
            break;

        exact = ((size_t)nl == namelen && memcmp(ename, name, namelen) == 0);
        ci = false;
        if (!exact && (size_t)nl == namelen) {
            ci = true;
            for (k = 0; k < namelen; k++) {
                if (tolower(ename[k]) != tolower((unsigned char)name[k])) {
                    ci = false;
                    break;
                }
            }
        }

        if (exact || ci) {
            const unsigned char *lh = zip + local;
            if (lh + 30 <= zip + extent && lh[0] == 'P' && lh[1] == 'K' &&
                lh[2] == 3 && lh[3] == 4) {
                int32 payload = (int32)(lh - zip) + 30 + (int32)rd16(lh + 26) +
                                (int32)rd16(lh + 28);
                if (payload >= 0 && payload + c <= extent) {
                    if (exact) {
                        *off = payload;
                        *comp = c;
                        *raw = r;
                        *method = m;
                        return true;
                    }
                    if (!ci_p) {
                        ci_p = lh;
                        ci_method = m;
                        ci_comp = c;
                        ci_raw = r;
                        ci_local = (uint32)payload;
                    }
                }
            }
        }
        p += 46 + nl + el + cl;
    }

    if (ci_p) {
        *off = (int32)ci_local;
        *comp = ci_comp;
        *raw = ci_raw;
        *method = ci_method;
        return true;
    }
    return false;
}

static unsigned char *inflate_raw(const unsigned char *src, int32 srclen,
                                  int32 rawlen)
{
    unsigned char *out;
    size_t got;

    if (rawlen <= 0 || srclen <= 0)
        return NULL;
    out = (unsigned char *)malloc((size_t)rawlen + 1);
    if (!out)
        return NULL;
    got = tinfl_decompress_mem_to_mem(out, (size_t)rawlen, src, (size_t)srclen,
                                      0);
    if (got == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED || (int32)got != rawlen) {
        free(out);
        return NULL;
    }
    out[rawlen] = '\0';
    return out;
}

static unsigned char *zip_extract(const unsigned char *zip, int32 extent,
                                  const char *name, int32 *outlen)
{
    int32 off, comp, raw;
    int method;

    if (!zip_find_entry(zip, extent, name, &off, &comp, &raw, &method))
        return NULL;
    if (method == 0) {
        unsigned char *out = (unsigned char *)malloc((size_t)raw + 1);
        if (!out)
            return NULL;
        memcpy(out, zip + off, (size_t)raw);
        out[raw] = '\0';
        *outlen = raw;
        return out;
    }
    if (method == 8) {
        unsigned char *out = inflate_raw(zip + off, comp, raw);
        if (out)
            *outlen = raw;
        return out;
    }
    return NULL;
}

static bool quest_package_found(unsigned char *story_file, int32 extent)
{
    int32 off, comp, raw;
    int method;

    if (story_file == NULL || extent < 30)
        return false;
    if (memcmp(story_file, "PK\003\004", 4) != 0)
        return false;
    return zip_find_entry(story_file, extent, "game.aslx", &off, &comp, &raw,
                          &method);
}

static bool aslx_header_found(unsigned char *story_file, int32 extent)
{
    int32 i = 0;
    int32 limit;

    if (story_file == NULL)
        return false;
    if (extent >= 3 && story_file[0] == 0xef && story_file[1] == 0xbb &&
        story_file[2] == 0xbf)
        i = 3;
    while (i < extent && isspace(story_file[i]))
        i++;
    if (i >= extent || story_file[i] != '<')
        return false;
    limit = extent < 2048 ? extent : 2048;
    for (; i + 4 < limit; i++) {
        if (memcmp(story_file + i, "<asl", 4) == 0 &&
            (story_file[i + 4] == ' ' || story_file[i + 4] == '>' ||
             story_file[i + 4] == '\t' || story_file[i + 4] == '\r' ||
             story_file[i + 4] == '\n'))
            return true;
    }
    return false;
}

/* If string is found, returns a pointer to the char AFTER it */
static unsigned char *find_string(unsigned char *storyvp, int32 extent,
                                  const char *string, int32 stringlength)
{
    int32 i;
    for (i = 0; i < extent - stringlength - 1; i++)
        if (memcmp(string, storyvp + i, (size_t)stringlength) == 0)
            return storyvp + i + stringlength;
    return NULL;
}

/* Compiled Quest 1–4: QCGF001 / QCGF002 / QCGF003 */
static bool quest_cas_header_found(unsigned char *story_file)
{
    if (story_file == NULL)
        return false;
    if (memcmp(story_file, "QCGF00", 6) != 0)
        return false;
    return story_file[6] >= '1' && story_file[6] <= '3';
}

/* Quest 1–4 ASL source: "define game" ... "asl-version" ... "end define" */
static bool asl_header_found(unsigned char *story_file, int32 extent)
{
    unsigned char *start, *end, *version;
    size_t offset;

    if (story_file == NULL)
        return false;
    start = find_string(story_file, extent, "define game ", 12);
    if (start == NULL)
        return false;
    offset = (size_t)(start - story_file);
    end = find_string(start, extent - (int32)offset, "end define", 10);
    if (end == NULL)
        return false;
    version = find_string(start, (int32)(end - start) - 10, "asl-version", 11);
    return version != NULL;
}

static int32 claim_story_file(void *storyvp, int32 extent)
{
    if (extent > 30 && quest_package_found(storyvp, extent))
        return VALID_STORY_FILE_RV;
    if (extent > 10 && aslx_header_found(storyvp, extent))
        return VALID_STORY_FILE_RV;
    if (extent > 8 && quest_cas_header_found(storyvp))
        return VALID_STORY_FILE_RV;
    if (extent > 25 && asl_header_found(storyvp, extent))
        return VALID_STORY_FILE_RV;
    return INVALID_STORY_FILE_RV;
}

static char *get_aslx_text(void *storyvp, int32 extent, int32 *len)
{
    unsigned char *story = storyvp;

    if (extent >= 4 && memcmp(story, "PK\003\004", 4) == 0) {
        int32 l = 0;
        unsigned char *raw = zip_extract(story, extent, "game.aslx", &l);
        if (!raw)
            return NULL;
        *len = l;
        return (char *)raw;
    }
    {
        char *s = (char *)malloc((size_t)extent + 1);
        if (!s)
            return NULL;
        memcpy(s, story, (size_t)extent);
        s[extent] = '\0';
        *len = extent;
        return s;
    }
}

static char *coverleaf_from_ifiction(const char *md)
{
    const char *lo, *hi, *open, *gt, *close;
    size_t n;
    char *out;

    lo = strstr(md, "<quest");
    if (!lo)
        return NULL;
    if (!(lo[6] == '>' || isspace((unsigned char)lo[6])))
        return NULL;
    hi = strstr(lo, "</quest>");
    if (!hi)
        return NULL;
    open = strstr(lo, "<coverleafname");
    if (!open || open >= hi)
        return NULL;
    gt = strchr(open, '>');
    if (!gt || gt >= hi || gt[-1] == '/')
        return NULL;
    close = strstr(gt + 1, "</coverleafname>");
    if (!close || close > hi)
        return NULL;
    n = (size_t)(close - (gt + 1));
    out = (char *)malloc(n + 1);
    if (!out)
        return NULL;
    memcpy(out, gt + 1, n);
    out[n] = '\0';
    return out;
}

static int cover_format_of(const unsigned char *d, int32 n)
{
    if (n >= 8 && d[0] == 137 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G')
        return PNG_COVER_FORMAT;
    if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF)
        return JPEG_COVER_FORMAT;
    return 0;
}

static unsigned char *get_cover(void *storyvp, int32 extent, int32 *len,
                                int *fmt)
{
    unsigned char *story = storyvp;
    char *name = NULL;
    int32 mlen = 0, clen = 0;
    unsigned char *md, *img;
    int f;

    if (!(extent >= 4 && memcmp(story, "PK\003\004", 4) == 0))
        return NULL;

    md = zip_extract(story, extent, "metadata.iFiction", &mlen);
    if (!md)
        return NULL;
    name = coverleaf_from_ifiction((char *)md);
    free(md);
    if (!name || !*name) {
        free(name);
        return NULL;
    }

    img = zip_extract(story, extent, name, &clen);
    free(name);
    if (!img)
        return NULL;
    f = cover_format_of(img, clen);
    if (f == 0) {
        free(img);
        return NULL;
    }
    *fmt = f;
    *len = clen;
    return img;
}

static int32 get_story_file_metadata_extent(void *story_file, int32 extent)
{
    int32 mlen = 0;
    unsigned char *md;

    if (!(extent >= 4 && memcmp(story_file, "PK\003\004", 4) == 0))
        return NO_REPLY_RV;
    md = zip_extract(story_file, extent, "metadata.iFiction", &mlen);
    if (!md)
        return NO_REPLY_RV;
    free(md);
    return mlen + 1;
}

static int32 get_story_file_metadata(void *story_file, int32 extent,
                                     char *output, int32 output_extent)
{
    int32 mlen = 0;
    unsigned char *md;

    if (!(extent >= 4 && memcmp(story_file, "PK\003\004", 4) == 0))
        return NO_REPLY_RV;
    md = zip_extract(story_file, extent, "metadata.iFiction", &mlen);
    if (!md)
        return NO_REPLY_RV;
    if (mlen + 1 > output_extent) {
        free(md);
        return INVALID_USAGE_RV;
    }
    memcpy(output, md, (size_t)mlen);
    output[mlen] = '\0';
    free(md);
    return mlen + 1;
}

static int32 get_story_file_cover_extent(void *story_file, int32 extent)
{
    int32 len = 0;
    int fmt = 0;
    unsigned char *c = get_cover(story_file, extent, &len, &fmt);
    if (!c)
        return NO_REPLY_RV;
    free(c);
    return len;
}

static int32 get_story_file_cover_format(void *story_file, int32 extent)
{
    int32 len = 0;
    int fmt = 0;
    unsigned char *c = get_cover(story_file, extent, &len, &fmt);
    if (!c)
        return NO_REPLY_RV;
    free(c);
    return fmt;
}

static int32 get_story_file_cover(void *story_file, int32 extent, void *output,
                                  int32 output_extent)
{
    int32 len = 0;
    int fmt = 0;
    unsigned char *c = get_cover(story_file, extent, &len, &fmt);
    if (!c)
        return NO_REPLY_RV;
    if (len > output_extent) {
        free(c);
        return INVALID_USAGE_RV;
    }
    memcpy(output, c, (size_t)len);
    free(c);
    return len;
}

static int32 get_story_file_IFID(void *storyvp, int32 extent, char *output,
                                 int32 output_extent)
{
    int32 ix;
    char *atext = NULL;
    unsigned char *search;
    int32 search_len;
    int32 atext_len = 0;
    unsigned char *p;
    char guid[64];
    int n;
    int32 left;

    if (claim_story_file(storyvp, extent) != VALID_STORY_FILE_RV)
        return INVALID_STORY_FILE_RV;

    ix = find_uuid_ifid_marker(storyvp, extent, output, output_extent);
    if (ix == VALID_STORY_FILE_RV || ix == INVALID_USAGE_RV)
        return ix;

    search = storyvp;
    search_len = extent;
    if (extent >= 4 && memcmp(storyvp, "PK\003\004", 4) == 0) {
        atext = get_aslx_text(storyvp, extent, &atext_len);
        if (atext) {
            search = (unsigned char *)atext;
            search_len = atext_len;
        }
    } else if (aslx_header_found(storyvp, extent)) {
        /* raw .aslx: search in place */
    } else {
        /* .asl / .cas: no gameid */
        ASSERT_OUTPUT_SIZE(7);
        strcpy(output, "QUEST-");
        return INCOMPLETE_REPLY_RV;
    }

    p = NULL;
    {
        int32 i;
        int32 lim = search_len - 8;
        for (i = 0; i < lim; i++) {
            if (memcmp(search + i, "<gameid>", 8) == 0) {
                p = search + i + 8;
                break;
            }
        }
    }
    if (p != NULL) {
        n = 0;
        left = search_len - (int32)(p - search);
        while (n < 63 && n < left && p[n] != '<' &&
               (isxdigit(p[n]) || p[n] == '-')) {
            guid[n] = (char)toupper(p[n]);
            n++;
        }
        guid[n] = 0;
        if (n >= 8 && n < left && p[n] == '<') {
            ASSERT_OUTPUT_SIZE(n + 1);
            memcpy(output, guid, (size_t)(n + 1));
            free(atext);
            return 1;
        }
    }

    free(atext);
    ASSERT_OUTPUT_SIZE(7);
    strcpy(output, "QUEST-");
    return INCOMPLETE_REPLY_RV;
}
