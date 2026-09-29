#include "test.h"
#include "co/str.h"
#include "co/path.h"
#include "co/time.h"
#include <errno.h>

static void test_split() {
    co::vector<fastring> v = str::split("|x|y|", 5, '|', 0);
    CHECK(v.size() == 3);
    CHECK_STR(v[0].c_str(), "");
    CHECK_STR(v[1].c_str(), "x");
    CHECK_STR(v[2].c_str(), "y");

    co::vector<fastring> w = str::split("xooy", 4, 'o', 1);
    CHECK(w.size() == 2);
    CHECK_STR(w[1].c_str(), "oy");

    co::vector<fastring> u = str::split_cstr("a::b::c", 7, "::", 0);
    CHECK(u.size() == 3);
    CHECK_STR(u[2].c_str(), "c");

    co::vector<int> n;
    for (int i = 0; i < 100; ++i) n.push_back(i);
    CHECK(n.size() == 100);
    CHECK(n[99] == 99);
    n.pop_back();
    CHECK(n.size() == 99);

    fastring r = str::replace("xooxoox", 7, "oo", "ee", 1);
    CHECK_STR(r.c_str(), "xeexoox");
}

static void test_convert() {
    CHECK(str::to_int64("-42") == -42);
    CHECK(errno == 0);
    CHECK(str::to_int64("0x10") == 16);
    CHECK(str::to_int64("4k") == 4096);
    CHECK(str::to_uint64("1m") == 1048576);
    CHECK(str::to_int32("99999999999") == 0);
    CHECK(errno == ERANGE);
    CHECK(str::to_int64("12x") == 0);
    CHECK(errno == EINVAL);
    CHECK(str::to_bool("true"));
    CHECK(!str::to_bool("0"));
    CHECK(str::to_double("2.5") == 2.5);
}

static fastring clean(const char* s) {
    fastring r = path::clean(s, strlen(s));
    return r;
}

static void test_path() {
    fastring a = clean("");          CHECK_STR(a.c_str(), ".");
    fastring b = clean(".//x/");     CHECK_STR(b.c_str(), "x");
    fastring c = clean("./x/../.."); CHECK_STR(c.c_str(), "..");
    fastring d = clean("/x/../..");  CHECK_STR(d.c_str(), "/");
    fastring e = clean("x//y//z");   CHECK_STR(e.c_str(), "x/y/z");

    fastring j = path::join("/x/", "y");
    CHECK_STR(j.c_str(), "/x/y");

    fastring dir;
    fastring file;
    path::split("/a/b", 4, &dir, &file);
    CHECK_STR(dir.c_str(), "/a/");
    CHECK_STR(file.c_str(), "b");

    fastring pd = path::dir("/a/", 3);   CHECK_STR(pd.c_str(), "/a");
    fastring pb = path::base("/a/b/", 5); CHECK_STR(pb.c_str(), "b");
    fastring pe = path::ext("x/x.c", 5);  CHECK_STR(pe.c_str(), ".c");
}

static void test_time() {
    co::Timer t;
    co::sleep_ms(5);
    CHECK(t.ms() >= 4);
    CHECK(now::ms() > 0);
    CHECK(epoch::ms() > 1600000000000LL);
    fastring y = now::str("%Y");
    CHECK(y.size() == 4);
}

int main() {
    test_split();
    test_convert();
    test_path();
    test_time();
    return test_report("str/path/time");
}
