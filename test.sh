#!/bin/bash
#
# NOTE: A different test script must be written for Windows.
#

export SQLITE_TEST="SQLite test passed"
export PSQL_TEST="PSQL test passed"

echo "Set environment variable VGRIND to valgring with args to run valgrind"

echo Removing sqlite test database...
rm -rf /tmp/testdb.sql3
echo Done.

echo Dropping all relevant test tables in default postgres db
echo "drop table one cascade;"   | psql
echo "drop table two cascade;"   | psql
echo "drop table three cascade;" | psql
echo Done.

$VGRIND sqldb/main-d.elf sqlite     || export SQLITE_TEST="SQLITE test failed"
$VGRIND sqldb/main-d.elf postgres   || export PSQL_TEST="PSQL test failed"

