#include "co/json.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>

// ---- nodes ------------------------------------------------------------------

static JsonNode* jn_new(uint32 type) {
    JsonNode* h = (JsonNode*) co::zalloc(sizeof(JsonNode));
    h->type = type;
    return h;
}

static char* jn_strdup(const char* p, size_t n) {
    char* s = (char*) co::alloc(n + 1);
    if (n > 0) memcpy(s, p, n);
    s[n] = '\0';
    return s;
}

static void jn_free(JsonNode* h) {
    if (h == 0) return;
    if (h->type == json::t_string) {
        co::free(h->s, h->size + 1);
    } else if (h->type == json::t_array || h->type == json::t_object) {
        for (uint32 i = 0; i < h->size; ++i) {
            jn_free(h->kids[i]);
            if (h->keys) co::free(h->keys[i], strlen(h->keys[i]) + 1);
        }
        if (h->kids) co::free(h->kids, h->cap * sizeof(JsonNode*));
        if (h->keys) co::free(h->keys, h->cap * sizeof(char*));
    }
    co::free(h, sizeof(JsonNode));
}

// room for one more element
static void jn_grow(JsonNode* h) {
    if (h->size < h->cap) return;
    const uint32 cap = h->cap ? h->cap * 2 : 8;
    h->kids = (JsonNode**) co::realloc(h->kids, h->cap * sizeof(JsonNode*), cap * sizeof(JsonNode*));
    if (h->type == json::t_object) {
        h->keys = (char**) co::realloc(h->keys, h->cap * sizeof(char*), cap * sizeof(char*));
    }
    h->cap = cap;
}

static void jn_push(JsonNode* h, char* key, JsonNode* v) {
    jn_grow(h);
    if (h->keys) h->keys[h->size] = key;
    h->kids[h->size] = v;
    h->size++;
}

static int64 jn_find(const JsonNode* h, const char* key) {
    if (h == 0 || h->type != json::t_object) return -1;
    for (uint32 i = 0; i < h->size; ++i) {
        if (strcmp(h->keys[i], key) == 0) return (int64)i;
    }
    return -1;
}

static JsonNode* jn_dup(const JsonNode* h) {
    if (h == 0) return 0;
    JsonNode* r = jn_new(h->type);
    r->b = h->b;
    r->i = h->i;
    r->d = h->d;
    if (h->type == json::t_string) {
        r->s = jn_strdup(h->s, h->size);
        r->size = h->size;
    } else if (h->type == json::t_array || h->type == json::t_object) {
        for (uint32 i = 0; i < h->size; ++i) {
            char* k = h->keys ? jn_strdup(h->keys[i], strlen(h->keys[i])) : 0;
            jn_push(r, k, jn_dup(h->kids[i]));
        }
    }
    return r;
}

// The value handed out for missing entries. Cleared on every lookup, so
// anything written into it by mistake is discarded rather than leaked.
static JsonNode* json_null_slot = 0;

static JsonNode** jn_null() {
    jn_free(json_null_slot);
    json_null_slot = 0;
    return &json_null_slot;
}

// ---- writer -------------------------------------------------------------------

static const char* const kJsonHex = "0123456789abcdef";

static void jn_write_str(fastring* s, const char* p, size_t n) {
    s->append_char(0x22);                        // '"'
    size_t run = 0;                              // start of unescaped run
    for (size_t i = 0; i < n; ++i) {
        const unsigned char c = (unsigned char)p[i];
        char esc = 0;
        switch (c) {
          case 0x22: esc = 0x22; break;          // '"'
          case 0x5c: esc = 0x5c; break;          // '\\'
          case 0x08: esc = 'b'; break;
          case 0x0c: esc = 'f'; break;
          case 0x0a: esc = 'n'; break;
          case 0x0d: esc = 'r'; break;
          case 0x09: esc = 't'; break;
          default:
            if (c >= 0x20) continue;
            esc = 'u';
        }
        s->append(p + run, i - run);
        run = i + 1;
        s->append_char(0x5c);
        s->append_char(esc);
        if (esc == 'u') {
            s->append("00", 2);
            s->append_char(kJsonHex[c >> 4]);
            s->append_char(kJsonHex[c & 15]);
        }
    }
    s->append(p + run, n - run);
    s->append_char(0x22);
}

static void jn_newline(fastring* s, int indent, int depth) {
    if (indent <= 0) return;
    s->append_char('\n');
    s->append_chars((size_t)(indent * depth), ' ');
}

