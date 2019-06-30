
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

/* ******************************************************************** */
/* ******************************************************************** */

#define PROG_ERR(...)      do {\
      fprintf (stderr, ":%s:%d: ", __FILE__, __LINE__);\
      fprintf (stderr, __VA_ARGS__);\
} while (0)


static void print_help_msg (const char *cmd)
{
#define COMMAND_HELP    \
"  help <command>",\
"     Provide the help message for the specified command <command>.",\
""
#define COMMAND_CREATE    \
"  create ",\
"     Creates a new, empty sqlite database. This is only needed for Sqlite.",\
"     When using a Postgres database, the database must be created by the",\
"     caller.",\
""
#define COMMAND_INIT    \
"  init <database-type> <database>",\
"     Initialises a database with the necessary tables. The <database-type>",\
"     must be one of 'sqlite' or 'postgres'. When the <database-type> is",\
"     'sqlite' the <database> argument refers to a filename for the sqlite",\
"     database. When the <database-type> is 'postgres' the <database> ",\
"     argument must contain the postgres connection string.",\
""
#define USER_NEW_MSG    \
"  user_new <email> <nick> <password> ",\
"     Create a new user, using specified <email>, <nick> and <password>.",\
""
#define USER_RM_MSG    \
"  user_rm <email> ",\
"     Delete the user with the specified <email>.",\
""
#define USER_MOD_MSG    \
"  user_mod <email> <new-email> <new-nick> <new-password>",\
"     Modify the user record specified with <email>, setting the new email,",\
"     nick and password.",\
""
#define USER_INFO_MSG    \
"  user_info <email> ",\
"     Display all the user info for the record specified with <email>.",\
""
#define USER_FIND_MSG    \
"  user_find <email-pattern> <nick-pattern>",\
"     Find users matching either the <email-pattern> or <nick-pattern>. The",\
"     pattern must match the rules for 'SQL LIKE' globbing patterns:",\
"        \%: Matches zero or more wildcard characters",\
"        _: Matches a single wildcard character",\
""
#define USER_PERMS_MSG    \
"  user_perms <email> <resource>",\
"     Display the permission bits of the user specified with <email> for the",\
"     resource specified with <resource>. Only the actual permissions of the",\
"     user is displayed; permissions inherited from group membership will not",\
"     be displayed.",\
"     To display the effective permissions of a user use the 'perms' command",\
"     below.",\
"     See '--display-bits' for display options.",\
""

#define GROUP_NEW_MSG    \
"  group_new <name> <description> ",\
"     Create a new group, using specified <name> and <description>.",\
""
#define GROUP_RM_MSG    \
"  group_rm <name> ",\
"     Delete the group with the specified <name>.",\
""
#define GROUP_MOD_MSG    \
"  group_mod <name> <new-name> <new-description>",\
"     Modify the group record specified with <name>, setting the new name,",\
"     and description.",\
""
#define GROUP_INFO_MSG    \
"  group_info <name>",\
"     Display all the group info for the record specified with <name>.",\
""
#define GROUP_FIND_MSG    \
"  group_find <name-pattern> <description-pattern>",\
"     Find groups matching either the <name-pattern> or <description-pattern>.",\
"     The pattern must match the rules for 'SQL LIKE' globbing patterns:",\
"        \%: Matches zero or more wildcard characters",\
"        _: Matches a single wildcard character",\
""
#define GROUP_PERMS_MSG    \
"  group_perms <name> <resource>",\
"     Display the permission bits of the group specified with <name> for the",\
"     resource specified with <resource>.",\
"     See '--display-bits' for display options.",\
""

#define GROUP_ADDUSER_MSG    \
"  group_adduser <name> <email>",\
"     Add the user specified by <email> to the group specified by <name>.",\
""
#define GROUP_RMUSER_MSG    \
"  group_rmuser <name> <email>",\
"     Remove the user specified by <email> from the group specified by <name>.",\
""
#define GROUP_MEMBERS_MSG    \
"  group_members <name>",\
"     List all the users in the group specified by <name>.",\
""

#define GRANT_MSG    \
"  grant <email> <resource> <bit-number [ | bit-number | ...]",\
"     Allow user specified by <email> access to the resource specified by",\
"     <resource>. The nature of the access is specified by one or more",\
"     <bit-number> specifiers.",\
""
#define REVOKE_MSG    \
"  revoke <email> <resource> <bit-number [ | bit-number | ...]",\
"     Revoke access to the resource specified by <resource> for user specified",\
"     by <email>. The nature of the access is specified by one or more",\
"     <bit-number> specifiers.",\
""
#define PERMS_MSG    \
"  perms <email> <resource>",\
"     Display the *EFFECTIVE* permission bits of the user specified with",\
"     <email> for the resource specified with <resource>. The effective",\
"     permissions include all the permissions inherited from group",\
"     membership.",\
"     See '--display-bits' for display options.",\
""
   static const struct {
      const char *cmd;
      const char *msg[20];
   } cmd_help[] = {
      { "help",            { COMMAND_HELP       }  },
      { "create",          { COMMAND_CREATE     }  },
      { "init",            { COMMAND_INIT       }  },

      { "user_new",        { USER_NEW_MSG       }  },
      { "user_rm",         { USER_RM_MSG       }  },
      { "user_mod",        { USER_MOD_MSG       }  },
      { "user_info",       { USER_INFO_MSG      }  },
      { "user_find",       { USER_FIND_MSG      }  },
      { "user_perms",      { USER_PERMS_MSG     }  },

      { "group_new",       { GROUP_NEW_MSG      }  },
      { "group_rm",        { GROUP_RM_MSG      }  },
      { "group_mod",       { GROUP_MOD_MSG      }  },
      { "group_info",      { GROUP_INFO_MSG     }  },
      { "group_find",      { GROUP_FIND_MSG     }  },
      { "group_perms",     { GROUP_PERMS_MSG    }  },

      { "group_adduser",   { GROUP_ADDUSER_MSG  }  },
      { "group_rmuser",    { GROUP_RMUSER_MSG   }  },
      { "group_members",   { GROUP_MEMBERS_MSG  }  },

      { "grant",           { GRANT_MSG          }  },
      { "revoke",          { REVOKE_MSG         }  },
      { "perms",           { PERMS_MSG          }  },
   };

   static const char *msg[] = {
"sqldb_auth: A command-line program to manage user, groups and permissions",
"using the sqldb_auth module.",
"",
"-----",
"USAGE",
"-----",
"     sqldb_auth help <command>",
"     sqldb_auth [options] <command>",
"     sqldb_auth <command> [options]",
"",
"The first form of usage displays the builtin help on every command. The",
"second and third forms of usage are identical in behaviour and serve to",
"show that commands and options may be interspersed.",
"",
"Options are of the form of  '--name=value' while commands and command",
"arguments are positional.",
"",
"",
"-------",
"OPTIONS",
"-------",
" --help",
"     Print this message, then exit.",
"",
" --verbose",
"     Be verbose in the messages printed to screen. By default only errors",
"     and some informational messages are printed.",
"",
" --database-type=<sqlite | postgres>",
"  Database type. When database-type is 'sqlite' then '--database' is used to",
"  determine the filename of the sqlite3 database file. When database-type is ",
"  'postgres' then '--database' must contain the full connection string for",
"  a postgres database.",
"  Defaults to 'sqlite'.",
"",
" --database=<connection>",
"  Specifies the database to connect to. When '--database-type' is 'sqlite'",
"  then '--database' specifies a sqlite3 file. When '--database-type' is",
"  'postgres' then '--database' must contain a postgres connection string.",
"  Defaults to 'sqldb_auth.sql3,",
"",
" --display-bits=<binary | hex | oct | dec>",
"  Specifies the format to display permission bits in. Defaults to binary.",
"",
"",
"----------------",
"GENERAL COMMANDS",
"----------------",
COMMAND_HELP,
COMMAND_CREATE,
COMMAND_INIT,
"",
"",
"-------------",
"USER COMMANDS",
"-------------",
USER_NEW_MSG,
USER_RM_MSG,
USER_MOD_MSG,
USER_INFO_MSG,
USER_FIND_MSG,
USER_PERMS_MSG,
"",
"",
"--------------",
"GROUP COMMANDS",
"--------------",
GROUP_NEW_MSG,
GROUP_RM_MSG,
GROUP_MOD_MSG,
GROUP_INFO_MSG,
GROUP_FIND_MSG,
GROUP_PERMS_MSG,
"",
"",
"-------------------------",
"GROUP MEMBERSHIP COMMANDS",
"-------------------------",
GROUP_ADDUSER_MSG,
GROUP_RMUSER_MSG,
GROUP_MEMBERS_MSG,
"",
"",
"-------------------",
"PERMISSION COMMANDS",
"-------------------",
GRANT_MSG,
REVOKE_MSG,
PERMS_MSG,
"",
"",
   };

   if (!cmd) {
      for (size_t i=0; i<sizeof msg/sizeof msg[0]; i++) {
         printf ("%s\n", msg[i]);
      }
      return;
   }

   for (size_t i=0; i<sizeof cmd_help/sizeof cmd_help[0]; i++) {
      if ((strcmp (cmd_help[i].cmd, cmd))==0) {
         for (size_t j=0;
              j<sizeof cmd_help[i].msg/sizeof cmd_help[i].msg[0];
              j++) {
            if (cmd_help[i].msg[j]) {
               printf ("%s\n", cmd_help[i].msg[j]);
            }
         }
         return;
      }
   }
   printf ("   (Unrecognised command [%s]\n", cmd);
}

