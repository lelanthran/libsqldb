#include <stdbool.h>

#include "sqldb_query.h"
#include "sqldb_queue_query.h"



#define create_tables \
"BEGIN TRANSACTION;" \
\
"CREATE TABLE t_queue (" \
"   c_id             INTEGER PRIMARY KEY," \
"   c_name           TEXT,
"   c_description    TEXT);" \
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
"      FOREIGN KEY (c_user) REFERENCES t_user(c_id) ON DELETE CASCADE," \
"      FOREIGN KEY (c_group) REFERENCES t_group(c_id) ON DELETE CASCADE);" \
"" \
"CREATE TABLE t_user_perm (" \
"   c_resource   TEXT," \
"   c_user       INTEGER," \
"   c_perms      INTEGER," \
"      PRIMARY KEY (c_user, c_resource),"\
"      FOREIGN KEY (c_user) REFERENCES t_user(c_id) ON DELETE CASCADE);" \
 "" \
"CREATE TABLE t_group_perm (" \
"   c_resource   TEXT," \
"   c_group      INTEGER," \
"   c_perms      INTEGER," \
"      PRIMARY KEY (c_group, c_resource),"\
"      FOREIGN KEY (c_group) REFERENCES t_group(c_id) ON DELETE CASCADE);" \
"" \
"COMMIT;"







static bool g_virgin = true;
static struct sqldb_query_t g_queries[] = {
#define QUERY(type,x)     { type, #x, x, 0 }
   QUERY (0, create_tables),
#undef QUERY
};
static const size_t g_queries_len = sizeof g_queries/sizeof g_queries[0];

static void sqldb_queue_query_init (void)
{
   sqldb_query_init (g_queries, g_queries_len);
}

const char *sqldb_queue_query (const char *name)
{
   return sqldb_query_find (g_queries, g_queries_len, 0, name);
}
