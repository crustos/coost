#include "co/fs.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

/* The libc calls live outside `namespace fs`: cpprust's namespace
   flattening would otherwise rename `struct stat` to `fs_stat`. */

// 0: missing, 1: file or other, 2: directory, 3: symlink
static int fs_kind(const char* path, int64* size, int64* mtime) {
    struct stat st;
    if (lstat(path, &st) != 0) return 0;
    if (size) *size = (int64)st.st_size;
    if (mtime) *mtime = (int64)st.st_mtime;
    if (S_ISDIR(st.st_mode)) return 2;
    if (S_ISLNK(st.st_mode)) return 3;
    return 1;
}

static bool fs_is_dot(const char* p) {
    return p[0] == '.' && (!p[1] || (p[1] == '.' && !p[2]));
}

static int fs_open_fd(const char* path, char mode) {
    switch (mode) {
      case 'r': return open(path, O_RDONLY);
      case 'a': return open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
      case 'w': return open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
      case 'm': return open(path, O_WRONLY | O_CREAT, 0644);
      case '+': return open(path, O_RDWR | O_CREAT, 0644);
      default: return -1;
    }
}

static size_t fs_read_fd(int fd, void* buf, size_t n) {
    char* c = (char*)buf;
    size_t done = 0;
    while (done < n) {
        const ssize_t r = read(fd, c + done, n - done);
        if (r > 0) {
            done += (size_t)r;
        } else if (r == 0 || errno != EINTR) {
            break;
        }
    }
    return done;
}

static size_t fs_write_fd(int fd, const void* buf, size_t n) {
    const char* c = (const char*)buf;
    size_t done = 0;
    while (done < n) {
        const ssize_t r = write(fd, c + done, n - done);
        if (r >= 0) {
            done += (size_t)r;
        } else if (errno != EINTR) {
            break;
        }
    }
    return done;
}

static void* fs_opendir(const char* path) { return opendir(path); }
static void fs_closedir(void* d) { closedir((DIR*)d); }

// the next entry of @d other than . and .., or NULL
static const char* fs_readdir(void* d) {
    struct dirent* e;
    while ((e = readdir((DIR*)d)) != 0) {
        if (!fs_is_dot(e->d_name)) return e->d_name;
    }
    return 0;
}

// rm -r on @s; @s is used as a scratch buffer and restored
static bool fs_rmdir_r(fastring* s) {
    void* d = fs_opendir(s->c_str());
    if (!d) return errno == ENOENT;

    const size_t n = s->size();
    bool ok = true;
    const char* name = fs_readdir(d);
    while (ok && name) {
        s->resize(n);
        s->append_char('/');
        s->append_cstr(name);
        if (fs_kind(s->c_str(), 0, 0) == 2) {
            ok = fs_rmdir_r(s);
        } else {
            ok = unlink(s->c_str()) == 0 || errno == ENOENT;
        }
        name = fs_readdir(d);
    }
    fs_closedir(d);
    s->resize(n);
    return ok && rmdir(s->c_str()) == 0;
}

static bool fs_mkdir_p(const char* path) {
    if (mkdir(path, 0755) == 0) return true;
    if (errno == EEXIST) return fs_kind(path, 0, 0) == 2;
    if (errno != ENOENT) return false;

    const char* s = strrchr(path, '/');
    if (s == 0 || s == path) return false;
    fastring parent(path, s - path);
    return fs_mkdir_p(parent.c_str()) && mkdir(path, 0755) == 0;
}

namespace fs {

bool exists(const char* path) { return fs_kind(path, 0, 0) != 0; }
bool isdir(const char* path) { return fs_kind(path, 0, 0) == 2; }

int64 mtime(const char* path) {
    int64 t = -1;
    fs_kind(path, 0, &t);
    return t;
}

int64 fsize(const char* path) {
    int64 n = -1;
    fs_kind(path, &n, 0);
    return n;
}

bool mkdir(const char* path, bool parents) {
    if (parents) return fs_mkdir_p(path);
    return ::mkdir(path, 0755) == 0;
}

bool remove(const char* path, bool recursive) {
    const int k = fs_kind(path, 0, 0);
    if (k == 0) return true;
    if (k != 2) return unlink(path) == 0;
    if (!recursive) return rmdir(path) == 0;
    fastring s = fastring::from_cstr(path);
    return fs_rmdir_r(&s);
}

bool mv(const char* from, const char* to) {
    if (fs_kind(to, 0, 0) != 2) return ::rename(from, to) == 0;
    const char* p = strrchr(from, '/');
    fastring s = fastring::from_cstr(to);
    if (!s.ends_with_char('/')) s.append_char('/');
    s.append_cstr(p ? p + 1 : from);
    return ::rename(from, s.c_str()) == 0;
}

bool symlink(const char* dst, const char* lnk) {
    if (fs_kind(lnk, 0, 0) == 3) unlink(lnk);
    return ::symlink(dst, lnk) == 0;
}

bool file::open(const char* path, char mode) {
    this->close();
    if (!path || !*path) return false;
    _path.assign_cstr(path);
    _fd = fs_open_fd(path, mode);
    return _fd >= 0;
}

void file::close() {
    if (_fd >= 0) {
        ::close(_fd);
        _fd = -1;
    }
}

void file::seek(int64 off, int whence) {
    if (_fd >= 0) lseek(_fd, (off_t)off, whence);
}

size_t file::read(void* buf, size_t n) {
    if (_fd < 0) return 0;
    return fs_read_fd(_fd, buf, n);
}

fastring file::read_str(size_t n) {
    fastring s(n + 1);
    s.resize(this->read(s.data(), n));
    return s;
}

size_t file::write(const void* s, size_t n) {
    if (_fd < 0) return 0;
    return fs_write_fd(_fd, s, n);
}

bool dir::open(const char* path) {
    this->close();
    _path.assign_cstr(path);
    _d = fs_opendir(path);
    return _d != 0;
}

void dir::close() {
    if (_d) {
        fs_closedir(_d);
        _d = 0;
    }
}

bool dir::next(fastring* name) {
    if (!_d) return false;
    const char* p = fs_readdir(_d);
    if (!p) return false;
    name->assign_cstr(p);
    return true;
}

co::vector<fastring> dir::all() {
    co::vector<fastring> r;
    fastring name;
    while (this->next(&name)) r.push_back(name);
    return r;
}

} // fs