static bool cmd_help (char **args)
{
   printf ("\n\nHow to use command [%s]:\n", args[1]);
   print_help_msg (args[1]);
   return true;
}

static bool cmd_create (char **args)
{
   printf ("Creating empty sqlite database [%s]\n", args[1]);

   bool ret = sqldb_create (args[1], sqldb_SQLITE);

   if (ret) {
      printf ("Created empty sqlite database [%s]\n", args[1]);
   } else {
      PROG_ERR ("Failed to create empty sqlite database [%s]\n", args[1]);
   }

   return ret;
}

static bool cmd_init (char **args)
{
   bool error = true;
   sqldb_t *db = NULL;
   sqldb_dbtype_t dbtype = sqldb_UNKNOWN;

   printf ("Initialising [%s] database on [%s]\n", args[1], args[2]);

   if ((strcmp (args[1], "sqlite"))==0) {
      dbtype = sqldb_SQLITE;
   }

   if ((strcmp (args[1], "postgres"))==0) {
      dbtype = sqldb_POSTGRES;
   }

   if (dbtype==sqldb_UNKNOWN) {
      PROG_ERR ("Unrecognised dbtype [%s].\n", args[1]);
      goto errorexit;
   }

   if (!(db = sqldb_open (args[2], dbtype))) {
      PROG_ERR ("Unable to open database - %s\n", sqldb_lasterr (db));
      goto errorexit;
   }

   if (!(sqldb_auth_initdb (db))) {
      PROG_ERR ("Failed to initialise the db for auth module [%s]\n",
                  sqldb_lasterr (db));
      goto errorexit;
   }

   error = false;

errorexit:

   sqldb_close (db);

   if (!error) {
      printf ("Initialised [%s] database [%s] for use with sqldb_auth.\n",
               args[1], args[2]);
   }
   return !error;
}

