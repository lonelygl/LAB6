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

## How it works
- **Login/Register** — select or create a database, enter credentials
- **Admin role** — full CRUD, create/drop database, create users
- **Guest role** — view all fights, search by event only
- Access control enforced by PostgreSQL GRANT system (not by hiding menu items)
- All operations go through stored procedures: `sp_add_fight`, `sp_update_fight`, etc.
- Database creation uses `psql` subprocess to run `setup.sql` (handles PL/pgSQL `$$` blocks)

## Default accounts (after creating a DB)
| Username | Password  | Role  |
|----------|-----------|-------|
| admin    | adminpass | admin |
| guest    | guestpass | guest |

## Admin secret code for registration: `admin`