static void jn_write(const JsonNode* h, fastring* s, int indent, int depth, int mdp) {
    if (h == 0) {
        s->append("null", 4);
        return;
    }
    switch (h->type) {
      case json::t_bool:
        s->append_bool(h->b);
        break;
      case json::t_int:
        s->append_int(h->i);
        break;
      case json::t_double:
        if (isfinite(h->d)) {
            s->append_double(h->d, mdp);
        } else {
            s->append("null", 4);
        }
        break;
      case json::t_string:
        jn_write_str(s, h->s, h->size);
        break;
      case json::t_array:
      case json::t_object: {
        const bool obj = h->type == json::t_object;
        s->append_char(obj ? '{' : '[');
        for (uint32 i = 0; i < h->size; ++i) {
            if (i > 0) s->append_char(',');
            jn_newline(s, indent, depth + 1);
            if (obj) {
                jn_write_str(s, h->keys[i], strlen(h->keys[i]));
                s->append_char(':');
                if (indent > 0) s->append_char(' ');
            }
            jn_write(h->kids[i], s, indent, depth + 1, mdp);
        }
        if (h->size > 0) jn_newline(s, indent, depth);
        s->append_char(obj ? '}' : ']');
        break;
      }
      default:
        s->append("null", 4);
    }
}

// ---- parser -------------------------------------------------------------------

struct JsonParser {
    const char* p;
    const char* e;
    int depth;
};

static const int kJsonMaxDepth = 512;

static void jp_ws(JsonParser* P) {
    while (P->p < P->e && (*P->p == ' ' || *P->p == '\n' || *P->p == '\r' || *P->p == '\t')) {
        P->p++;
    }
}

static int jp_hex(char c) {
    if ('0' <= c && c <= '9') return c - '0';
    if ('a' <= c && c <= 'f') return c - 'a' + 10;
    if ('A' <= c && c <= 'F') return c - 'A' + 10;
    return -1;
}

// four hex digits at P->p; -1 on error
static long jp_u4(JsonParser* P) {
    if (P->e - P->p < 4) return -1;
    long v = 0;
    for (int k = 0; k < 4; ++k) {
        const int h = jp_hex(P->p[k]);
        if (h < 0) return -1;
        v = (v << 4) | h;
    }
    P->p += 4;
    return v;
}

static void jp_utf8(fastring* s, unsigned long c) {
    if (c < 0x80) {
        s->append_char((char)c);
    } else if (c < 0x800) {
        s->append_char((char)(0xc0 | (c >> 6)));
        s->append_char((char)(0x80 | (c & 0x3f)));
    } else if (c < 0x10000) {
        s->append_char((char)(0xe0 | (c >> 12)));
        s->append_char((char)(0x80 | ((c >> 6) & 0x3f)));
        s->append_char((char)(0x80 | (c & 0x3f)));
    } else {
        s->append_char((char)(0xf0 | (c >> 18)));
        s->append_char((char)(0x80 | ((c >> 12) & 0x3f)));
        s->append_char((char)(0x80 | ((c >> 6) & 0x3f)));
        s->append_char((char)(0x80 | (c & 0x3f)));
    }
}

// a string starting at the opening quote, into @out (cleared first)
static bool jp_string(JsonParser* P, fastring* out) {
    out->clear();
    P->p++;                                      // opening quote
    while (P->p < P->e) {
        const char* q = P->p;
        while (q < P->e && *q != 0x22 && *q != 0x5c && (unsigned char)*q >= 0x20) ++q;
        out->append(P->p, q - P->p);
        P->p = q;
        if (q == P->e) return false;
        const char c = *P->p++;
        if (c == 0x22) return true;              // closing quote
        if (c != 0x5c) return false;             // raw control character
        if (P->p == P->e) return false;
        const char x = *P->p++;
        switch (x) {
          case 0x22: out->append_char(0x22); break;
          case 0x5c: out->append_char(0x5c); break;
          case '/': out->append_char('/'); break;
          case 'b': out->append_char('\b'); break;
          case 'f': out->append_char('\f'); break;
          case 'n': out->append_char('\n'); break;
          case 'r': out->append_char('\r'); break;
          case 't': out->append_char('\t'); break;
          case 'u': {
            long u = jp_u4(P);
            if (u < 0) return false;
            if (u >= 0xd800 && u <= 0xdbff) {    // high surrogate
                if (P->e - P->p < 6 || P->p[0] != 0x5c || P->p[1] != 'u') return false;
                P->p += 2;
                const long lo = jp_u4(P);
                if (lo < 0xdc00 || lo > 0xdfff) return false;
                u = 0x10000 + ((u - 0xd800) << 10) + (lo - 0xdc00);
            } else if (u >= 0xdc00 && u <= 0xdfff) {
                return false;                    // lone low surrogate
            }
            jp_utf8(out, (unsigned long)u);
            break;
          }
          default:
            return false;
        }
    }
    return false;
}

static bool jp_digits(JsonParser* P) {
    const char* const b = P->p;
    while (P->p < P->e && '0' <= *P->p && *P->p <= '9') P->p++;
    return P->p != b;
}

