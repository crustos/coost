#include "test.h"
#include "co/json.h"

static void test_build() {
    Json r = json::object();
    r.member("name")->set_cstr("coost");
    r.member("n")->set_int(-42);
    r.member("pi")->set_double(3.25);
    r.member("ok")->set_bool(true);
    r.member("nothing")->set_null();
    Json* list = r.member("list");
    list->set_array();
    list->push()->set_int(1);
    list->push()->set_cstr("two");
    list->push()->set_object();
    list->at(2)->member("k")->set_int(3);

    fastring s = r.str();
    CHECK_STR(s.c_str(),
        "{\"name\":\"coost\",\"n\":-42,\"pi\":3.25,\"ok\":true,\"nothing\":null,"
        "\"list\":[1,\"two\",{\"k\":3}]}");
    CHECK(r.size() == 6);

    r.member("n")->set_int(7);               // replace, not append
    CHECK(r.size() == 6);
    CHECK(r.get("n")->as_int64() == 7);

    r.erase_key("nothing");
    CHECK(!r.has_member("nothing"));
    CHECK_STR(r.key_at(4), "list");

    fastring p = list->pretty();
    CHECK_STR(p.c_str(), "[\n    1,\n    \"two\",\n    {\n        \"k\": 3\n    }\n]");

    Json e = json::array();
    fastring es = e.str();
    CHECK_STR(es.c_str(), "[]");
    Json n;
    fastring ns = n.str();
    CHECK_STR(ns.c_str(), "null");
}

static void test_parse() {
    const char* s = " {\"a\": [1, 2.5, -3e2, true, false, null, \"x\\ty\"],"
                    " \"b\": {\"c\": \"\\u00e9\\ud83d\\ude00\"}, \"big\": 18446744073709551616} ";
    Json x = json::parse(s, strlen(s));
    CHECK(x.is_object());
    Json* a = x.get("a");
    CHECK(a->is_array());
    CHECK(a->size() == 7);
    CHECK(a->at(0)->is_int() && a->at(0)->as_int() == 1);
    CHECK(a->at(1)->is_double() && a->at(1)->as_double() == 2.5);
    CHECK(a->at(2)->as_double() == -300);
    CHECK(a->at(3)->as_bool());
    CHECK(!a->at(4)->as_bool());
    CHECK(a->at(5)->is_null());
    CHECK_STR(a->at(6)->as_c_str(), "x\ty");
    CHECK_STR(x.get("b")->get("c")->as_c_str(), "\xc3\xa9\xf0\x9f\x98\x80");
    CHECK(x.get("big")->is_double());         // overflows int64

    // missing entries read as null and chain safely
    CHECK(x.get("zz")->get("y")->at(3)->is_null());
    CHECK(a->at(99)->is_null());

    // round trip
    fastring out = x.str();
    Json y = json::parse(out.data(), out.size());
    fastring out2 = y.str();
    CHECK_STR(out.c_str(), out2.c_str());

    // conversions
    Json z = json::parse("\"123\"", 5);
    CHECK(z.as_int64() == 123);
    fastring zs = z.as_string();
    CHECK_STR(zs.c_str(), "123");
}

static void test_errors() {
    const char* bad[] = {
        "", "{", "[1,]", "{\"a\" 1}", "{\"a\":}", "[1 2]", "tru", "nul",
        "\"abc", "\"\\x\"", "01x", "-", "1.", "[1]]", "{\"a\":1,}", "\"\\ud800\"",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        Json v;
        const bool ok = v.parse_from(bad[i], strlen(bad[i]));
        CHECK(!ok);
        CHECK(v.is_null());
    }
    // deep nesting is refused rather than overflowing the stack
    fastring deep = fastring::repeat(10000, '[');
    Json d;
    CHECK(!d.parse_from(deep.data(), deep.size()));
}

static void test_ownership() {
    Json a = json::array();
    Json item = json::object();
    item.member("v")->set_int(1);
    a.push_json(item);
    CHECK(item.is_null());                    // moved in
    CHECK(a.at(0)->get("v")->as_int() == 1);

    Json c = a.dup();
    c.at(0)->member("v")->set_int(2);
    CHECK(a.at(0)->get("v")->as_int() == 1);  // deep copy
    CHECK(c.at(0)->get("v")->as_int() == 2);

    Json m = json::parse("[1,2,3]", 7);
    m.erase(0);
    fastring ms = m.str();
    CHECK_STR(ms.c_str(), "[2,3]");

    Json s;
    s.set_cstr("abc");
    s.set_str(s.as_c_str() + 1, 2);           // from its own buffer
    CHECK_STR(s.as_c_str(), "bc");

    Json q;
    fastring big(0);
    for (int i = 0; i < 1000; ++i) q.push()->set_int(i);
    q.str_to(&big, 16);
    Json q2 = json::parse(big.data(), big.size());
    CHECK(q2.size() == 1000);
    CHECK(q2.at(999)->as_int() == 999);

    // string escaping round trip
    Json e;
    e.set_str("a\"b\\c\n\x01", 7);
    fastring es = e.str();
    CHECK_STR(es.c_str(), "\"a\\\"b\\\\c\\n\\u0001\"");
}

int main() {
    test_build();
    test_parse();
    test_errors();
    test_ownership();
    return test_report("json");
}
