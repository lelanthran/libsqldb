
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "sqldb/sqldb.h"
#include "sqldb_auth/sqldb_auth.h"

#define TESTDB_SQLITE    ("/tmp/testdb.sql3")
#define TESTDB_POSTGRES  ("postgresql://lelanthran:a@localhost:5432/lelanthran")
#define TEST_BATCHFILE   ("test-file.sql")

#define PROG_ERR(...)      do {\
      fprintf (stderr, ":%s:%d: ", __FILE__, __LINE__);\
      fprintf (stderr, __VA_ARGS__);\
} while (0)

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

static const struct {
   const char *name;
   const char *descr;
} groups[] = {
   { "Group-One",    "GROUP description: ONE"    },
   { "Group-Two",    "GROUP description: TWO"    },
   { "Group-Three",  "GROUP description: THREE"  },
   { "Group-Four",   "GROUP description: FOUR"   },
   { "Group-Five",   "GROUP description: FIVE"   },
   { "Group-Six",    "GROUP description: SIX"    },
};


static bool create_users (sqldb_t *db)
{
   bool error = true;

   sqldb_batch (db, "BEGIN TRANSACTION;", NULL);
   for (size_t i=0; i<sizeof users/sizeof users[0]; i++) {
      if ((sqldb_auth_user_create (db, users[i].email,
                                       users[i].nick,
                                       "123456"))==(uint64_t)-1) {
         PROG_ERR ("Failed to create user [%s]\n%s\n", users[i].email,
                                                       sqldb_lasterr (db));
         goto errorexit;
      }

      printf ("Created user [%s,%s]\n", users[i].email, users[i].nick);
   }

   for (size_t i=0; i<sizeof users/sizeof users[0]; i+=3) {
      if (!(sqldb_auth_user_rm (db, users[i].email))) {
         PROG_ERR ("Failed to remove user [%s]\n%s\n", users[i].email,
                                                       sqldb_lasterr (db));
         goto errorexit;
      }

      printf ("Created user [%s,%s]\n", users[i].email, users[i].nick);
   }

   error = false;

errorexit:

   sqldb_batch (db, "COMMIT;", NULL);

   return !error;
}

static bool list_users (sqldb_t *db)
{
   bool error = true;
   uint64_t nitems = 0;
   char **emails = NULL;
   char **nicks = NULL;
   uint64_t *ids = NULL;

   if (!(sqldb_auth_user_find (db, "t%", NULL,
                               &nitems, &emails, &nicks, &ids))) {
      PROG_ERR ("Failed to execute patterns for user searching\n");
      goto errorexit;
   }

   printf ("Found %" PRIu64 " users\n", nitems);

   for (uint64_t i=0; i<nitems; i++) {
      uint64_t n_id = 0;
      char *n_nick = NULL;
      char sess[65];

      if (!(sqldb_auth_user_info (db, emails[i], &n_id, &n_nick, sess))) {
         PROG_ERR ("Failed to get user info\n");
         goto errorexit;
      }

      printf ("L[%" PRIu64 "][%s][%s]\n", ids[i], emails[i], nicks[i]);

      printf ("I[%" PRIu64 "][%s][%s]\n", n_id, n_nick, sess);

      free (emails[i]);
      free (nicks[i]);
      free (n_nick);
   }

   free (ids);
   free (emails);
   free (nicks);

   error = false;

errorexit:

   return !error;
}

