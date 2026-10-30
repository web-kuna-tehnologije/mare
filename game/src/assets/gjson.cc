// mare::assets::Json - the JSON tokenizer behind gjson.h, defined once.
//
// Every function here used to be `static` in the header, so each translation unit that parsed
// JSON - the asset registry and the glTF reader - compiled and carried its own copy of the
// parser. It is defined once here now. The C names are unchanged, so no call site moved; the
// class below is the same parser as an object for code that prefers one.

#include <cstdlib>
#include <cstring>

extern "C" {
#include "gjson.h"
}

// the tokenizer internals stay private to this file
namespace {
static int jpush(gjson_t *j, gjtype_t type, int start) {
    if (j->ntok == j->cap) { j->cap = j->cap ? j->cap * 2 : 256; j->t = (gjtok_t *)realloc(j->t, j->cap * sizeof *j->t); }
    gjtok_t *t = &j->t[j->ntok];
    t->type = (unsigned char)type; t->start = start; t->len = 0; t->ntok = 1; t->nchild = 0;
    return j->ntok++;
}
static void jws(gjson_t *j, int *i) { while (*i < j->n && (j->s[*i]==' '||j->s[*i]=='\t'||j->s[*i]=='\n'||j->s[*i]=='\r')) (*i)++; }

static int jparse_value(gjson_t *j, int *i, int depth);

static int jparse_string(gjson_t *j, int *i) {   // *i at the opening quote
    int tok = jpush(j, JS_STR, *i + 1);
    for ((*i)++; *i < j->n; (*i)++) {
        if (j->s[*i] == '\\') { (*i)++; continue; }
        if (j->s[*i] == '"') { j->t[tok].len = *i - j->t[tok].start; (*i)++; return tok; }
    }
    return -1;
}
static int jparse_value(gjson_t *j, int *i, int depth) {
    if (depth > 64) return -1;
    jws(j, i);
    if (*i >= j->n) return -1;
    char c = j->s[*i];
    if (c == '"') return jparse_string(j, i);
    if (c == '{' || c == '[') {
        int tok = jpush(j, c == '{' ? JS_OBJ : JS_ARR, *i);
        char close = c == '{' ? '}' : ']';
        (*i)++;
        for (;;) {
            jws(j, i);
            if (*i >= j->n) return -1;
            if (j->s[*i] == close) { (*i)++; break; }
            if (j->t[tok].nchild) { if (j->s[*i] != ',') return -1; (*i)++; jws(j, i); }
            if (close == '}') {
                if (*i >= j->n || j->s[*i] != '"' || jparse_string(j, i) < 0) return -1;
                jws(j, i);
                if (*i >= j->n || j->s[*i] != ':') return -1;
                (*i)++;
            }
            if (jparse_value(j, i, depth + 1) < 0) return -1;
            j->t[tok].nchild++;
        }
        j->t[tok].ntok = j->ntok - tok;
        return tok;
    }
    int start = *i;
    while (*i < j->n && !strchr(",]}} \t\n\r", j->s[*i])) (*i)++;
    int len = *i - start;
    if (len <= 0) return -1;
    gjtype_t t = JS_NUM;
    if (!strncmp(j->s + start, "true", 4) || !strncmp(j->s + start, "false", 5)) t = JS_BOOL;
    else if (!strncmp(j->s + start, "null", 4)) t = JS_NULL;
    int tok = jpush(j, t, start);
    j->t[tok].len = len;
    return tok;
}
int gjson_parse_impl(gjson_t *j, const char *s, int n) {
    j->s = s; j->n = n; j->t = NULL; j->ntok = j->cap = 0;
    int i = 0;
    int root = jparse_value(j, &i, 0);
    if (root != 0) return -1;
    jws(j, &i);
    return i == n ? 0 : -1;
}

// lookups. All take/return token indices; -1 = not found (safe to pass through).
int jget_impl(const gjson_t *j, int obj, const char *key) {
    if (obj < 0 || j->t[obj].type != JS_OBJ) return -1;
    int klen = (int)strlen(key);
    int i = obj + 1;
    for (int c = 0; c < j->t[obj].nchild; ++c) {
        int val = i + 1;
        if (j->t[i].len == klen && !strncmp(j->s + j->t[i].start, key, klen)) return val;
        i = val + j->t[val].ntok;
    }
    return -1;
}
int jlen_impl(const gjson_t *j, int arr) { return arr < 0 ? 0 : j->t[arr].nchild; }
int jat_impl(const gjson_t *j, int arr, int idx) {
    if (arr < 0 || j->t[arr].type != JS_ARR || idx < 0 || idx >= j->t[arr].nchild) return -1;
    int i = arr + 1;
    while (idx--) i += j->t[i].ntok;
    return i;
}
double jnum_impl(const gjson_t *j, int tok, double dflt) {
    if (tok < 0 || j->t[tok].type != JS_NUM) return dflt;
    return strtod(j->s + j->t[tok].start, NULL);
}
int jint_impl(const gjson_t *j, int tok, int dflt) { return (int)jnum_impl(j, tok, dflt); }
int jbool_impl(const gjson_t *j, int tok, int dflt) {
    if (tok < 0 || j->t[tok].type != JS_BOOL) return dflt;
    return j->s[j->t[tok].start] == 't';
}
int jstreq_impl(const gjson_t *j, int tok, const char *s) {
    return tok >= 0 && j->t[tok].type == JS_STR && (int)strlen(s) == j->t[tok].len
        && !strncmp(j->s + j->t[tok].start, s, j->t[tok].len);
}
void jstrcpy_impl(const gjson_t *j, int tok, char *dst, int cap) {   // basic escapes only
    int n = 0;
    if (tok >= 0 && j->t[tok].type == JS_STR)
        for (int i = 0; i < j->t[tok].len && n < cap - 1; ++i) {
            char c = j->s[j->t[tok].start + i];
            if (c == '\\' && i + 1 < j->t[tok].len) { c = j->s[j->t[tok].start + ++i]; if (c=='n') c=' '; if (c=='u') { i += 4; c='?'; } }
            dst[n++] = c;
        }
    dst[n] = 0;
}
void jfloats_impl(const gjson_t *j, int arr, float *out, int n) {    // leaves defaults where missing
    for (int i = 0; i < n; ++i) { int t = jat_impl(j, arr, i); if (t >= 0) out[i] = (float)jnum_impl(j, t, out[i]); }
}

}  // namespace

