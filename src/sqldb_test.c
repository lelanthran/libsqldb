
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>

#include "sqldb.h"

// This database will be created!
#define TESTDB_SQLITE    ("/tmp/testdb.sql3")

// This database must exist!
#define EXISTDB_POSTGRES  ("postgresql://lelanthran:a@localhost:5432/lelanthran")
// This database will be created!
#define TESTDB_POSTGRES  ("postgresql://lelanthran:a@localhost:5432/testdb")

// This database must exist!
#define EXISTDB_MYSQL  ("mysql://lelanthran:a@localhost:3306/lelanthran")
// This database will be created!
#define TESTDB_MYSQL  ("mysql://lelanthran:a@localhost:3306/testdb")


#define TEST_BATCHFILE   ("test-scripts/test-file.sql")

#define PROG_ERR(...)      do {\
      fprintf (stderr, "%s:%i:[%s]: ", __FILE__, __LINE__, argv[1]);\
      fprintf (stderr, __VA_ARGS__);\
} while (0)

int main (int argc, char **argv)
{
   static const char *create_stmts[] = {
"create table one (col_a int primary key, col_b varchar (20));",
"create table two (col_1 int primary key, col_2 int references one (col_a));",
"insert into one values (41, '42');",
"insert into two values (51, 41);",
"create table three (col_a int primary key, col_b timestamp);",
"insert into three values (99, CURRENT_TIMESTAMP);",
NULL,
   };

   sqldb_dbtype_t dbtype = sqldb_UNKNOWN;
   const char *dbname = NULL;
   FILE *inf = NULL;

   int ret = EXIT_FAILURE;
   sqldb_t *db = NULL;
   sqldb_res_t *res = NULL;

   char **colnames = NULL;

   printf ("Testing sqldb version [%s]\n", SQLDB_VERSION);
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
      dbname = EXISTDB_POSTGRES;
   }

   if ((strcmp (argv[1], "mysql"))==0) {
      dbtype = sqldb_MYSQL;
      dbname = EXISTDB_MYSQL;
   }

   if (!dbname || dbtype==sqldb_UNKNOWN) {
      fprintf (stderr, "Failed to specify one of 'sqlite' or 'postgres'\n");
      return EXIT_FAILURE;
   }

   if (dbtype==sqldb_POSTGRES || dbtype==sqldb_MYSQL) {
      db = sqldb_open (dbname, dbtype);
      if (!db) {
         PROG_ERR ("Unable to open database - %s\n", sqldb_lasterr (db));
         goto errorexit;
      }
      dbname = "testdb";
   }

   if (!(sqldb_create (db, dbname, dbtype))) {
      PROG_ERR ("(%s) Could not create database\n", dbname);
      PROG_ERR ("Last error that occurred: [%s]\n", sqldb_lasterr (db));
      goto errorexit;
   }

   sqldb_close (db);
   db = NULL;

   if (dbtype==sqldb_POSTGRES) {
      dbname = TESTDB_POSTGRES;
   }

   if (dbtype==sqldb_MYSQL) {
      dbname = TESTDB_MYSQL;
   }

   db = sqldb_open (dbname, dbtype);
   if (!db) {
      PROG_ERR ("Unable to open database [%s] - %s\n", dbname, sqldb_lasterr (db));
      goto errorexit;
   }

   for (size_t i=0; create_stmts[i]; i++) {
      sqldb_res_t *r = sqldb_exec (db, create_stmts[i], sqldb_col_UNKNOWN);
      if (!r) {
         PROG_ERR ("(%s) Error during _exec [%s]\n", sqldb_lasterr (db),
                                                     create_stmts[i]);
         goto errorexit;
      }
      switch (sqldb_res_step (r)) {
         case 1:  PROG_ERR ("Results available\n");                    break;
         case 0:  PROG_ERR ("Execution complete\n");                   break;
         case -1: PROG_ERR ("S Error (%s)\n", sqldb_res_lasterr (r));  break;
      }
      sqldb_res_del (r);
   }

   res = sqldb_exec (db, "BEGIN TRANSACTION;", sqldb_col_UNKNOWN);
   if (!res) {
      PROG_ERR ("(%s) Error during _exec []\n", sqldb_lasterr (db));
      goto errorexit;
   }
   if (sqldb_res_step (res)!=0) {
      PROG_ERR ("(%s) Error during _step []\n", sqldb_lasterr (db));
      goto errorexit;
   }

   sqldb_res_del (res); res = NULL;

   for (uint32_t i=0; i<25; i++) {
      uint32_t u32 = i + 100;
      const char *string = "Testing";

      res = sqldb_exec (db, "insert into one values (#1, #2)",
                              sqldb_col_UINT32, &u32,
                              sqldb_col_TEXT,   &string,
                              sqldb_col_UNKNOWN);
      if (!res) {
         PROG_ERR ("%u(%s) Error during _exec []\n", i, sqldb_lasterr (db));
         goto errorexit;
      }
      if (sqldb_res_step (res)!=0) {
         PROG_ERR ("%u(%s) Error during _step []\n", i, sqldb_lasterr (db));
         goto errorexit;
      }
      PROG_ERR ("Inserted [%s] into values\n", string);
      sqldb_res_del (res); res = NULL;
   }

   res = sqldb_exec (db, "COMMIT;", sqldb_col_UNKNOWN);
   if (!res) {
      PROG_ERR ("(%s) Error during commit []\n", sqldb_lasterr (db));
      goto errorexit;
   }
   sqldb_res_del (res); res = NULL;

   /*
   res = sqldb_exec (db, "ROLLBACK;", sqldb_col_UNKNOWN);
   if (!res) {
      PROG_ERR ("(%s) Error during _exec []\n", sqldb_lasterr (db));
      goto errorexit;
   }
   int rc = sqldb_res_step (res);
   {
      PROG_ERR ("(%s) Error during _step [%i]\n", sqldb_lasterr (db), rc);
      // goto errorexit;
   }

   sqldb_res_del (res); res = NULL;
   */

   PROG_ERR ("Completed parameterised insertion\n");

   if (!(inf = fopen (TEST_BATCHFILE, "r"))) {
      PROG_ERR ("Cannot open batchfile [%s] for reading: %m\n",
                  TEST_BATCHFILE);
      goto errorexit;
   }

   if (!(sqldb_batchfile (db, inf))) {
      PROG_ERR ("Batchfile [%s] failed with error [%s]\n",
                  TEST_BATCHFILE, sqldb_lasterr (db));
      goto errorexit;
   }

   res = sqldb_exec (db, "select * from one;", sqldb_col_UNKNOWN);
   if (!res) {
      PROG_ERR ("(%s) Error during _exec []\n", sqldb_lasterr (db));
      goto errorexit;
   }

   printf ("Counted [%" PRIu32 "] columns in results\n",
            sqldb_res_num_columns (res));

   if (!(colnames = sqldb_res_column_names (res))) {
      PROG_ERR ("Failed to get the column names\n");
      goto errorexit;
   }

   for (size_t i=0; colnames[i]; i++) {
      printf ("%zu [%s]\n", i, colnames[i]);
   }

