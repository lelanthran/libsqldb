
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "sqldb/sqldb.h"

#define TESTDB    ("/tmp/testdb.sql")

int main (void)
{
   static const char *create_stmts[] = {
"create table one (col_a int primary key, col_b varchar (20));",
"create table two (col_1 int primary key, col_2 int references one (col_a));",
"insert into one values (41, '42');",
"insert into two values (51, 41);",
NULL,
   };


   int ret = EXIT_FAILURE;
   sqldb_t *db = NULL;
   sqldb_res_t *res = NULL;

   if (!sqldb_create (TESTDB, sqldb_SQLITE)) {
      XERROR ("(%s) Could not create database file\n", TESTDB);
      goto errorexit;
   }

   db = sqldb_open (TESTDB, sqldb_SQLITE);
   if (!db) {
      XERROR ("Unable to open database - %s\n", sqldb_lasterr (db));
      goto errorexit;
   }

   for (size_t i=0; create_stmts[i]; i++) {
      sqldb_res_t *r = sqldb_exec (db, create_stmts[i], sqldb_col_UNKNOWN);
      if (!r) {
         XERROR ("(%s) Error during _exec [%s]\n", sqldb_lasterr (db),
                                                   create_stmts[i]);
         goto errorexit;
      }
      switch (sqldb_res_step (r)) {
         case 1:  XERROR ("Results available\n");                    break;
         case 0:  XERROR ("Execution complete\n");                   break;
         case -1: XERROR ("Error (%s)\n", sqldb_res_lasterr (r));    break;
      }
      sqldb_res_del (r);
   }

   res = sqldb_exec (db, "BEGIN TRANSACTION;", sqldb_col_UNKNOWN);
   if (!res) {
      XERROR ("(%s) Error during _exec []\n", sqldb_lasterr (db));
      goto errorexit;
   }
   if (sqldb_res_step (res)!=0) {
      XERROR ("(%s) Error during _step []\n", sqldb_lasterr (db));
      goto errorexit;
   }

   sqldb_res_del (res); res = NULL;

   for (uint32_t i=0; i<25; i++) {
      uint32_t u32 = i + 100;
      res = sqldb_exec (db, "insert into one values (#1, #2)",
                              sqldb_col_UINT32, &u32,
                              sqldb_col_TEXT,   "testing",
                              sqldb_col_UNKNOWN);
      if (!res) {
         XERROR ("(%s) Error during _exec []\n", sqldb_lasterr (db));
         goto errorexit;
      }
      if (sqldb_res_step (res)!=0) {
         XERROR ("(%s) Error during _step []\n", sqldb_lasterr (db));
         goto errorexit;
      }

      sqldb_res_del (res); res = NULL;
   }

   /*
   res = sqldb_exec (db, "ROLLBACK;", sqldb_col_UNKNOWN);
   if (!res) {
      XERROR ("(%s) Error during _exec []\n", sqldb_lasterr (db));
      goto errorexit;
   }
   int rc = sqldb_res_step (res);
   {
      XERROR ("(%s) Error during _step [%i]\n", sqldb_lasterr (db), rc);
      // goto errorexit;
   }

   sqldb_res_del (res); res = NULL;
   */

   XERROR ("Completed parameterised insertion\n");

   res = sqldb_exec (db, "select * from one;", sqldb_col_UNKNOWN);
   if (!res) {
      XERROR ("(%s) Error during _exec []\n", sqldb_lasterr (db));
      goto errorexit;
   }

#if 0
   int rc = sqldb_res_step (res);
   if (rc==0) {
      XERROR ("(%s) No results during _step []\n", sqldb_lasterr (db));
      goto errorexit;
   }
   if (rc==-1) {
      XERROR ("(%s) Error during _step []\n", sqldb_lasterr (db));
      goto errorexit;
   }
#endif

   // TODO: scan in all the results in a loop and print them
   while (sqldb_res_step (res)==1) {
      uint32_t intvar;
      char *stringvar = NULL;
      uint32_t num_scanned =
                  sqldb_scan_columns (res, sqldb_col_UINT32, &intvar,
                                           sqldb_col_TEXT,   &stringvar,
                                           sqldb_col_UNKNOWN);
      if (num_scanned!=2) {
         XERROR ("(%u) Incomplete scanning of columns\n", num_scanned);
         free (stringvar);
         goto errorexit;
      }
      XLOG ("Scanned in %u[%s]\n", intvar, stringvar);
      free (stringvar);
   }

   sqldb_res_del (res); res = NULL;

   // Get the first result from a query in a single step
   {
      uint32_t intvar, intparam = 121;
      char *stringvar = NULL;

      uint32_t num_scanned =
         sqldb_exec_and_fetch (db, "select * from one where col_a=?1;",
               sqldb_col_UINT32,    &intparam,
               sqldb_col_UNKNOWN, // End of SQL parameters
               sqldb_col_UINT32,    &intvar,
               sqldb_col_TEXT,      &stringvar,
               sqldb_col_UNKNOWN); // End of dest pointers
      if (num_scanned != 2) {
         XERROR ("[%u] Unexpected number of columns scanned\n", num_scanned);
         free (stringvar);
         goto errorexit;
      }
      XLOG ("Successfully scanned in single-step: [%u] : [%s]\n", intvar,
                                                                  stringvar);
      free (stringvar);
   }

   ret = EXIT_SUCCESS;
errorexit:

   sqldb_res_del (res);
   sqldb_close (db);
   XERROR ("XXX Test: %s XXX\n", ret==EXIT_SUCCESS ? "passed" : "failed");

   xerror_set_logfile (NULL);

   return ret;
}

