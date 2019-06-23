
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqldb/sqldb.h"
#include "sqldb_auth/sqldb_auth.h"

#define TESTDB_SQLITE    ("/tmp/testdb.sql3")
#define TESTDB_POSTGRES  ("postgresql://lelanthran:a@localhost:5432/lelanthran")
#define TEST_BATCHFILE   ("test-file.sql")

#define PROG_ERR(...)      do {\
      fprintf (stderr, ":%s:%d: ", __FILE__, __LINE__);\
      fprintf (stderr, __VA_ARGS__);\
} while (0)

static bool create_users (sqldb_t *db)
{
   bool error = true;

   static const struct {
      const char *email;
      const char *nick;
   } users[] = {
      { "one@email.com",   "ONE"    },
      { "two@email.com",   "TWO"    },
      { "three@email.com", "THREE"  },
      { "four@email.com",  "FOUR"   },
      { "five@email.com",  "FIVE"   },
      { "six@email.com",   "SIX"    },
      { "seven@email.com", "SEVEN"  },
      { "eight@email.com", "EIGHT"  },
      { "nine@email.com",  "NINE"   },
      { "ten@email.com",   "TEN"    },
   };

   for (size_t i=0; i<sizeof users/sizeof users[0]; i++) {
      if (!(sqldb_auth_user_create (db, users[i].email,
                                        users[i].nick,
                                        "123456"))) {
         PROG_ERR ("Failed to create user [%s]\n", users[i].email);
         goto errorexit;
      }

      printf ("Created user [%s,%s]\n", users[i].email, users[i].nick);
   }

   for (size_t i=0; i<sizeof users/sizeof users[0]; i+=3) {
      if (!(sqldb_auth_user_rm (db, users[i].email))) {
         PROG_ERR ("Failed to create user [%s]\n", users[i].email);
         goto errorexit;
      }

      printf ("Created user [%s,%s]\n", users[i].email, users[i].nick);
   }

   error = false;

errorexit:

   return !error;
}

static bool create_groups (sqldb_t *db)
{
   bool error = true;

   static const struct {
      const char *name;
      const char *descr;
   } groups[] = {
      { "Group-1", "GROUP description: ONE"    },
      { "Group-2", "GROUP description: TWO"    },
      { "Group-3", "GROUP description: THREE"  },
      { "Group-4", "GROUP description: FOUR"   },
      { "Group-5", "GROUP description: FIVE"   },
      { "Group-6", "GROUP description: SIX"    },
   };

   for (size_t i=0; i<sizeof groups/sizeof groups[0]; i++) {
      if (!(sqldb_auth_user_create (db, groups[i].name,
                                        groups[i].descr,
                                        "123456"))) {
         PROG_ERR ("Failed to create group [%s]\n", groups[i].name);
         goto errorexit;
      }

      printf ("Created group [%s,%s]\n", groups[i].name, groups[i].descr);
   }

   for (size_t i=0; i<sizeof groups/sizeof groups[0]; i++) {
      if (!(sqldb_auth_group_rm (db, groups[i].name))) {
         PROG_ERR ("Failed to create group [%s]\n", groups[i].name);
         goto errorexit;
      }

      printf ("Created group [%s,%s]\n", groups[i].name, groups[i].descr);
   }

   error = false;

errorexit:

   return !error;
}

int main (int argc, char **argv)
{
   int ret = EXIT_FAILURE;

   sqldb_dbtype_t dbtype = sqldb_UNKNOWN;
   const char *dbname = NULL;

   sqldb_t *db = NULL;

   PROG_ERR ("Testing sqldb_auth version [%s]\n", SQLDB_VERSION);
   if (argc <= 1) {
      PROG_ERR ("Failed to specify one of 'sqlite' or 'postgres'\n");
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
      PROG_ERR ("Failed to specify one of 'sqlite' or 'postgres'\n");
      return EXIT_FAILURE;
   }

   if (!(db = sqldb_open (dbname, dbtype))) {
      PROG_ERR ("Unable to open database - %s\n", sqldb_lasterr (db));
      goto errorexit;
   }

   if (!(sqldb_auth_initdb (db))) {
      PROG_ERR ("Failed to initialise the db for auth module [%s]\n",
                  sqldb_lasterr (db));
      goto errorexit;
   }

   if (!(create_users (db))) {
      PROG_ERR ("Failed to create users, aborting\n");
      goto errorexit;
   }

   if (!(create_groups (db))) {
      PROG_ERR ("Failed to create groups, aborting\n");
      goto errorexit;
   }

   ret = EXIT_SUCCESS;

errorexit:

   sqldb_close (db);

   return ret;
}

