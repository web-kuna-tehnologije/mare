// Minimal JSON tokenizer shared by the asset runtime (glTF documents, asset descriptors,
// the manifest). Tokens are stored in preorder; tok.ntok = tokens in the subtree including
// itself, so skipping a value is `i += tok[i].ntok`. Object children alternate key / value.
// Everything is static so the header can be included by any translation unit (the game is a
// single TU; the headless tests include the .c files directly).

#ifndef MARE_GJSON_H
#define MARE_GJSON_H

#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { JS_NULL, JS_BOOL, JS_NUM, JS_STR, JS_ARR, JS_OBJ } gjtype_t;
typedef struct gjtok_t { unsigned char type; int start, len; int ntok, nchild; } gjtok_t;
typedef struct gjson_t { const char *s; int n; gjtok_t *t; int ntok, cap; } gjson_t;

// The parser is defined once in gjson.cc; these are the names every call site already uses.
int    gjson_parse(gjson_t *j, const char *s, int n);   // 1 on success; free(j->t) when done
int    jget(const gjson_t *j, int obj, const char *key);
int    jlen(const gjson_t *j, int arr);
int    jat(const gjson_t *j, int arr, int idx);
double jnum(const gjson_t *j, int tok, double dflt);
int    jint(const gjson_t *j, int tok, int dflt);
int    jbool(const gjson_t *j, int tok, int dflt);
int    jstreq(const gjson_t *j, int tok, const char *s);
void   jstrcpy(const gjson_t *j, int tok, char *dst, int cap);
void   jfloats(const gjson_t *j, int arr, float *out, int n);

#ifdef __cplusplus
}

// The same parser as an object, for C++ callers that prefer one.
namespace mare::assets {
class Json {
public:
    static int    parse(gjson_t *j, const char *s, int n);
    static void   free_tokens(gjson_t *j);
    static int    get(const gjson_t *j, int obj, const char *key);
    static int    len(const gjson_t *j, int arr);
    static int    at(const gjson_t *j, int arr, int i);
    static double num(const gjson_t *j, int tok, double dflt);
    static int    integer(const gjson_t *j, int tok, int dflt);
    static int    boolean(const gjson_t *j, int tok, int dflt);
    static int    str_eq(const gjson_t *j, int tok, const char *s);
    static void   str_copy(const gjson_t *j, int tok, char *dst, int cap);
    static void   floats(const gjson_t *j, int arr, float *out, int n);
};
} // namespace mare::assets
#endif

#endif
