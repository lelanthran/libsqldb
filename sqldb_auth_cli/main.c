
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "sqldb/sqldb.h"
#include "sqldb_auth/sqldb_auth.h"

// This is needed only to display the version of sqlite that we are
// compiled with. A similar include is not possible for postgres, as libpq
// is a runtime determination of version, and is only supported on 9.1 and
// above. In order to prevent an artificial dependency on 9.1 and above we
// don't print the postgres version (yet).
#include "sqlite3/sqlite3ext.h"

/* ********************************************************************
 * An even more comprehensive command-line parsing mechanism. This method
 * allows the program to have long options (--name=value) short options
 * (-n value or -nvalue or -abcnvalue) mixed with non-options (arguments
 * without a '-' or '--'.
 *
 * The caller will be able to support intuitive arguments when options and
 * arguments can be mixed freely. For example the following are all
 * equivalent:
 *   progname --infile=/path/to/infile command-name --outfile=/path/to/outfile
 *   progname command-name --outfile=/path/to/outfile --infile=/path/to/infile
 *   progname --outfile=/path/to/outfile --infile=/path/to/infile command-name
 *
 * To support all of the above the parser must be run on the main() argc and
 * argv parameters, after which all of the arguments, long options and
 * short options will be stored in arrays provided by the caller.
 *
 * The arrays containing the arguments and options are all positional, in that
 * they will contain the arguments in the order they were processed.
 *
 * The arguments array can be searched linearly, stopping when the array
 * element is NULL.
 *
 * The long options array can be searched similarly for the entire
 * name=value pair, or can be searched with the provided function
 * find_lopt() which will return only the value part of the string, which
 * may be an empty string if the user did not provide a value.
 *
 * The short options can be searched linearly using the first element of
 * each string in the array to determine if the option is stored (and
 * using the rest of the string as the value for that option) or can be
 * searched using the function find_sopt() which will return the value
 * part of the option, which may be an empty string if the user did not
 * provide a value for the option.
 *
 * The caller can search for both long options and short options
 * simultaneously using the find_opt() function which will return the long
 * option for the specified long option name if it exists, or the short
 * option for the specified short option name if it exists.
 *
 *
 */
static void string_array_free (char **sarr)
{
   for (size_t i=0; sarr && sarr[i]; i++) {
      free (sarr[i]);
   }
   free (sarr);
}

static char **string_array_append (char ***dst, const char *s1, const char *s2)
{
   bool error = true;
   size_t nstr = 0;

   for (size_t i=0; (*dst) && (*dst)[i]; i++)
      nstr++;

   char **tmp = realloc (*dst, (sizeof **dst) * (nstr + 2));
   if (!tmp)
      goto errorexit;

   *dst = tmp;
   (*dst)[nstr] = NULL;
   (*dst)[nstr+1] = NULL;

   char *scopy = malloc (strlen (s1) + strlen (s2) + 1);
   if (!scopy)
      goto errorexit;

   strcpy (scopy, s1);
   strcat (scopy, s2);

   (*dst)[nstr] = scopy;

   error = false;

errorexit:
   if (error) {
      return NULL;
   }

   return *dst;
}

static const char *find_lopt (char **lopts, const char *name)
{
   if (!lopts || !name)
      return NULL;

   size_t namelen = strlen (name);
   for (size_t i=0; lopts[i]; i++) {
      if ((strncmp (name, lopts[i], namelen))==0) {
         char *ret = strchr (lopts[i], '=');
         return ret ? &ret[1] : "";
      }
   }
   return NULL;
}

static const char *find_sopt (char **sopts, char opt)
{
   if (!sopts || !opt)
      return NULL;

   size_t index = 0;
   for (size_t i=0; sopts[i]; i++)
      index++;

   for (size_t i=index; i>0; i--) {
      for (size_t j=0; sopts[i-1][j]; j++) {
         if (sopts[i-1][j]==opt)
            return &sopts[i-1][j+1];
      }
   }
   return NULL;
}

static const char *find_opt (char **lopts, const char *lname,
                             char **sopts, char sname)
{
   const char *ret = NULL;

   if (lopts && lname)
      ret = find_lopt (lopts, lname);

   if (!ret && sopts && sname)
      ret = find_sopt (sopts, sname);

   return ret;
}