extern "C" {
int    gjson_parse(gjson_t *j, const char *s, int n) { return ::gjson_parse_impl(j, s, n); }
int    jget(const gjson_t *j, int obj, const char *key) { return ::jget_impl(j, obj, key); }
int    jlen(const gjson_t *j, int arr) { return ::jlen_impl(j, arr); }
int    jat(const gjson_t *j, int arr, int idx) { return ::jat_impl(j, arr, idx); }
double jnum(const gjson_t *j, int tok, double dflt) { return ::jnum_impl(j, tok, dflt); }
int    jint(const gjson_t *j, int tok, int dflt) { return ::jint_impl(j, tok, dflt); }
int    jbool(const gjson_t *j, int tok, int dflt) { return ::jbool_impl(j, tok, dflt); }
int    jstreq(const gjson_t *j, int tok, const char *s) { return ::jstreq_impl(j, tok, s); }
void   jstrcpy(const gjson_t *j, int tok, char *dst, int cap) { ::jstrcpy_impl(j, tok, dst, cap); }
void   jfloats(const gjson_t *j, int arr, float *out, int n) { ::jfloats_impl(j, arr, out, n); }
}

namespace mare::assets {
int    Json::parse(gjson_t *j, const char *s, int n) { return ::gjson_parse(j, s, n); }
void   Json::free_tokens(gjson_t *j) { ::free(j->t); j->t = nullptr; j->ntok = j->cap = 0; }
int    Json::get(const gjson_t *j, int obj, const char *key) { return ::jget(j, obj, key); }
int    Json::len(const gjson_t *j, int arr) { return ::jlen(j, arr); }
int    Json::at(const gjson_t *j, int arr, int i) { return ::jat(j, arr, i); }
double Json::num(const gjson_t *j, int tok, double dflt) { return ::jnum(j, tok, dflt); }
int    Json::integer(const gjson_t *j, int tok, int dflt) { return ::jint(j, tok, dflt); }
int    Json::boolean(const gjson_t *j, int tok, int dflt) { return ::jbool(j, tok, dflt); }
int    Json::str_eq(const gjson_t *j, int tok, const char *s) { return ::jstreq(j, tok, s); }
void   Json::str_copy(const gjson_t *j, int tok, char *dst, int cap) { ::jstrcpy(j, tok, dst, cap); }
void   Json::floats(const gjson_t *j, int arr, float *out, int n) { ::jfloats(j, arr, out, n); }
} // namespace mare::assets
