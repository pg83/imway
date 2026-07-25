# Integration tests

`./build test` builds the dedicated `imway_test` compositor and runs every
`headless_*.sh` scenario in a fresh runtime directory. The default is three
runs per scenario; use `-Druns=1` for a quick pass or
`-Dfilter='headless_kms_*'` to select a group.

The cache is content-addressed, so instrumented and regular objects remain
distinct even in one build directory. The commands below use named directories
only to keep their outputs and cleanup separate. They require a libc and
compiler runtime that provide the requested sanitizer; the normal musl
environment does not.

```sh
ASAN_OPTIONS='detect_leaks=1:abort_on_error=1' \
UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1' \
./build -B .build-asan-ubsan -Dsanitizers=address,undefined test

TSAN_OPTIONS='halt_on_error=1:second_deadlock_stack=1' \
./build -B .build-tsan -Dsanitizers=thread test
```

The checked-out `third_party/libstd` is consumed as the installed `-lstd`
archive, so it remains outside the instrumentation boundary.