static size_t process_args (int argc, char **argv,
                            char ***args, char ***lopts, char ***sopts)
{
   int error = true;

   char **largs = NULL,
        **llopts = NULL,
        **lsopts = NULL;

   size_t ret = 0;

   free (*args);
   free (*lopts);
   free (*sopts);

   *args = NULL;
   *lopts = NULL;
   *sopts = NULL;

   for (int i=1; i<argc && argv[i]; i++) {
      if ((memcmp (argv[i], "--", 2))==0) {
         // Store in lopt
         if (!(string_array_append (&llopts, &argv[i][2], "")))
            goto errorexit;

         continue;
      }
      if (argv[i][0]=='-') {
         for (size_t j=1; argv[i][j]; j++) {
            if (argv[i][j+1]) {
               if (!(string_array_append (&lsopts, &argv[i][j], "")))
                  goto errorexit;
            } else {
               char *value = &argv[i+1][0];
               if (!value || value[0]==0 || value[0]=='-')
                  value = "";
               if (!(string_array_append (&lsopts, &argv[i][j], value)))
                  goto errorexit;
               if (argv[i+1] && argv[i+1][0]!='-')
                  i++;
               break;
            }
         }
         continue;
      }
      // Default - store in largs
      if (!(string_array_append (&largs, argv[i], "")))
         goto errorexit;

   }

   *args = largs;
   *lopts = llopts;
   *sopts = lsopts;

   error = false;

errorexit:
   if (error) {
      ret = (size_t)-1;
      string_array_free (largs);
      string_array_free (llopts);
      string_array_free (lsopts);
   }

   return ret;
}

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

   sqldb_batch (db, "BEGIN TRANSACTION;", NULL);
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
   sqldb_batch (db, "COMMIT;", NULL);

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

      char name[30];

      sprintf (name, "%s-%zu", groups[i].name, i);

      if (!(sqldb_auth_group_members (db, name, &nitems,
                                                &emails,
                                                &nicks,
                                                &ids))) {
         PROG_ERR ("Failed to get membership for [%s]: %s\n",
                   name,
                   sqldb_lasterr (db));
         goto errorexit;
      }

      printf ("Group %s has %" PRIu64 " members\n", groups[i].name, nitems);
      for (uint64_t j=0; j<nitems; j++) {
         printf ("%" PRIu64 ", %s, %s\n", ids[j],
                                          emails[j],
                                          nicks[j]);
         if (j==0) {
            if (!(sqldb_auth_group_rmuser (db, name, emails[j]))) {
               PROG_ERR ("Failed to remove user [%s/%s] from group [%s]\n%s\n",
                           emails[j], nicks[j], name, sqldb_lasterr (db));
               goto errorexit;
            }
         }
         free (emails[j]);
         free (nicks[j]);
      }
      free (emails);
      free (nicks);
      free (ids);

      emails = NULL;
      nicks = NULL;
      ids = NULL;

      printf ("-----------------------------\n");
   }

   for (size_t i=0; i<sizeof groups/sizeof groups[0]; i++) {
      uint64_t nitems = 0;
      char **emails = NULL,
           **nicks = NULL;
      uint64_t *ids = NULL;

      char name[30];

      sprintf (name, "%s-%zu", groups[i].name, i);

      if (!(sqldb_auth_group_members (db, name, &nitems,
                                                &emails,
                                                &nicks,
                                                &ids))) {
         PROG_ERR (">>Failed to get membership for [%s]: %s\n",
                   name,
                   sqldb_lasterr (db));
         goto errorexit;
      }

      printf (">>Group %s has %" PRIu64 " members\n", groups[i].name, nitems);
      for (uint64_t j=0; j<nitems; j++) {
         printf (">>%" PRIu64 ", %s, %s\n", ids[j],
                                          emails[j],
                                          nicks[j]);
         free (emails[j]);
         free (nicks[j]);
      }
      free (emails);
      free (nicks);
      free (ids);

      emails = NULL;
      nicks = NULL;
      ids = NULL;

      printf (">>========================================\n");
   }

   error = false;

errorexit:
   return !error;
}

