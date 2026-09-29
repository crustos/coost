#pragma once

#include "def.h"
#include "fastring.h"

namespace now {

// monotonic timestamps
int64 ns();
int64 us();
int64 ms();

// local time formatted by strftime, e.g. "%Y-%m-%d %H:%M:%S"
fastring str(const char* fm);

} // now

namespace epoch {

// time since the unix epoch
int64 us();
int64 ms();

} // epoch

namespace co {

// sleep for @n milliseconds / seconds (not `sleep::ms`: a namespace named
// sleep collides with the libc function in C++)
void sleep_ms(uint32 n);
void sleep_sec(uint32 n);

class Timer {
  public:
    Timer() { _start = now::ns(); }
    void restart() { _start = now::ns(); }
    int64 ns() const { return now::ns() - _start; }
    int64 us() const { return this->ns() / 1000; }
    int64 ms() const { return this->ns() / 1000000; }

  private:
    int64 _start;
};

} // co
