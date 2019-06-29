#!/bin/bash

set -e

rm -fv sqldb_auth.sql3

export PROG=./sqldb_auth_cli/main-d.elf
export VALGRIND="valgrind --error-exitcode=127"
if [ -z "$VGOPTS" ]; then
   export VALGRIND=""
fi

$VALGRIND $VGOPTS $PROG --help
$VALGRIND $VGOPTS $PROG create sqldb_auth.sql3
$VALGRIND $VGOPTS $PROG init sqlite sqldb_auth.sql3
