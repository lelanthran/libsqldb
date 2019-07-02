
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
"   c_flags      INTEGER," \
"   c_salt       TEXT," \
"   c_hash       TEXT);" \
"" \
"CREATE TABLE t_group (" \
"   c_id            INTEGER PRIMARY KEY," \
"   c_name          TEXT UNIQUE," \
"   c_description   TEXT);" \
"" \
"CREATE TABLE t_group_membership (" \
"   c_user    INTEGER NOT NULL," \
"   c_group   INTEGER NOT NULL," \
"      PRIMARY KEY (c_user, c_group),"\
"      FOREIGN KEY (c_user) REFERENCES t_user(c_id)," \
"      FOREIGN KEY (c_group) REFERENCES t_group(c_id));" \
"" \
"CREATE TABLE t_user_perm (" \
"   c_resource   TEXT," \
"   c_user       INTEGER," \
"   c_perms      INTEGER," \
"      PRIMARY KEY (c_user, c_resource),"\
"      FOREIGN KEY (c_user) REFERENCES t_user(c_id));" \
 "" \
"CREATE TABLE t_group_perm (" \
"   c_resource   TEXT," \
"   c_group      INTEGER," \
"   c_perms      INTEGER," \
"      PRIMARY KEY (c_group, c_resource),"\
"      FOREIGN KEY (c_group) REFERENCES t_group(c_id));" \
"" \
"COMMIT;"

///////////////////////////////////////////////////////////////////

#define user_create \
" INSERT INTO t_user (c_email, c_hash) VALUES (#1, '00000000');"

#define user_mod \
" UPDATE t_user SET c_email = #2, "\
"                   c_nick = #3, "\
"                   c_salt = #4, "\
"                   c_hash = #5, "\
"                   c_flags = #6, "\
"                   c_expiry = 0 "\
" WHERE c_email = #1;"

#define user_rm \
" DELETE FROM t_user WHERE c_email = #1;"

#define user_info \
" SELECT c_id, c_nick, c_session, c_flags FROM t_user WHERE c_email = #1;"

///////////////////////////////////////////////////////////////////

#define group_create \
" INSERT INTO t_group (c_name, c_description) VALUES (#1, #2);"

#define group_rm \
" DELETE FROM t_group WHERE c_name = #1;"

#define group_info \
" SELECT c_id, c_description FROM t_group WHERE c_name = #1;"

#define group_mod \
" UPDATE t_group SET c_name = #2, "\
"                    c_description = #3 "\
" WHERE c_name = #1;"

///////////////////////////////////////////////////////////////////

#define group_adduser \
"INSERT INTO t_group_membership (c_user, c_group) "\
"  SELECT t_user.c_id, t_group.c_id FROM t_user, t_group "\
"WHERE "\
"     t_user.c_email=#1 "\
"AND "\
"     t_group.c_name=#2 "\
" ;"

#define group_rmuser \
"DELETE FROM t_group_membership "\
"WHERE "\
"     c_group = (SELECT c_id FROM t_group WHERE c_name=#1) "\
"AND "\
"     c_user = (SELECT c_id FROM t_user WHERE c_email=#2);"

#define group_membership \
"SELECT t_user.c_email, t_user.c_nick, t_user.c_id, t_user.c_flags "\
"FROM t_user, t_group_membership, t_group "\
"WHERE "\
"     t_group.c_name=#1 "\
"AND  t_user.c_id = t_group_membership.c_user "\
"AND  t_group_membership.c_group = t_group.c_id;"

///////////////////////////////////////////////////////////////////

#define perms_user_grant \
"INSERT INTO t_user_perm (c_resource, c_user, c_perms) "\
" VALUES ("\
"  #1, "\
"  (SELECT c_id FROM t_user WHERE c_email=#2), "\
"  #3) "\
" ON CONFLICT (c_user, c_resource) DO UPDATE "\
"  SET c_perms = c_perms | #3 "\
" WHERE c_resource = #1 "\
"  AND c_user = (SELECT c_id FROM t_user WHERE c_email=#2);"

#define perms_user_revoke \
"UPDATE t_user_perm "\
" SET c_perms = c_perms & ~#3 "\
" WHERE c_resource = #1 "\
"  AND c_user = (SELECT c_id FROM t_user WHERE c_email=#2);"

#define perms_group_grant \
"INSERT INTO t_group_perm (c_resource, c_group, c_perms) "\
" VALUES ("\
"  #1, "\
"  (SELECT c_id FROM t_group WHERE c_name=#2), "\
"  #3) "\
" ON CONFLICT (c_group, c_resource) DO UPDATE "\
"  SET c_perms = c_perms | #3 "\
" WHERE c_resource = #1 "\
"  AND c_group = (SELECT c_id FROM t_group WHERE c_name=#2);"

#define perms_group_revoke \
"UPDATE t_group_perm "\
" SET c_perms = c_perms & ~#3 "\
" WHERE c_resource = #1 "\
"  AND c_group = (SELECT c_id FROM t_group WHERE c_name=#2);"

#define perms_get_user \
"SELECT c_perms FROM t_user_perm "\
"WHERE c_user = (SELECT c_id FROM t_user WHERE c_email=#1) "\
"AND   c_resource = #2;"

#define perms_get_group \
"SELECT c_perms FROM t_group_perm "\
"WHERE c_group = (SELECT c_id FROM t_group WHERE c_name=#1) "\
"AND   c_resource = #2;"

#define perms_get_all \
"SELECT c_perms FROM t_group_perm "\
"WHERE c_resource = #2 "\
"AND "\
"   c_group IN "\
"      (SELECT c_group FROM t_group_membership "\
"      WHERE c_user = "\
"         (SELECT c_id FROM t_user "\
"         WHERE c_email=#1));"


///////////////////////////////////////////////////////////////////

#define STMT(x)      {#x, x }
static const struct {
   const char *name;
   const char *stmt;
} stmts[] ={

   STMT (init_sqlite),

   STMT (create_tables),

   STMT (user_create),
   STMT (user_mod),
   STMT (user_rm),
   STMT (user_info),

   STMT (group_create),
   STMT (group_mod),
   STMT (group_rm),
   STMT (group_info),

   STMT (group_adduser),
   STMT (group_rmuser),
   STMT (group_membership),

   STMT (perms_user_grant),
   STMT (perms_user_revoke),
   STMT (perms_group_grant),
   STMT (perms_group_revoke),
   STMT (perms_get_user),
   STMT (perms_get_group),
   STMT (perms_get_all),
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

