# lib_filesystem

Paths, directories, metadata, traversal, and filesystems for the p101 portable systems subset.

The public API is the intersection implemented on Linux, macOS, and FreeBSD.
POSIX, XSI, optional-POSIX, and common-Unix origins are recorded in
`api-manifest.tsv`; provenance does not determine ownership.

The maintained implementation is deliberately one public header and one source
file. Standards origin is not part of the source-tree architecture.

## Build and verification

```sh
./change-compiler.sh -c clang
./check.sh
```

Every public wrapper is listed in `test/unit-test-manifest.tsv` and
invoked by either a deterministic injected-failure test or a handwritten
behavior test.

Instrumentation sees only calls routed through `p101_*` wrappers. It does
not observe direct libc calls or work performed inside third-party code.
