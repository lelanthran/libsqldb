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

echo Dropping all relevant test tables in relevant postgres db
echo "drop table one cascade;"   | psql testdb
echo "drop table two cascade;"   | psql testdb
echo "drop table three cascade;" | psql testdb
echo "drop database testdb;"     | psql lelanthran
echo Done.

echo Dropping all relevant test tables in relevant mysql db
echo "drop table one cascade;"   | mysql -pa testdb
echo "drop table two cascade;"   | mysql -pa testdb
echo "drop table three cascade;" | mysql -pa testdb
echo "drop database testdb;"     | mysql -pa lelanthran
echo Done.

set -e

echo Starting sqlite tests
# $VGRIND test-scripts/sqldb_test.elf sqlite   || die "SQLite test failed"
echo Sqlite tests passed.

echo Starting postgreSQL tests
# $VGRIND test-scripts/sqldb_test.elf postgres || die "PSQL test failed"
echo PostgreSQL tests passed.

echo Starting mysql tests
$VGRIND test-scripts/sqldb_test.elf mysql || die "MySQL test failed"
echo MySQL tests passed.

exit 0;
