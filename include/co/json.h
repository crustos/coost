#pragma once

// JSON value, parser and serializer.
//
// A Json is a handle owning a tree of nodes. It is move-only; dup() makes
// a deep copy. Lookups return Json* pointing into the tree, so edits
// through them change the tree. Written in the Crust C++ subset:
//
//     Json r = json::object();
//     r.member("name")->set_cstr("coost");
//     Json* v = r.member("list");
//     v->set_array();
//     v->push()->set_int(1);
//     fastring s = r.str();                 // {"name":"coost","list":[1]}
//
//     Json x = json::parse(s.data(), s.size());
//     int64 n = x.get("list")->at(0)->as_int64();
//
// Missing keys and out-of-range indexes give a pointer to a null value,
// so lookups chain safely: x.get("a")->get("b")->is_null().

#include "fastring.h"

// Plain node data; the Json class and json.cc own every allocation.
struct JsonNode {
    uint32 type;
    uint32 size;       // string length, or number of elements
    uint32 cap;        // allocated elements (array / object)
    bool b;
    int64 i;
    double d;
    char* s;           // string
    JsonNode** kids;   // array elements or object values
    char** keys;       // object keys, parallel to kids
};

namespace json {

enum {
    t_null = 0,
    t_bool = 1,
    t_int = 2,
    t_double = 4,
    t_string = 8,
    t_array = 16,
    t_object = 32
};

class Json {
  public:
    Json() { _h = 0; }
    Json(Json&& v) {
        _h = v._h;
        v._h = 0;
    }
    ~Json() { this->reset(); }

    void operator=(Json&& v) {
        if (v._h != _h) {
            this->reset();
            _h = v._h;
            v._h = 0;
        }
    }

    // ---- type ---------------------------------------------------------

    int type() const { return _h ? (int)_h->type : t_null; }
    bool is_null() const { return _h == 0; }
    bool is_bool() const { return _h && _h->type == t_bool; }
    bool is_int() const { return _h && _h->type == t_int; }
    bool is_double() const { return _h && _h->type == t_double; }
    bool is_string() const { return _h && _h->type == t_string; }
    bool is_array() const { return _h && _h->type == t_array; }
    bool is_object() const { return _h && _h->type == t_object; }

    // ---- read (with conversions between bool, int, double, string) ------

    bool as_bool() const;
    int64 as_int64() const;
    int as_int() const { return (int) this->as_int64(); }
    double as_double() const;

    // the string value, or "" for other types
    const char* as_c_str() const { return this->is_string() ? _h->s : ""; }

    // the string value; other types are stringified
    fastring as_string() const;

    // elements of an array or object, length of a string, else 0
    uint32 size() const;
    bool empty() const { return this->size() == 0; }

    // ---- lookup (never NULL; a missing entry reads as null) -------------

    // the @i-th element of an array, or the @i-th value of an object
    Json* at(uint32 i) const;

    // the value for @key in an object (the first, if repeated)
    Json* get(const char* key) const;

    // the @i-th key of an object, or ""
    const char* key_at(uint32 i) const;

    bool has_member(const char* key) const;

    // ---- write ----------------------------------------------------------

    void set_null() { this->reset(); }
    void set_bool(bool v);
    void set_int(int64 v);
    void set_double(double v);
    void set_str(const char* s, size_t n);
    void set_cstr(const char* s) { this->set_str(s, strlen(s)); }

    // become an empty array / object (a no-op if already one)
    void set_array();
    void set_object();

    // append a null element to an array (converting this to one if needed)
    // and return it for filling in
    Json* push();

    // the value for @key, added as null if missing (converting this to an
    // object if needed)
    Json* member(const char* key);

    // append a key, even if already present; returns the new null value
    Json* add_member(const char* key);

    // move @v into the array / under @key; @v is left null
    void push_json(Json& v);
    void add_json(const char* key, Json& v);

    // remove the @i-th element / the member @key (order is kept)
    void erase(uint32 i);
    void erase_key(const char* key);

    // ---- whole value ----------------------------------------------------

    Json dup() const;
    void reset();
    void swap(Json& v) {
        JsonNode* const h = _h;
        _h = v._h;
        v._h = h;
    }

    // minified JSON, doubles with at most @mdp decimal places
    void str_to(fastring* s, int mdp) const;
    fastring str() const;

    // indented JSON
    void pretty_to(fastring* s, int indent, int mdp) const;
    fastring pretty() const;

    // parse, replacing the current value; false (and null) on error
    bool parse_from(const char* s, size_t n);

  private:
    JsonNode* _h;
};

// an empty array / object
Json array();
Json object();

// parse @s; null on error
Json parse(const char* s, size_t n);

} // json

typedef json::Json Json;
