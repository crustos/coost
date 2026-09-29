#pragma once

#include "def.h"
#include "fastring.h"
#include "vector.h"

namespace fs {

bool exists(const char* path);
bool isdir(const char* path);

// modification time (seconds since epoch), or -1
int64 mtime(const char* path);

// file size, or -1
int64 fsize(const char* path);

// create a directory; @parents: also create missing parents (mkdir -p)
bool mkdir(const char* path, bool parents);

// remove a file or empty directory; @recursive: rm -r
bool remove(const char* path, bool recursive);

// rename or move; moving onto an existing directory moves into it
bool mv(const char* from, const char* to);

// create symlink @lnk pointing to @dst, replacing an existing symlink
bool symlink(const char* dst, const char* lnk);

// open modes:
//   'r': read          fails if missing
//   'a': append        created if missing
//   'w': write         created if missing, truncated if exists
//   'm': modify        like 'w', but not truncated
//   '+': read/write    created if missing
class file {
  public:
    file() { _fd = -1; }
    file(const char* path, char mode) {
        _fd = -1;
        this->open(path, mode);
    }
    ~file() { this->close(); }

    bool open(const char* path, char mode);
    void close();
    bool is_open() const { return _fd >= 0; }
    const char* path() const { return _path.c_str(); }
    int64 size() const { return fs::fsize(_path.c_str()); }

    // @whence: SEEK_SET, SEEK_CUR or SEEK_END
    void seek(int64 off, int whence);

    // read up to @n bytes; returns the number read (0 at end of file)
    size_t read(void* buf, size_t n);

    // read up to @n bytes into a string
    fastring read_str(size_t n);

    // returns the number of bytes written
    size_t write(const void* s, size_t n);
    size_t write_cstr(const char* s) { return this->write(s, strlen(s)); }
    size_t write_str(const fastring& s) { return this->write(s.data(), s.size()); }

  private:
    int _fd;
    fastring _path;
};

// Buffered writer. Mode is 'w' (truncate) or 'a' (append).
class fstream {
  public:
    fstream() { _s.reserve(8192); }
    fstream(size_t cap) { _s.reserve(cap); }
    ~fstream() { this->close(); }

    bool open(const char* path, char mode) {
        this->close();
        return _f.open(path, mode == 'w' ? 'w' : 'a');
    }

    bool is_open() const { return _f.is_open(); }

    void flush() {
        if (!_s.empty()) {
            _f.write(_s.data(), _s.size());
            _s.clear();
        }
    }

    void close() {
        this->flush();
        _f.close();
    }

    // small writes are buffered; one larger than the buffer goes straight out
    void append(const void* s, size_t n) {
        if (_s.capacity() < _s.size() + n + 1) this->flush();
        if (n + 1 <= _s.capacity()) {
            _s.append(s, n);
        } else {
            _f.write(s, n);
        }
    }

    void append_cstr(const char* s) { this->append(s, strlen(s)); }
    void append_str(const fastring& s) { this->append(s.data(), s.size()); }

  private:
    fastring _s;
    fs::file _f;
};

// Directory listing; "." and ".." are skipped.
class dir {
  public:
    dir() { _d = 0; }
    ~dir() { this->close(); }

    bool open(const char* path);
    void close();
    const char* path() const { return _path.c_str(); }

    // the next entry name into @name; false when there are no more
    bool next(fastring* name);

    // all remaining entries
    co::vector<fastring> all();

  private:
    void* _d;
    fastring _path;
};

} // fs
