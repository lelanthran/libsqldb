#!/bin/bash

set -e
set -o xtrace

rm -fv sqldb_queue.sql3

# export PROG="test-scripts/sqldb_queue_cli.exe"
export PROG="test-scripts/sqldb_queue_cli.elf"
export VALGRIND="valgrind --error-exitcode=127"
if [ -z "$VGOPTS" ]; then
   export VALGRIND=""
fi

# Ensure that the help message works
$VALGRIND $VGOPTS $PROG --help

# Create a new database for use, initialise it with the queue tables.
echo ".databases" | sqlite3 sqldb_queue.sql3
$VALGRIND $VGOPTS $PROG init --database-type=sqlite --database=sqldb_queue.sql3
# gdb $PROG init sqlite sqldb_queue.sql3