static bool create_groups (sqldb_t *db)
{
   bool error = true;

   // sqldb_batch (db, "BEGIN TRANSACTION;", NULL);
   for (size_t i=0; i<sizeof groups/sizeof groups[0]; i++) {
      if ((sqldb_auth_group_create (db, groups[i].name,
                                        groups[i].descr))==(uint64_t)-1) {
         PROG_ERR ("Failed to create group [%s]\n%s\n", groups[i].name,
                                                        sqldb_lasterr (db));
         goto errorexit;
      }

      printf ("Created group [%s,%s]\n", groups[i].name, groups[i].descr);

      for (size_t j=1; j<sizeof users/sizeof users[0]; j+=3) {
         if (!(sqldb_auth_group_adduser (db, groups[i].name, users[j].email))) {
            PROG_ERR ("Failed to add [%s] to group [%s]: %s\n",
                       users[j].email, groups[i].name,
                       sqldb_lasterr (db));
            goto errorexit;
         }
      }
   }

   for (size_t i=0; i<sizeof groups/sizeof groups[0]; i++) {
      char name[30],
           descr[100];

      sprintf (name, "%s-%zu", groups[i].name, i);
      sprintf (descr, "%s-%zu", groups[i].descr, i);

      if (!(sqldb_auth_group_mod (db, groups[i].name, name, descr))) {
         PROG_ERR ("Failed to modify group [%s][%s]=>[%s][%s]\n",
                   groups[i].name, groups[i].descr,
                   name, descr);
         goto errorexit;
      }
   }

   for (size_t i=0; i<sizeof groups/sizeof groups[0]; i+=3) {
      if (!(sqldb_auth_group_rm (db, groups[i].name))) {
         PROG_ERR ("Failed to remove group [%s]\n%s\n", groups[i].name,
                                                        sqldb_lasterr (db));
         goto errorexit;
      }

      printf ("Created group [%s,%s]\n", groups[i].name, groups[i].descr);
   }

   error = false;

errorexit:
   // sqldb_batch (db, "COMMIT;", NULL);

   return !error;
}

static bool list_groups (sqldb_t *db)
{
   bool error = true;
   uint64_t nitems = 0;
   char **names = NULL;
   char **descrs = NULL;
   uint64_t *ids = NULL;

   if (!(sqldb_auth_group_find (db, "group-t%", NULL,
                                &nitems, &names, &descrs, &ids))) {
      PROG_ERR ("Failed to execute patterns for group searching\n");
      goto errorexit;
   }

   printf ("Found %" PRIu64 " groups\n", nitems);

   for (uint64_t i=0; i<nitems; i++) {
      uint64_t n_id = 0;
      char *n_descr = NULL;

      if (!(sqldb_auth_group_info (db, names[i], &n_id, &n_descr))) {
         PROG_ERR ("Failed to get group info\n");
         goto errorexit;
      }

      printf ("L[%" PRIu64 "][%s][%s]\n", ids[i], names[i], descrs[i]);

      printf ("I[%" PRIu64 "][%s][%s]\n", n_id, names[i], n_descr);

      free (n_descr);
      free (names[i]);
      free (descrs[i]);
   }

   free (ids);
   free (names);
   free (descrs);

   error = false;

errorexit:

   return !error;
}

static bool list_memberships (sqldb_t *db)
{
   bool error = false;

   for (size_t i=0; i<sizeof groups/sizeof groups[0]; i++) {
      uint64_t nitems = 0;
      char **emails = NULL,
           **nicks = NULL;
      uint64_t *ids = NULL;

      if (!(sqldb_auth_group_members (db, groups[i].name, &nitems,
                                                          &emails,
                                                          &nicks,
                                                          &ids))) {
         PROG_ERR ("Failed to get membership for [%s]: %s\n",
                   groups[i].name,
                   sqldb_lasterr (db));
         goto errorexit;
      }

      printf ("Group %s has %" PRIu64 " members\n", groups[i].name, nitems);
      for (uint64_t j=0; j<nitems; j++) {
         printf ("%" PRIu64 ", %s, %s\n", ids[j],
                                          emails[j],
                                          nicks[j]);
         free (emails[j]);
         free (nicks[j]);
      }
      free (emails);
      free (nicks);
      free (ids);
   }

   error = false;

errorexit:
   return error;
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

   if (!(list_users (db))) {
      PROG_ERR ("Failed to list users, aborting\n");
      goto errorexit;
   }

   if (!(create_groups (db))) {
      PROG_ERR ("Failed to create groups, aborting\n");
      goto errorexit;
   }

   if (!(list_groups (db))) {
      PROG_ERR ("Failed to list groups, aborting\n");
      goto errorexit;
   }

   if (!(list_memberships (db))) {
      PROG_ERR ("Failed to list the group members. aborting\n");
      goto errorexit;
   }

   ret = EXIT_SUCCESS;

errorexit:

   sqldb_close (db);

   return ret;
}

