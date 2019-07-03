@echo off

echo NOTES: The following must be done or else the tests will fail
echo 1. Start postres server, make sure that the connection string
echo        postgresql://lelanthran:a@localhost:5432/lelanthran
echo    works.
echo 2. Make sure that psql.exe is in the PATH, and connects
echo    with the above credentials to the above database.
echo 3. Make sure that the libpq.dll file is in the PATH.
@echo

echo Removing sqlite test database...
del /f \tmp\testdb.sql3
echo Done.

echo Dropping all relevant test tables in default postgres db
echo drop table one cascade;   | psql lelanthran lelanthran
echo drop table two cascade;   | psql lelanthran lelanthran
echo drop table three cascade; | psql lelanthran lelanthran
echo Done.

echo Starting sqlite tests
set ERRMSG="SQLite test failed"
sqldb\main-d.exe sqlite   || goto errorexit
echo Sqlite tests passed.

echo Starting postgres tests
set ERRMSG="PSQL test failed"
sqldb\main-d.exe postgres || goto errorexit
echo Postgres tests passed.

goto end

:errorexit
echo Error: %ERRMSG%

:end
