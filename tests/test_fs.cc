#include "test.h"
#include "co/fs.h"
#include <stdio.h>
#include <unistd.h>

static void test_fs() {
    char root[64];
    snprintf(root, sizeof(root), "/tmp/coost_test_%d", (int)getpid());
    fs::remove(root, true);

    fastring sub = fastring::from_cstr(root);
    sub.append_cstr("/a/b");
    CHECK(fs::mkdir(sub.c_str(), true));
    CHECK(fs::isdir(sub.c_str()));

    fastring fp = fastring::from_cstr(root);
    fp.append_cstr("/a/x.txt");
    fs::file f(fp.c_str(), 'w');
    CHECK(f.is_open());
    CHECK(f.write_cstr("hello ") == 6);
    f.close();

    fs::fstream s;
    CHECK(s.open(fp.c_str(), 'a'));
    s.append_cstr("world");
    s.close();
    CHECK(fs::fsize(fp.c_str()) == 11);

    fs::file r;
    CHECK(r.open(fp.c_str(), 'r'));
    fastring all = r.read_str(100);
    CHECK_STR(all.c_str(), "hello world");
    r.seek(6, SEEK_SET);
    fastring w = r.read_str(5);
    CHECK_STR(w.c_str(), "world");
    r.close();

    fastring dp = fastring::from_cstr(root);
    dp.append_cstr("/a");
    fs::dir d;
    CHECK(d.open(dp.c_str()));
    co::vector<fastring> names = d.all();
    CHECK(names.size() == 2);            // b and x.txt, in any order

    fastring moved = fastring::from_cstr(root);
    moved.append_cstr("/a/b");
    CHECK(fs::mv(fp.c_str(), moved.c_str()));   // into the directory
    moved.append_cstr("/x.txt");
    CHECK(fs::exists(moved.c_str()));
    CHECK(!fs::exists(fp.c_str()));

    CHECK(fs::remove(root, true));
    CHECK(!fs::exists(root));
}

int main() {
    test_fs();
    return test_report("fs");
}
