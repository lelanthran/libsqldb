# libsqldb - C library wrapper to SQL backends


## Warning
`[libsqldb is intended to be a x-platform library for SQL backends]`

This is a work in progress; you probably do not want to use this. If you
are going to use this, use the master branch as the most recent stable
version.


## Overview
This is a library with a `C` interface that acts as a single-interface
wrapper to various database backends. As of writing, only _SQLite_ and
_PostgreSQL_ are supported. Both backends are included unconditionally in
the build process and cannot be excluded at build.

This means that `libpq` is both a build dependency and a runtime
dependency, even if it is never used. The _SQLite_ interface is statically
linked and has no runtime dependencies.

Included is a generic library and command-line tool for managing user,
groups and permissions. A caller who needs to create and manage users,
groups and their permissions can use the function calls `sqldb_auth_*`
to:
1. Add users.
2. Set and/or reset users' passwords.
3. Assign users to groups.
4. Set the permissions for both users and groups.
5. Check permissions for users.
7. Set and test caller-defined flags for each user.

The users, groups and permissions can be managed with the `sqldb_auth_cli`
tool that is created during the build process. All the `sqldb_auth_*`
functions are documented in the `sqldb_auth.h` header.


## Dependencies
### Building
The `libpq` library and associated headers.
### Running
The `libpq.so` (on POSIX) or `libpq.dll` (on Windows) must be in the
correct path at runtime.


## Building
Run `make debug` or `make release` depending on which binaries you want.
You can change the compiler to target a different platform by setting the
environment variable `GCC` and `GXX` to point to the gcc binaries for your
particular platform.

At this point only `gcc` and `clang` are supported compilers.


## Binary packages
I haven't created binary packages yet. A major stable release will have
binary packages when I get to a major, stable point.