#if 1
   int rc = sqldb_res_step (res);
   if (rc==0) {
      PROG_ERR ("(%s) No results during _step []\n", sqldb_lasterr (db));
      goto errorexit;
   }
   if (rc==-1) {
      PROG_ERR ("(%s) Error during _step []\n", sqldb_lasterr (db));
      goto errorexit;
   }
#endif

   while (rc==1) {
      uint32_t intvar;
      char *stringvar = NULL;
      uint32_t num_scanned =
                  sqldb_scan_columns (res, sqldb_col_UINT32, &intvar,
                                           sqldb_col_TEXT,   &stringvar,
                                           sqldb_col_UNKNOWN);
      if (num_scanned!=2) {
         PROG_ERR ("(%u) Incomplete scanning of columns\n", num_scanned);
         free (stringvar);
         goto errorexit;
      }
      printf ("Scanned in %u[%s]\n", intvar, stringvar);
      free (stringvar);

      rc = sqldb_res_step (res);
      if (rc==-1) {
         PROG_ERR ("(%s) Error during _step []\n", sqldb_lasterr (db));
         goto errorexit;
      }
   }

   sqldb_res_del (res); res = NULL;

   // Get the first result from a query in a single step
   {
      uint32_t intvar, intparam = 121;
      char *stringvar = NULL;

      uint32_t num_scanned =
         sqldb_exec_and_fetch (db, "select * from one where col_a=#1;",
               sqldb_col_UINT32,    &intparam,
               sqldb_col_UNKNOWN, // End of SQL parameters
               sqldb_col_UINT32,    &intvar,
               sqldb_col_TEXT,      &stringvar,
               sqldb_col_UNKNOWN); // End of dest pointers
      if (num_scanned != 2) {
         PROG_ERR ("[%u] Unexpected number of columns scanned\n", num_scanned);
         free (stringvar);
         goto errorexit;
      }
      printf ("Successfully scanned in single-step: [%u] : [%s]\n", intvar,
                                                                    stringvar);
      free (stringvar);
   }
   sqldb_res_del (res); res = NULL;

   // Get the timestamp
   res = sqldb_exec (db, "select * from three;", sqldb_col_UNKNOWN);
   if (!res) {
      PROG_ERR ("(%s) Error during _exec []\n", sqldb_lasterr (db));
      goto errorexit;
   }