static sqldb_t *g_db = NULL;

static bool cmd_user_new (char **args)
{
   return sqldb_auth_user_create (g_db, args[1], args[2], args[3]);
}

static bool cmd_user_rm (char **args)
{
   return sqldb_auth_user_rm (g_db, args[1]);
}

static bool cmd_user_mod (char **args)
{
   return sqldb_auth_user_mod (g_db, args[1], args[2], args[3], args[4]);
}

int main (int argc, char **argv)
{
   int ret = EXIT_FAILURE;

   static const struct command_t {
      const char *cmd;
      bool (*fptr) (char **args);
      size_t min_args;
      size_t max_args;
   } cmds[] = {
      { "help",               cmd_help,      2, 2     },
      { "create",             cmd_create,    2, 2     },
      { "init",               cmd_init,      3, 3     },

      { "user_new",           cmd_user_new,  4, 4     },
      { "user_rm",            cmd_user_rm,   2, 2     },
      { "user_mod",           cmd_user_mod,  5, 5     },
      { "user_info",          cmd_help,      2, 2     },
      { "user_find",          cmd_help,      3, 3     },
      { "user_perms",         cmd_help,      3, 3     },

      { "group_new",          cmd_help,      3, 3     },
      { "group_rm",           cmd_help,      2, 2     },
      { "group_mod",          cmd_help,      4, 4     },
      { "group_info",         cmd_help,      2, 2     },
      { "group_find",         cmd_help,      3, 3     },
      { "group_perms",        cmd_help,      3, 3     },

      { "group_adduser",      cmd_help,      3, 3     },
      { "group_rmuser",       cmd_help,      3, 3     },
      { "group_members",      cmd_help,      2, 2     },

      { "grant",              cmd_help,      4, 68    },
      { "revoke",             cmd_help,      4, 68    },
      { "perms",              cmd_help,      3, 3     },
   };

   const struct command_t *cmd = NULL;

   sqldb_dbtype_t dbtype = sqldb_UNKNOWN;
   const char *dbname = NULL;

   const char *opt_help = NULL,
              *opt_dbtype = NULL,
              *opt_dbconn = NULL,
              *opt_display_bits = NULL,
              *opt_verbose = NULL;
   char **args = NULL, **lopts = NULL, **sopts = NULL;
   size_t nopts = 0;
   size_t nargs = 0;


   if ((nopts = process_args (argc, argv, &args, &lopts, &sopts))==(size_t)-1) {
      PROG_ERR ("Failed to read the command line arguments\n");
   }

   /* Work out how many arguments we have. */
   for (size_t i=0; args && args[i]; i++)
      nargs++;

   opt_help = find_opt (lopts, "help", sopts, 'h');
   opt_dbtype = find_opt (lopts, "database-type", sopts, 't');
   opt_dbconn = find_opt (lopts, "database", sopts, 'D');
   opt_display_bits = find_opt (lopts, "display-bits", sopts, 'd');
   opt_verbose = find_opt (lopts, "verbose", sopts, 'v');

   /* Preliminary stuff, initialisation, parsing commands, etc */
   if (opt_verbose) {
      printf ("sqldb_auth_cli program:\n"
              "   sqldb_auth version [%s]\n"
              "   sqlite version [%s]\n",
              SQLDB_VERSION,
              SQLITE_VERSION);
   }

   if (opt_help) {
      print_help_msg (NULL);
      ret = EXIT_SUCCESS;
      goto errorexit;
   }


   /* Find the command that was requested, run a few sanity checks */
   for (size_t i=0; i<sizeof cmds/sizeof cmds[0]; i++) {
      if ((strcmp (cmds[i].cmd, args[0]))==0) {
         cmd = &cmds[i];
         break;
      }
   }

   if (!cmd) {
      PROG_ERR ("Unrecognised command: [%s]\n", args[0]);
      goto errorexit;
   }

   if (nargs < cmd->min_args) {
      PROG_ERR ("[%s] Too few arguments: minimum is %zu, got %zu\n",
                cmd->cmd, cmd->min_args, nargs);
      goto errorexit;
   }

   if (nargs > cmd->max_args) {
      PROG_ERR ("[%s] Too many arguments: maximum is %zu, got %zu\n",
                cmd->cmd, cmd->max_args, nargs);
      goto errorexit;
   }



   /* If the command is 'init' or 'help' then we run that separately from
    * the other commands because we must not try to open the database. All
    * the commands other than 'help' and 'init' require the database to
    * be opened first.
    *
    * We also check for 'create' if the caller wants to use an sqlite
    * database that doesn't already exist. Create only works for sqlite.
    */

   if ((strcmp (cmd->cmd, "help"))==0) {
      ret = cmd_help (args) ? EXIT_SUCCESS : EXIT_FAILURE;
      goto errorexit;
   }

   if ((strcmp (cmd->cmd, "create"))==0) {
      ret = cmd_create (args) ? EXIT_SUCCESS : EXIT_FAILURE;
      goto errorexit;
   }

   if ((strcmp (cmd->cmd, "init"))==0) {
      ret = cmd_init (args) ? EXIT_SUCCESS : EXIT_FAILURE;
      goto errorexit;
   }

   opt_dbtype = opt_dbtype ? opt_dbtype : "sqlite";
   dbname = opt_dbconn ? opt_dbconn : "sqldb_auth.sql3";


   /* Figure out what database type we are using */
   dbtype = sqldb_UNKNOWN;

   if ((strcmp (opt_dbtype, "sqlite"))==0) {
      dbtype = sqldb_SQLITE;
   }

   if ((strcmp (opt_dbtype, "postgres"))==0) {
      dbtype = sqldb_POSTGRES;
   }

   if (dbtype==sqldb_UNKNOWN) {
      PROG_ERR ("Unrecognised dbtype [%s].\n", opt_dbtype);
      goto errorexit;
   }


   /* Open the specified database, using the specified type. */
   if (!(g_db = sqldb_open (dbname, dbtype))) {
      PROG_ERR ("Unable to open [%s] database using [%s] connection\n"
                "Error:%s\n", opt_dbtype, opt_dbconn, sqldb_lasterr (g_db));
      goto errorexit;
   }

   if (opt_verbose) {
      printf ("Using [%s] database %s\n", opt_dbtype,
                                          dbtype==sqldb_SQLITE ? dbname : "");
   }

   /* Run the command given */
   if (opt_verbose) {
      printf ("Running command [%s]\n", cmd->cmd);
   }

   ret = cmd->fptr (args) ? EXIT_SUCCESS : EXIT_FAILURE;

errorexit:

   string_array_free (args);
   string_array_free (lopts);
   string_array_free (sopts);

   sqldb_close (g_db);

   return ret;
}

