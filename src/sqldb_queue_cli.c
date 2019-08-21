
#include <stdio.h>
#include <stdlib.h>

#include "sqldb_queue.h"

#define LOG_ERR(...)       do {                          \
   fprintf (stderr, "%s:%i:%m:", __FILE__, __LINE__);   \
   fprintf (stderr, __VA_ARGS__);                        \
} while (0);

#define TESTDB_NAME     ("/tmp/test_qdb.sql")
#define TESTQUEUE_1     ("test-queue 1")

int main (void)
{
   int ret = EXIT_FAILURE;

   int rc = 0;

   sqldb_t *db = NULL;
   sqldb_queue_t *queue = NULL;

   if (!(sqldb_create (TESTDB_NAME, sqldb_SQLITE))) {
      LOG_ERR ("Failed to create database [%s]\n", TESTDB_NAME);
      goto errorexit;
   }

   if (!(db = sqldb_open (TESTDB_NAME, sqldb_SQLITE))) {
      LOG_ERR ("Failed to open database [%s]\n", TESTDB_NAME);
      goto errorexit;
   }

   if ((rc = sqldb_queue_create (db, TESTQUEUE_1))!=SQLDB_QUEUE_SUCCESS) {
      LOG_ERR ("Failed to create queue [%s] in [%s]\n", TESTQUEUE_1,
                                                        TESTDB_NAME);
      goto errorexit;
   }

   if ((rc = sqldb_queue_open (db, TESTQUEUE_1, &queue))!=SQLDB_QUEUE_SUCCESS) {
      LOG_ERR ("Failed to open queue [%s] in [%s]\n", TESTQUEUE_1,
                                                      TESTDB_NAME);
      goto errorexit;
   }

   // Tests start...

   ret = EXIT_SUCCESS;

errorexit:

   if (rc) {
      LOG_ERR ("Last error that was detected [%s]\n",
                  sqldb_queue_strerror (rc));
   }

   sqldb_queue_close (queue);
   sqldb_close (db);

   return ret;
}

