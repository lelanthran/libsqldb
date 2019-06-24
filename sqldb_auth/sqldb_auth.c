
#include "sqldb_auth/sqldb_auth.h"
#include "sqldb_auth/sqldb_auth_query.h"

#define LOG_ERR(...)      do {\
      fprintf (stderr, ":%s:%d: ", __FILE__, __LINE__);\
      fprintf (stderr, __VA_ARGS__);\
} while (0)


bool sqldb_auth_initdb (sqldb_t *db)
{
   bool error = true;
   const char *init_sqlite_stmt = NULL,
              *create_tables_stmt = NULL;

   if (!db)
      goto errorexit;

   if (!(init_sqlite_stmt = sqldb_auth_query ("init_sqlite")))
      LOG_ERR ("Could not find statement for 'init_sqlite'\n");

   if (!(create_tables_stmt = sqldb_auth_query ("create_tables")))
      LOG_ERR ("Could not find statement for 'create_tables'\n");

   if (!init_sqlite_stmt || !create_tables_stmt)
      goto errorexit;

   if (!(sqldb_batch (db, init_sqlite_stmt, create_tables_stmt))) {
      LOG_ERR ("Failed to execute init/create statement.\n");
      goto errorexit;
   }

   error = false;

errorexit:

   return !error;
}

uint64_t sqldb_auth_user_create (sqldb_t *db,
                                 const char *email,
                                 const char *nick,
                                 const char *password)
{
   bool error = true;
   uint64_t ret = (uint64_t)-1;
   const char *qstring = NULL;

   if (!db ||
       !email     || !nick    || !password ||
       !email[0]  || !nick[0] || !password[0])
      goto errorexit;

   if (!(qstring = sqldb_auth_query ("user_create")))
      goto errorexit;

   ret = sqldb_exec_ignore (db, qstring, sqldb_col_TEXT, email,
                                         sqldb_col_UNKNOWN);
   if (ret==(uint64_t)-1 || ret==0)
      goto errorexit;

   if (!(sqldb_auth_user_mod (db, email, email, nick, password)))
      goto errorexit;

   error = false;

errorexit:

   return error ? (uint64_t)-1 : ret;
}


bool sqldb_auth_user_rm (sqldb_t *db, const char *email);

bool sqldb_auth_user_mod (sqldb_t *db,
                          const char *old_email,
                          const char *new_email,
                          const char *nick,
                          const char *password)
{
   bool error = true;
   const char *qstring = NULL;
   char     sz_salt[256];
   char     sz_hash[65];
   uint8_t  hash[32];

   // TODO:
   // 1. Write a rand() function,
   // 2. Write the stringify function for raw bytes
   // 3. Write the salt-creation function
   if (!db ||
       !old_email    || !new_email    || !nick    || !password ||
       !old_email[0] || !new_email[0] || !nick[0] || !password[0])
      goto errorexit;

   if (!(qstring = sqldb_auth_query ("user_mod")))
      goto errorexit;

   ret = sqldb_exec_ignore (db, qstring, sqldb_col_TEXT, email,
                                         sqldb_col_UNKNOWN);
   error = false;

errorexit:

   return !error;
}


