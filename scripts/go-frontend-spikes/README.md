# Go front-end parsing/type-checking spikes

Throw-away spikes used to choose between an external and a native parser
and type checker for the CBMC Go front-end. The decision and the data are
recorded in `doc/architectural/go-frontend-plan.md` §4. They are kept here
so the measurements can be reproduced.

All commands assume `go1.27.3` on `PATH`. The toolchain on the development
host is in `~/go-toolchain/go/bin`.

## corpus/

`features.go` exercises what a front-end must resolve: generics with
inference, exact untyped constants, embedded-method promotion, type
switches, closures, map ranges, wrap-around overflow, and stdlib imports.

```sh
cd corpus && go run features.go   # 6 4 {k 4} id=7 7 6 0 OK MyInt id=7 0.333…
```

## external/ — spike A (`go/parser` + `go/types` → typed JSON)

```sh
cd external && go build -o /tmp/go2json .
cd ../corpus && /tmp/go2json . > /tmp/features.json            # gc importer
/tmp/go2json -importer source . > /tmp/features.json           # source importer
```

Measured on go1.27.3 (linux/amd64): 0.18 s per run with a warm build cache
(4.5 s once, cold), or 0.53 s with the source importer. The JSON is 30 KB
for the corpus.

## native/ — spike B (C++ lexer and recursive-descent parser)

```sh
cd native && g++ -std=c++17 -O2 -Wall -Wextra -o /tmp/goparse \
  main.cpp go_lexer.cpp go_parser.cpp
find "$(go env GOROOT)/src" -name '*.go' -not -path '*/testdata/*' \
  > /tmp/stdlib_files.txt
/tmp/goparse < /tmp/stdlib_files.txt > /dev/null   # ok=6827 fail=0, ~6 s
```

## oracle/ — structural check of spike B against go/parser

```sh
cd oracle && go build -o /tmp/go_oracle .
/tmp/go_oracle < /tmp/stdlib_files.txt > /tmp/oracle_counts.txt
/tmp/goparse --counts < /tmp/stdlib_files.txt > /tmp/native_counts.txt
# Compare per-file counts of 18 node kinds: 0 mismatching files.
```