static void print_help_msg (void)
{
   static const char *msg[] = {
"sqldb_auth: A command-line program to manage user, groups and permissions",
"using the sqldb_auth module.",
"",
"USAGE:",
"sqldb_auth [options] <command>",
"",
"",
"OPTIONS:",
" --help",
"     Print this message, then exit.",
"",
" --dbtype=<sqlite | postgres>",
"  Database type. When dbtype is 'sqlite' then '--db' is used to determine",
"  the filename of the sqlite3 database file. When dbtype is 'postgres'",
"  '--dbtype' must contain the full connection string for a postgres database.",
"  Defaults to 'sqlite'.",
"",
" --db=<connection>",
"  Specifies the database to connect to. When '--dbtype' is 'sqlite' then",
"  '--db' specifies a sqlite3 file. When '--dbtype' is 'postgres' then this",
"  option must contain a full postgres connection string.",
"  Defaults to 'sqldb_auth.sql3",
"",
" --display-bits=<binary | hex | oct | dec>",
"  Specifies the format to display permission bits in. Defaults to binary.",
"",
"",
"USER COMMANDS:",
"  user_new <email> <nick> <password> ",
"     Create a new user, using specified <email>, <nick> and <password>.",
"  user_del <email> ",
"     Delete the user with the specified <email>.",
"  user_mod <email> <new-email> <new-nick> <new-password>",
"     Modify the user record specified with <email>, setting the new email,",
"     nick and password.",
"  user_info <email> ",
"     Display all the user info for the record specified with <email>.",
"  user_find <email-pattern> <nick-pattern>",
"     Find users matching either the <email-pattern> or <nick-pattern>. The",
"     pattern must match the rules for 'SQL LIKE' globbing patterns:",
"        \%: Matches zero or more wildcard characters",
"        _: Matches a single wildcard character",
"  user_perms <email> <resource>",
"     Display the permission bits of the user specified with <email> for the",
"     resource specified with <resource>. Only the actual permissions of the",
"     user is displayed; permissions inherited from group membership will not",
"     be displayed.",
"     To display the effective permissions of a user use the 'perms' command",
"     below.",
"     See '--display-bits' for display options.",
"",
"GROUP COMMANDS:",
"  group_new <name> <description> ",
"     Create a new group, using specified <name> and <description>.",
"  group_del <name> ",
"     Delete the group with the specified <name>.",
"  group_mod <name> <new-name> <new-description>",
"     Modify the group record specified with <name>, setting the new name,",
"     and description.",
"  group_info <name>",
"     Display all the group info for the record specified with <name>.",
"  group_find <name-pattern> <description-pattern>",
"     Find groups matching either the <name-pattern> or <description-pattern>.",
"     The pattern must match the rules for 'SQL LIKE' globbing patterns:",
"        \%: Matches zero or more wildcard characters",
"        _: Matches a single wildcard character",
"  group_perms <name> <resource>",
"     Display the permission bits of the group specified with <name> for the",
"     resource specified with <resource>.",
"     See '--display-bits' for display options.",
"",
"",
"GROUP MEMBERSHIP COMMANDS:",
"  group_adduser <name> <email>",
"     Add the user specified by <email> to the group specified by <name>.",
"  group_rmuser <name> <email>",
"     Remove the user specified by <email> from the group specified by <name>.",
"  group_members <name>",
"     List all the users in the group specified by <name>.",
"",
"PERMISSION COMMANDS:",
"  grant <email> <resource> <bit-number [ | bit-number | ...]",
"     Allow user specified by <email> access to the resource specified by",
"     <resource>. The nature of the access is specified by one or more",
"     <bit-number> specifiers.",
"  revoke <email> <resource> <bit-number [ | bit-number | ...]",
"     Revoke access to the resource specified by <resource> for user specified",
"     by <email>. The nature of the access is specified by one or more",
"     <bit-number> specifiers.",
"",
"  perms <email> <resource>",
"     Display the *EFFECTIVE* permission bits of the user specified with",
"     <email> for the resource specified with <resource>. The effective",
"     permissions include all the permissions inherited from group",
"     membership.",
"     See '--display-bits' for display options.",
"",

   };

   for (size_t i=0; i<sizeof msg/sizeof msg[0]; i++) {
      printf ("%s\n", msg[i]);
   }
}

int main (int argc, char **argv)
{
   int ret = EXIT_FAILURE;

   sqldb_dbtype_t dbtype = sqldb_UNKNOWN;
   sqldb_t *db = NULL;
   const char *dbname = NULL;

   const char *opt_help = NULL,
              *opt_dbtype = NULL,
              *opt_dbconn = NULL;

   char **args = NULL, **lopts = NULL, **sopts = NULL;
   size_t nopts = 0;

   printf ("Using:\n"
           "   sqldb_auth version [%s]\n"
           "   sqlite version [%s]\n",
           SQLDB_VERSION,
           SQLITE_VERSION);

   if ((nopts = process_args (argc, argv, &args, &lopts, &sopts))==(size_t)-1) {
      PROG_ERR ("Failed to read the command line arguments\n");
   }

   opt_help = find_opt (lopts, "help", sopts, 'h');
   opt_dbtype = find_opt (lopts, "dbtype", sopts, 'D');
   opt_dbconn = find_opt (lopts, "db", sopts, 'C');

   if (opt_help) {
      print_help_msg ();
      ret = EXIT_SUCCESS;
      goto errorexit;
   }

   if (argc <= 1) {
      PROG_ERR ("Failed to specify one of 'sqlite' or 'postgres'\n");
      return EXIT_FAILURE;
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

   string_array_free (args);
   string_array_free (lopts);
   string_array_free (sopts);

   sqldb_close (db);

   return ret;
}

