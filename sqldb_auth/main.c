
#include <stdio.h>

#include "sqldb/sqldb.h"
#include "sqldb/sqldb_auth.h"

#define TESTDB_SQLITE    ("/tmp/testdb.sql3")
#define TESTDB_POSTGRES  ("postgresql://lelanthran:a@localhost:5432/lelanthran")
#define TEST_BATCHFILE   ("test-file.sql")

#define PROG_ERR(...)      do {\
      fprintf (stderr, "%s: ", argv[1]);\
      fprintf (stderr, __VA_ARGS__);\
} while (0)

int main (int argc, char **argv)
{
   int ret = EXIT_FAILURE;

   sqldb_dbtype_t dbtype = sqldb_UNKNOWN;
   const char *dbname = NULL;

   sqldb_t *db = NULL;

   printf ("Testing sqldb_auth version [%s]\n", SQLDB_VERSION);
   if (argc <= 1) {
      fprintf (stderr, "Failed to specify one of 'sqlite' or 'postgres'\n");
      return EXIT_FAILURE;
   }

   if ((strcmp (argv[1], "sqlite"))==0) {
      dbtype = sqldb_SQLITE;
      dbname = TESTDB_SQLITE;
   }

   if ((strcmp (argv[1], "postgres"))==0) {
      dbtype = sqldb_POSTGRES;
      dbname = TESTDB_POSTGRES;
   }

   if (!dbname || dbtype==sqldb_UNKNOWN) {
      fprintf (stderr, "Failed to specify one of 'sqlite' or 'postgres'\n");
      return EXIT_FAILURE;
   }


   ret = EXIT_SUCCESS;

errorexit:

   return ret;
}

