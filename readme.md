# coost (crust edition)

A minimal cut of [coost](https://github.com/idealvin/coost), trimmed down to a
small string/stream/JSON core and written in the C++ subset that
[Crust](https://github.com/brentharts/crust) lowers to C
(see [CPPRUST.md](https://github.com/brentharts/crust/blob/master/CPPRUST.md)).

The goals are a small codebase, fast compiles, and output that goes through
`tools/cpprust.py` to plain C. POSIX only; there is no Windows support.

## Modules

| Header | What it provides |
|---|---|
| `co/fastring.h` | heap string with small-buffer tricks and search helpers |
| `co/fastream.h` | append-only byte/text stream |
| `co/fast.h` | fast integer, float and hex to string conversion |
| `co/str.h` | split, strip, replace and string-to-number helpers |
| `co/json.h` | JSON value type, parser and serializer |
| `co/fs.h` | files, directories and path queries |
| `co/path.h` | path manipulation (`clean`, `join`, `split`, ...) |
| `co/time.h` | monotonic clocks, sleep and time formatting |
| `co/hash.h` | murmur, crc16, md5, sha256, base64 and url encoding |
| `co/vector.h` | a simple growable array |
| `co/mem.h` | allocation helpers |
| `co/god.h` | small compile-time helpers |

Everything else from upstream coost is gone: coroutines, networking, RPC,
HTTP, SSL, logging, flags, unit test and benchmark frameworks, threads,
atomics and the task scheduler.

## Subset rules that shape the API

Because the code must lower to C, a few C++ habits are absent:

- No `operator<<` / `>>`; use the named `append_*` methods instead.
- Overloads are chosen by argument **count**, so type-overloaded functions
  have typed names (e.g. `append_int`, `append_str`).
- No exceptions, `std::function`, threads, or variadic class templates.
- No array `new[]` / `delete[]`, reference returns, or multiple inheritance.

## Build

```sh
make            # runs python3 build.py
make clean
```

Requires `python3` and a C compiler.

## License

MIT, as upstream. See [LICENSE.md](LICENSE.md).
Original work copyright (c) 2019-2023 Alvin Yih.
