
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

#define PROG_ERR(...)      do {\
      fprintf (stderr, ":%s:%d: ", __FILE__, __LINE__);\
      fprintf (stderr, __VA_ARGS__);\
} while (0)


static void print_help_msg (const char *cmd)
{
#define COMMAND_HELP    \
"  help <command>"\
"     Provide the help message for the specified command <command>."\
""
#define USER_NEW_MSG    \
"  user_new <email> <nick> <password> ",\
"     Create a new user, using specified <email>, <nick> and <password>.",\
""
#define USER_DEL_MSG    \
"  user_del <email> ",\
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
#define GROUP_DEL_MSG    \
"  group_del <name> ",\
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

      { "user_new",        { USER_NEW_MSG       }  },
      { "user_del",        { USER_DEL_MSG       }  },
      { "user_mod",        { USER_MOD_MSG       }  },
      { "user_info",       { USER_INFO_MSG      }  },
      { "user_find",       { USER_FIND_MSG      }  },
      { "user_perms",      { USER_PERMS_MSG     }  },

      { "group_new",       { GROUP_NEW_MSG      }  },
      { "group_del",       { GROUP_DEL_MSG      }  },
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
"-------------",
"USER COMMANDS",
"-------------",
USER_NEW_MSG,
USER_DEL_MSG,
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
GROUP_DEL_MSG,
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

static bool help_cmd (char **args)
{
   printf ("\n\nHow to use command [%s]:\n", args[1]);
   print_help_msg (args[1]);
   return true;
}

static bool user_new (char **args)
{
   return false;
}

int main (int argc, char **argv)
{
   int ret = EXIT_FAILURE;

   static const struct {
      const char *cmd;
      bool (*fptr) (char **args);
      size_t min_args;
      size_t max_args;
   } cmds[] = {
      { "help",               help_cmd, 1, 1 },

      { "user_new",           user_new, 3, 3 },
      { "user_del",           user_new, 1, 1 },
      { "user_mod",           user_new, 4, 4 },
      { "user_info",          user_new, 1, 1 },
      { "user_find",          user_new, 2, 2 },
      { "user_perms",         user_new, 2, 2 },

      { "group_new",          user_new, 2, 2 },
      { "group_del",          user_new, 1, 1 },
      { "group_mod",          user_new, 3, 3 },
      { "group_info",         user_new, 1, 1 },
      { "group_find",         user_new, 2, 2 },
      { "group_perms",        user_new, 2, 2 },

      { "group_adduser",      user_new, 2, 2 },
      { "group_rmuser",       user_new, 2, 2 },
      { "group_members",      user_new, 1, 1 },

      { "grant",              user_new, 3, 67 },
      { "revoke",             user_new, 3, 67 },
      { "perms",              user_new, 2, 2 },
   };

   sqldb_dbtype_t dbtype = sqldb_UNKNOWN;
   sqldb_t *db = NULL;
   const char *dbname = NULL;

   const char *opt_help = NULL,
              *opt_dbtype = NULL,
              *opt_dbconn = NULL,
              *opt_display_bits = NULL;
   char **args = NULL, **lopts = NULL, **sopts = NULL;
   size_t nopts = 0;


   /* Preliminary stuff, initialisation, parsing commands, etc */
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
   opt_display_bits = find_opt (lopts, "display-bits", sopts, 'd');

   if (opt_help) {
      print_help_msg (NULL);
      ret = EXIT_SUCCESS;
      goto errorexit;
   }

   if ((strcmp (args[0], "help"))==0) {
      help_cmd (args);
      ret = EXIT_SUCCESS;
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
   if (!(db = sqldb_open (dbname, dbtype))) {
      PROG_ERR ("Unable to open [%s] database using [%s] connection\n"
                "Error:%s\n", opt_dbtype, opt_dbconn, sqldb_lasterr (db));
      goto errorexit;
   }

   if (!(sqldb_auth_initdb (db))) {
      PROG_ERR ("Failed to initialise the db for auth module [%s]\n",
                  sqldb_lasterr (db));
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