#if 1
   rc = sqldb_res_step (res);
   if (rc==0) {
      PROG_ERR ("(%s) No results during _step []\n", sqldb_lasterr (db));
      goto errorexit;
   }
   if (rc==-1) {
      PROG_ERR ("(%s) Error during _step []\n", sqldb_lasterr (db));
      goto errorexit;
   }
#endif

   while (rc==1) {
      uint32_t intvar;
      uint64_t datevar;
      uint32_t num_scanned =
                  sqldb_scan_columns (res, sqldb_col_UINT32,   &intvar,
                                           sqldb_col_DATETIME, &datevar,
                                           sqldb_col_UNKNOWN);
      if (num_scanned!=2) {
         PROG_ERR ("(%u) Incomplete scanning of columns\n", num_scanned);
         goto errorexit;
      }
      printf ("Scanned in %u[%" PRIu64 "] [%s]\n", intvar,
                                                   datevar,
                                                   ctime ((int64_t *)&datevar));

      rc = sqldb_res_step (res);
      if (rc==-1) {
         PROG_ERR ("(%s) Error during _step []\n", sqldb_lasterr (db));
         goto errorexit;
      }
   }

   sqldb_res_del (res); res = NULL;

   // Test the _ignore() functions
   const char *qstr = "insert into one values (#1, #2);";
   uint64_t ione = 6464;
   const char *stwo = "Two Value";
   if (!(sqldb_exec_ignore (db, qstr, sqldb_col_UINT64,  &ione,
                                      sqldb_col_TEXT,    &stwo,
                                      sqldb_col_UNKNOWN))) {
      fprintf (stderr, "Failed to execute the _ignore() statement\n");
      goto errorexit;
   }

   ret = EXIT_SUCCESS;
errorexit:

   if (inf)
      fclose (inf);

   for (size_t i=0; colnames && colnames[i]; i++) {
      free (colnames[i]);
   }
   free (colnames);

   sqldb_res_del (res);
   sqldb_close (db);
   PROG_ERR ("XXX Test: %s XXX\n", ret==EXIT_SUCCESS ? "passed" : "failed");

   return ret;
}

