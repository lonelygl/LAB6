# UFC Fight Database — C++ / libpq

## Requirements
- PostgreSQL 14+
- libpq-dev (C library for PostgreSQL)
- CMake 3.14+
- C++17 compiler

## macOS (Homebrew)
```bash
brew install postgresql libpq cmake
```

## Build
```bash
mkdir build && cd build
cmake ..
make
```

## Run
Place `setup.sql` next to the binary, then:
```bash
./ufc_db
```

## Default accounts (after creating a DB)
| Username | Password  | Role  |
|----------|-----------|-------|
| admin    | adminpass | admin |
| guest    | guestpass | guest |

## Admin secret code for registration: `admin`
