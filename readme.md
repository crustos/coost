# coost (crust edition)

A minimal cut of [coost](https://github.com/idealvin/coost), trimmed down to a
small string/JSON/filesystem core and written in the C++ subset that
[Crust](https://github.com/brentharts/crust) lowers to C
(see [CPPRUST.md](https://github.com/brentharts/crust/blob/master/CPPRUST.md)).

The goals are a small codebase, fast compiles, and output that goes through
`tools/cpprust.py` to plain C. POSIX only.

Code that needs no C++ features is written as plain C under `src/c/` and
wrapped by thin C++ classes; see [C core](#c-core).

## Modules

| Header | What it provides |
|---|---|
| `co/fastring.h` | growable string that doubles as an output stream (`fastream` is an alias) |
| `co/fast.h` | integer, hex and double to string conversion |
| `co/str.h` | split, replace, string-to-number |
| `co/json.h` | JSON value, parser and serializer |
| `co/fs.h` | files, buffered writer, directory listing, path queries |
| `co/path.h` | path manipulation (`clean`, `join`, `split`, `dir`, `base`, `ext`) |
| `co/time.h` | monotonic and epoch clocks, sleep, time formatting, `co::Timer` |
| `co/hash.h` | murmur, crc16, md5, sha256, base64, url encoding |
| `co/vector.h` | a minimal growable array |
| `co/mem.h` | allocation wrappers |
| `co/tcp.h` | TCP client and server sockets, blocking with timeouts |
| `co/http.h` | HTTP/1.1 client, and a server for many connections on one thread (plain HTTP, no TLS) |

Everything else from upstream coost is gone: coroutines, RPC, SSL, logging,
flags, unit test and benchmark frameworks, threads, atomics, the custom
allocator and the task scheduler. TCP and HTTP are back, rewritten without
coroutines (see [Networking](#networking)).

## API conventions

These follow from the subset rules and hold across the library:

- **Overloads differ by argument count only**, so type variants carry the
  type in their name: `append_cstr`, `append_int`, `append_double`,
  `find_char`, `find_cstr`, `md5sum` / `md5sum_to`.
- **No `operator<<`**; use the named `append_*` methods.
- **No reference returns** (except `operator[]`). Appends return `void`;
  JSON lookups return `Json*`.
- **No default arguments**; every parameter is passed explicitly.
- **No exceptions**. Conversions report through `errno`; parsers return `bool`.

```cpp
fastring s = fastring::from_cstr("n=");
s.append_int(42);                         // "n=42"

Json r = json::object();
r.member("name")->set_cstr("coost");
r.member("list")->push()->set_int(1);
fastring out = r.str();                   // {"name":"coost","list":[1]}
```

## Networking

Sockets block with timeouts instead of yielding to a scheduler. Every
timeout is in milliseconds, a negative one waits forever, and a timeout fails
the call with `errno == ETIMEDOUT`. IPv4 and IPv6; SIGPIPE is never raised.

```cpp
static void handle(const http::Req* req, http::Res* res, void* ud) {
    fastring path = req->path();
    if (path.eq_cstr("/hello")) {
        res->add_header("Content-Type", "text/plain");
        res->set_body_cstr("hello");
    } else {
        res->set_status(404);
    }
}

http::Server srv;
srv.on_req(handle, 0);                    // a function and a context pointer
srv.start("0.0.0.0", 8080);
srv.run();                                // until a handler calls srv.stop()

http::Client cli;
cli.open("http://127.0.0.1:8080");
if (cli.get("/hello")) printf("%d %s\n", cli.status(), cli.body());
else printf("failed: %s\n", cli.error());
```

The server is a single-threaded `poll()` loop (`co/c/http_server.h`) that
serves many connections at once, without threads or coroutines. An idle,
slow or non-reading client costs a pollfd, not the server's attention.
Handlers run on the loop, so they must not block. `run()` loops until a
handler calls `stop()`; `step(ms)` runs one round, for embedding the server
in a loop of your own.

- Keep-alive and pipelining; responses stay in order.
- `set_timeout(ms)` (default 5 s) bounds idling between requests, receiving a
  request from its first byte (so a client trickling bytes is cut off), and
  a stalled write.
- `set_max_conns(n)` (default 1024); further clients wait in the backlog.
- Malformed requests get 400, bodies over `set_max_body` 413, oversized heads
  431; a request carrying both `Content-Length` and `Transfer-Encoding` is
  rejected.
- While a response is unsent, that connection is not read, so a client that
  pipelines without reading can't make the server buffer without limit.
- `stop()` answers the current request with `Connection: close`, then closes
  each connection once its response is written. Requests still arriving are
  dropped.

The client keeps the connection alive and reconnects once if the server has
dropped it; it reads `Content-Length`, chunked and close-delimited bodies and
skips `100 Continue`.

Not yet: TLS (`https://` is refused), and more than one thread.

## C core

`src/c/*.c` is plain C11 that the C compiler builds directly: it never goes
through cpprust, is compiled once into `build/c/`, and is only rebuilt when
it or `include/co/c/*.h` changes. Lowering is the slow step of the build, so
anything that is bytes and syscalls belongs here, and the C++ above it only
owns resources and presents them. Currently: `c/sock.h` (sockets),
`c/http.h` (a resumable HTTP parser, chunked decoder and message I/O) and
`c/http_server.h` (the event loop).

Rules for a C header under `include/co/c/`:

- Prefix every name with `co_`; C has one namespace.
- Wrap declarations in `#ifdef __cplusplus extern "C" { ... }`. cpprust
  drops the guard; `make cxx` needs it to link.
- Plain structs are fine to hold by value in a C++ class (`co_http_msg`).
- A C typedef is fine in a free function's signature, but not in a method's:
  cpprust hoists method prototypes above the header. Take `void*` in the
  method and cast in the body (see `http::Server::_dispatch`).
- A function-pointer typedef has the same problem. Write the pointer type
  out, and access a function-pointer field as `this->_fn`, which the lowering
  otherwise misses.

## Build

```sh
make            # lower and compile: build/co.c, build/co.o
make test       # also lower, compile and run tests/*.cc
make cxx        # cross-check: compile the same sources and tests with g++
make clean
ASAN=1 make test
```

Requires `python3`, a C compiler, `ar`, and a crust checkout at `../crust`
(or set `CRUST=/path/to/crust`). `make` produces `build/libco.a` (`build/asan/libco.a` under `ASAN=1`), holding
the lowered library and the C core.

The whole library is lowered as one translation unit: `build.py` includes
every source into `build/co_all.cc` and runs cpprust once. Programs using
the library are built the same way and link the C objects (see `tests/`).

Lowering is the slow step, so the build avoids repeating it:

- Each test is lowered with only the library sources it reaches (`src/X.cc`
  joins when `co/X.h` is in its include closure), so `test_tcp` lowers 3 of
  the 15 sources. The full library is still lowered by every `make test`.
- A lowering is skipped when nothing it splices has changed: the key hashes
  the include closure, `cpprust.py` and the flags. Editing `src/json.cc`
  re-lowers the library and `test_json`, nothing else.
- Independent lowerings run in parallel (`JOBS`, default the CPU count).

On one core: cold `make test` 16 s (was 39 s), nothing changed 2.4 s (was
38 s), one test edited 4 s.

### Crust version

Needs a crust with the fixes for three silent miscompiles found while
writing the networking code (`tests/test_cpprust_coost_fixes.py` in crust):

- a leading `::` was stripped inside string literals, so `"::1"` lowered to
  `"1"`;
- `if (c) return T(..);` without braces guarded only the first of the
  statements the return expanded to;
- a stack array of class objects (`tcp::Conn c[50];`) was never constructed
  or destroyed, and assigning to an element was a struct copy.

An older crust fails `make test` on all three.

## License

MIT, as upstream. See [LICENSE.md](LICENSE.md).
Original work copyright (c) 2019-2023 Alvin Yih.
