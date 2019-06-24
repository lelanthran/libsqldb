
#include <string.h>
#include <stdio.h>

#include "sqldb_auth/sqldb_auth_query.h"


#define init_sqlite \
   "PRAGMA foreign_keys = ON;"

#define create_tables \
"BEGIN TRANSACTION;" \
\
"CREATE TABLE t_user (" \
"   c_id         INTEGER PRIMARY KEY," \
"   c_session    TEXT UNIQUE," \
"   c_expiry     INTEGER," \
"   c_email      TEXT UNIQUE," \
"   c_nick       TEXT," \
"   c_salt       TEXT," \
"   c_hash       TEXT);" \
"" \
"CREATE TABLE t_group (" \
"   c_id            INTEGER PRIMARY KEY," \
"   c_name          TEXT UNIQUE," \
"   c_description   TEXT);" \
"" \
"CREATE TABLE t_group_membership (" \
"   c_user    INTEGER," \
"   c_group   INTEGER," \
"      FOREIGN KEY (c_user) REFERENCES t_user(c_id)," \
"      FOREIGN KEY (c_group) REFERENCES t_group(c_id));" \
"" \
"CREATE TABLE t_user_perm (" \
"   c_user       INTEGER," \
"   c_perms      INTEGER," \
"   c_resource   TEXT," \
"      FOREIGN KEY (c_user) REFERENCES t_user(c_id));" \
"" \
"CREATE TABLE t_group_perm (" \
"   c_group      INTEGER," \
"   c_perms      INTEGER," \
"   c_resource   TEXT," \
"      FOREIGN KEY (c_group) REFERENCES t_group(c_id));" \
"" \
"COMMIT;"

#define user_create \
" INSERT INTO t_user (c_email, c_hash) VALUES (#1, '00000000');"

#define user_mod \
" UPDATE t_user SET c_email = #2, "\
"                   c_nick = #3, "\
"                   c_salt = #4, "\
"                   c_hash = #5, "\
"                   c_expiry = 0 "\
" WHERE c_email = #1"

#define STMT(x)      {#x, x }
static const struct {
   const char *name;
   const char *stmt;
} stmts[] ={

   STMT (init_sqlite),

   STMT (create_tables),

   STMT (user_create),
   STMT (user_mod),

};
#undef STMT

static size_t stmts_len = sizeof stmts / sizeof stmts[0];

const char *sqldb_auth_query (const char *qname)
{
   for (size_t i=0; i<stmts_len; i++) {
      if (strcmp (stmts[i].name, qname)==0) {
         return stmts[i].stmt;
      }
   }

#ifdef DEBUG
    fprintf (stderr, "[%s] Statement not found\n", qname);
#endif

   // Invalid statement forces an error to occur.
   return "SQL statement not found";
}

