#include "test.h"
#include "co/fastring.h"

static void test_basic() {
    fastring s;
    CHECK(s.empty());
    CHECK_STR(s.c_str(), "");
    s.append_cstr("hello");
    s.append_char(' ');
    s.append_str(s);                 // self-append
    CHECK_STR(s.c_str(), "hello hello ");
    CHECK(s.size() == 12);

    fastring t = fastring::from_cstr("abc");
    fastring u(t);
    u.append_cstr("def");
    CHECK_STR(t.c_str(), "abc");
    CHECK_STR(u.c_str(), "abcdef");
    t = u;
    CHECK(t.eq_str(u));
    CHECK(t.eq_cstr("abcdef"));
    CHECK(t.compare_cstr("abd") < 0);

    t = t;                           // self-assignment keeps the value
    CHECK_STR(t.c_str(), "abcdef");

    fastring r = fastring::repeat(3, 'x');
    CHECK_STR(r.c_str(), "xxx");
}

static void test_format() {
    fastring s;
    s.append_int(-123);
    s.append_char(',');
    s.append_uint(18446744073709551615ULL);
    s.append_char(',');
    s.append_double(3.14159, 2);
    s.append_char(',');
    s.append_hex(255);
    s.append_char(',');
    s.append_bool(true);
    CHECK_STR(s.c_str(), "-123,18446744073709551615,3.14,0xff,true");

    fastring m;
    m.append_int(-9223372036854775807LL - 1);
    CHECK_STR(m.c_str(), "-9223372036854775808");
}

static void test_find() {
    fastring s = fastring::from_cstr("hello world, hello");
    CHECK(s.find_char('o') == 4);
    CHECK(s.find_char_from('o', 5) == 7);
    CHECK(s.find_cstr("hello") == 0);
    CHECK(s.find_cstr_from("hello", 1) == 13);
    CHECK(s.rfind_cstr("hello") == 13);
    CHECK(s.rfind_char('l') == 16);
    CHECK(s.ifind_cstr("WORLD", 0) == 6);
    CHECK(s.find_cstr("xyz") == fastring::npos);
    CHECK(s.find_first_of(",w", 0) == 6);
    CHECK(s.find_last_not_of("ol", fastring::npos) == 14);
    CHECK(s.starts_with_cstr("hell"));
    CHECK(s.ends_with_cstr("llo"));
    CHECK(s.contains_cstr("world"));
    CHECK(s.match("hello*hel?o"));
    CHECK(!s.match("hello*x"));
}

static void test_modify() {
    fastring s = fastring::from_cstr("  a-b-c  ");
    s.trim();
    CHECK_STR(s.c_str(), "a-b-c");
    s.replace_cstr("-", "+", 0);
    CHECK_STR(s.c_str(), "a+b+c");
    s.replace_cstr("+", "==", 1);
    CHECK_STR(s.c_str(), "a==b+c");
    fastring u = s.upper();
    CHECK_STR(u.c_str(), "A==B+C");
    fastring sub = s.substr(1, 2);
    CHECK_STR(sub.c_str(), "==");
    fastring tail = s.substr(4);
    CHECK_STR(tail.c_str(), "+c");
    s.remove_prefix_cstr("a==");
    CHECK_STR(s.c_str(), "b+c");
    s.trim_char('c', 'r');
    CHECK_STR(s.c_str(), "b+");
    s.resize(5, '.');
    CHECK_STR(s.c_str(), "b+...");
}

int main() {
    test_basic();
    test_format();
    test_find();
    test_modify();
    return test_report("fastring");
}
