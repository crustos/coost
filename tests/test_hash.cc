#include "test.h"
#include "co/hash.h"

static void test_digests() {
    const char* s = "hello world";
    const size_t n = strlen(s);
    fastring m = md5sum(s, n);
    CHECK_STR(m.c_str(), "5eb63bbbe01eeed093cb22bb8f5acdc3");
    fastring h = sha256sum(s, n);
    CHECK_STR(h.c_str(), "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9");
    fastring d = md5digest(s, n);
    CHECK(d.size() == 16);
    CHECK((unsigned char)d[0] == 0x5e);
    fastring e = md5sum("", 0);
    CHECK(e.eq_cstr("d41d8cd98f00b204e9800998ecf8427e"));
    CHECK(crc16("123456789", 9, 0) == 0x31c3);   // CRC-16/XMODEM
    CHECK(hash64(s, n) == murmur_hash64(s, n, 0));
    CHECK(hash32(s, n) == hash32(s, n));
}

static void test_base64() {
    const char* cases[][2] = {
        { "", "" }, { "f", "Zg==" }, { "fo", "Zm8=" }, { "foo", "Zm9v" },
        { "foob", "Zm9vYg==" }, { "fooba", "Zm9vYmE=" }, { "foobar", "Zm9vYmFy" },
    };
    for (int i = 0; i < 7; ++i) {
        fastring e = base64_encode(cases[i][0], strlen(cases[i][0]));
        CHECK_STR(e.c_str(), cases[i][1]);
        fastring d = base64_decode(e.data(), e.size());
        CHECK_STR(d.c_str(), cases[i][0]);
    }
    fastring bad = base64_decode("Zm9$", 4);
    CHECK(bad.empty());
}

static void test_url() {
    const char* s = "a b&c=d/é";
    fastring e = url_encode(s, strlen(s));
    CHECK_STR(e.c_str(), "a%20b&c=d/%C3%A9");
    fastring d = url_decode(e.data(), e.size());
    CHECK_STR(d.c_str(), s);
    fastring bad = url_decode("%zz", 3);
    CHECK(bad.empty());
}

int main() {
    test_digests();
    test_base64();
    test_url();
    return test_report("hash");
}
