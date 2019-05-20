#!/bin/bash
#
# NOTE: A different test script must be written for Windows.
#

function die () {
   echo $*
   exit 127
}

echo "Set environment variable VGRIND to valgrind with args to run valgrind"

echo Removing sqlite test database...
rm -rf /tmp/testdb.sql3
echo Done.

echo Dropping all relevant test tables in default postgres db
echo "drop table one cascade;"   | psql
echo "drop table two cascade;"   | psql
echo "drop table three cascade;" | psql
echo Done.

echo Starting sqlite tests
$VGRIND sqldb/main-d.elf sqlite   || die "SQLITE test failed"
echo Sqlite tests passed.

echo Starting postgres tests
$VGRIND sqldb/main-d.elf postgres || die "PSQL test failed"
echo Postgres tests passed.

exit 0;
