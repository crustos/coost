# coost (crust edition)

A minimal cut of [coost](https://github.com/idealvin/coost), trimmed down to a
small string/JSON/filesystem core and written in the C++ subset that
[Crust](https://github.com/brentharts/crust) lowers to C
(see [CPPRUST.md](https://github.com/brentharts/crust/blob/master/CPPRUST.md)).

The goals are a small codebase, fast compiles, and output that goes through
`tools/cpprust.py` to plain C. POSIX only.

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

Everything else from upstream coost is gone: coroutines, networking, RPC,
HTTP, SSL, logging, flags, unit test and benchmark frameworks, threads,
atomics, the custom allocator and the task scheduler.

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

## Build

```sh
make            # lower and compile: build/co.c, build/co.o
make test       # also lower, compile and run tests/*.cc
make cxx        # cross-check: compile the same sources and tests with g++
make clean
ASAN=1 make test
```

Requires `python3`, a C compiler, and a crust checkout at `../crust`
(or set `CRUST=/path/to/crust`).

The whole library is lowered as one translation unit: `build.py` includes
every source into `build/co_all.cc` and runs cpprust once. Programs using
the library are built the same way (see `tests/`).

## License

MIT, as upstream. See [LICENSE.md](LICENSE.md).
Original work copyright (c) 2019-2023 Alvin Yih.