static JsonNode* jp_number(JsonParser* P) {
    const char* const b = P->p;
    bool is_int = true;
    if (P->p < P->e && *P->p == '-') P->p++;
    if (!jp_digits(P)) return 0;
    if (P->p < P->e && *P->p == '.') {
        P->p++;
        is_int = false;
        if (!jp_digits(P)) return 0;
    }
    if (P->p < P->e && (*P->p == 'e' || *P->p == 'E')) {
        P->p++;
        is_int = false;
        if (P->p < P->e && (*P->p == '+' || *P->p == '-')) P->p++;
        if (!jp_digits(P)) return 0;
    }

    fastring t(b, P->p - b);                     // NUL-terminated copy
    if (is_int) {
        errno = 0;
        const int64 v = strtoll(t.c_str(), 0, 10);
        if (errno == 0) {
            JsonNode* h = jn_new(json::t_int);
            h->i = v;
            return h;
        }
    }
    JsonNode* h = jn_new(json::t_double);
    h->d = strtod(t.c_str(), 0);
    return h;
}

static bool jp_literal(JsonParser* P, const char* w, size_t n) {
    if ((size_t)(P->e - P->p) < n || memcmp(P->p, w, n) != 0) return false;
    P->p += n;
    return true;
}

// one value into *out; on failure *out holds whatever was built (to free)
static bool jp_value(JsonParser* P, JsonNode** out, fastring* tmp) {
    *out = 0;
    jp_ws(P);
    if (P->p == P->e) return false;
    const char c = *P->p;

    if (c == '{' || c == '[') {
        if (++P->depth > kJsonMaxDepth) return false;
        const bool obj = c == '{';
        JsonNode* h = jn_new(obj ? json::t_object : json::t_array);
        *out = h;
        P->p++;
        jp_ws(P);
        if (P->p < P->e && *P->p == (obj ? '}' : ']')) {
            P->p++;
            P->depth--;
            return true;
        }
        while (true) {
            char* key = 0;
            if (obj) {
                jp_ws(P);
                if (P->p == P->e || *P->p != 0x22) return false;
                if (!jp_string(P, tmp)) return false;
                key = jn_strdup(tmp->data(), tmp->size());
                jp_ws(P);
                if (P->p == P->e || *P->p != ':') {
                    co::free(key, strlen(key) + 1);
                    return false;
                }
                P->p++;
            }
            JsonNode* v = 0;
            const bool ok = jp_value(P, &v, tmp);
            jn_push(h, key, v);                  // owned by h even on failure
            if (!ok) return false;
            jp_ws(P);
            if (P->p == P->e) return false;
            if (*P->p == ',') {
                P->p++;
                continue;
            }
            if (*P->p == (obj ? '}' : ']')) {
                P->p++;
                P->depth--;
                return true;
            }
            return false;
        }
    }

    if (c == 0x22) {
        if (!jp_string(P, tmp)) return false;
        JsonNode* h = jn_new(json::t_string);
        h->s = jn_strdup(tmp->data(), tmp->size());
        h->size = (uint32)tmp->size();
        *out = h;
        return true;
    }
    if (c == 't' || c == 'f') {
        const bool v = c == 't';
        if (!jp_literal(P, v ? "true" : "false", v ? 4 : 5)) return false;
        JsonNode* h = jn_new(json::t_bool);
        h->b = v;
        *out = h;
        return true;
    }
    if (c == 'n') return jp_literal(P, "null", 4);

    *out = jp_number(P);
    return *out != 0;
}

// ---- Json ---------------------------------------------------------------------

