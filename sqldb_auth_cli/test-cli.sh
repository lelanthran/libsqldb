#!/bin/bash

set -e

rm -fv sqldb_auth.sql3

export PROG=./sqldb_auth_cli/main-d.elf
export VALGRIND="valgrind --error-exitcode=127"
$VALGRIND $VGOPTS $PROG --help
$VALGRIND $VGOPTS $PROG create sqldb_auth.sql3