namespace json {

bool Json::as_bool() const {
    if (_h == 0) return false;
    switch (_h->type) {
      case t_bool: return _h->b;
      case t_int: return _h->i != 0;
      case t_double: return _h->d != 0;
      case t_string: return strcmp(_h->s, "true") == 0 || strcmp(_h->s, "1") == 0;
      default: return false;
    }
}

int64 Json::as_int64() const {
    if (_h == 0) return 0;
    switch (_h->type) {
      case t_int: return _h->i;
      case t_double: return (int64)_h->d;
      case t_bool: return _h->b ? 1 : 0;
      case t_string: return (int64)strtoll(_h->s, 0, 0);
      default: return 0;
    }
}

double Json::as_double() const {
    if (_h == 0) return 0;
    switch (_h->type) {
      case t_double: return _h->d;
      case t_int: return (double)_h->i;
      case t_bool: return _h->b ? 1 : 0;
      case t_string: return strtod(_h->s, 0);
      default: return 0;
    }
}

fastring Json::as_string() const {
    if (this->is_string()) {
        fastring r(_h->s, _h->size);
        return r;
    }
    fastring r(64);
    jn_write(_h, &r, 0, 0, 16);
    return r;
}

uint32 Json::size() const {
    if (_h == 0) return 0;
    if (_h->type == t_array || _h->type == t_object || _h->type == t_string) return _h->size;
    return 0;
}

Json* Json::at(uint32 i) const {
    if (_h && (_h->type == t_array || _h->type == t_object) && i < _h->size) {
        return (Json*) &_h->kids[i];
    }
    return (Json*) jn_null();
}

Json* Json::get(const char* key) const {
    const int64 i = jn_find(_h, key);
    if (i >= 0) return (Json*) &_h->kids[i];
    return (Json*) jn_null();
}

const char* Json::key_at(uint32 i) const {
    if (_h && _h->type == t_object && i < _h->size) return _h->keys[i];
    return "";
}

bool Json::has_member(const char* key) const {
    return jn_find(_h, key) >= 0;
}

void Json::set_bool(bool v) {
    this->reset();
    _h = jn_new(t_bool);
    _h->b = v;
}

void Json::set_int(int64 v) {
    this->reset();
    _h = jn_new(t_int);
    _h->i = v;
}

void Json::set_double(double v) {
    this->reset();
    _h = jn_new(t_double);
    _h->d = v;
}

void Json::set_str(const char* s, size_t n) {
    char* const p = jn_strdup(s, n);             // @s may point into this value
    this->reset();
    _h = jn_new(t_string);
    _h->s = p;
    _h->size = (uint32)n;
}

void Json::set_array() {
    if (this->is_array()) return;
    this->reset();
    _h = jn_new(t_array);
}

void Json::set_object() {
    if (this->is_object()) return;
    this->reset();
    _h = jn_new(t_object);
}

Json* Json::push() {
    this->set_array();
    jn_push(_h, 0, 0);
    return (Json*) &_h->kids[_h->size - 1];
}

Json* Json::add_member(const char* key) {
    this->set_object();
    jn_push(_h, jn_strdup(key, strlen(key)), 0);
    return (Json*) &_h->kids[_h->size - 1];
}

Json* Json::member(const char* key) {
    const int64 i = jn_find(_h, key);
    if (i >= 0) return (Json*) &_h->kids[i];
    return this->add_member(key);
}

void Json::push_json(Json& v) {
    JsonNode* const n = v._h;
    v._h = 0;
    this->set_array();
    jn_push(_h, 0, n);
}

void Json::add_json(const char* key, Json& v) {
    JsonNode* const n = v._h;
    v._h = 0;
    this->set_object();
    jn_push(_h, jn_strdup(key, strlen(key)), n);
}

void Json::erase(uint32 i) {
    if (_h == 0 || (_h->type != t_array && _h->type != t_object) || i >= _h->size) return;
    jn_free(_h->kids[i]);
    if (_h->keys) co::free(_h->keys[i], strlen(_h->keys[i]) + 1);
    const uint32 rest = _h->size - i - 1;
    memmove(_h->kids + i, _h->kids + i + 1, rest * sizeof(JsonNode*));
    if (_h->keys) memmove(_h->keys + i, _h->keys + i + 1, rest * sizeof(char*));
    _h->size--;
}

void Json::erase_key(const char* key) {
    const int64 i = jn_find(_h, key);
    if (i >= 0) this->erase((uint32)i);
}

Json Json::dup() const {
    Json r;
    r._h = jn_dup(_h);
    return r;
}

void Json::reset() {
    jn_free(_h);
    _h = 0;
}

void Json::str_to(fastring* s, int mdp) const {
    jn_write(_h, s, 0, 0, mdp);
}

fastring Json::str() const {
    fastring s(256);
    jn_write(_h, &s, 0, 0, 16);
    return s;
}

void Json::pretty_to(fastring* s, int indent, int mdp) const {
    jn_write(_h, s, indent, 0, mdp);
}

fastring Json::pretty() const {
    fastring s(256);
    jn_write(_h, &s, 4, 0, 16);
    return s;
}

bool Json::parse_from(const char* s, size_t n) {
    this->reset();
    JsonParser P;
    P.p = s;
    P.e = s + n;
    P.depth = 0;
    fastring tmp(64);
    JsonNode* h = 0;
    bool ok = jp_value(&P, &h, &tmp);
    if (ok) {
        jp_ws(&P);
        ok = P.p == P.e;
    }
    if (!ok) {
        jn_free(h);
        return false;
    }
    _h = h;
    return true;
}

Json array() {
    Json r;
    r.set_array();
    return r;
}

Json object() {
    Json r;
    r.set_object();
    return r;
}

Json parse(const char* s, size_t n) {
    Json r;
    r.parse_from(s, n);
    return r;
}

} // json
