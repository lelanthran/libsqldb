
#include <string.h>

#include "xerror/xerror.h"
#include "xstring/xstring.h"

#include "sqldb_auth/sqldb_auth_query.h"


#define init_sqlite \
   "PRAGMA foreign_keys = ON;"

#define create_tables \
"BEGIN TRANSACTION;" \
 \
/* Database and application admin  */\
"create table tbl_version (" \
"  element  TEXT PRIMARY KEY, " \
"  version  INTEGER);" \
\
"create table tbl_config (" \
"  cname    TEXT PRIMARY KEY, " \
"  cvalue   TEXT," \
"  cdescr   TEXT);" \
\
"create table tbl_staged (" \
"  operation   TEXT, " \
"  parent_path TEXT," \
"  path        TEXT," \
"  hash        TEXT," \
"  PRIMARY KEY (operation, parent_path, path, hash));"\
\
/* Files and Dirs  */\
"create table tbl_path (" \
"  id          INTEGER PRIMARY KEY," \
"  revision    INTEGER NOT NULL," \
"  version     INTEGER NOT NULL," \
"  path        TEXT NOT NULL);" \
\
"create table tbl_dir (" \
"  id          INTEGER PRIMARY KEY," \
"  revision    INTEGER NOT NULL," \
"  version     INTEGER NOT NULL," \
"  dir_path    INTEGER NOT NULL," \
"  parent_path INTEGER NOT NULL," \
"  flags       INTEGER NOT NULL," \
"     foreign key (dir_path) references tbl_path(id),"\
"     foreign key (parent_path) references tbl_path(id));"\
\
"create table tbl_file (" \
"  id          INTEGER PRIMARY KEY," \
"  revision    INTEGER NOT NULL," \
"  version     INTEGER NOT NULL," \
"  file_path   INTEGER NOT NULL," \
"  real_path   TEXT NOT NULL," \
"  phase       TEXT NOT NULL," \
"  state       TEXT NOT NULL," \
"  flags       INTEGER NOT NULL," \
"     foreign key (file_path) references tbl_path(id));"\
\
/* Users and groups  */\
"create table tbl_uuser (" \
"  id       INTEGER PRIMARY KEY," \
"  email    TEXT UNIQUE NOT NULL," \
"  version  INTEGER NOT NULL);" \
\
"create table tbl_uuser_perm ("\
"  resource TEXT," \
"  uuser    INTEGER NOT NULL," \
"  flags    INTEGER NOT NULL," \
"  version  INTEGER NOT NULL," \
"     PRIMARY KEY (resource, uuser),"\
"     foreign key (uuser) references tbl_uuser(id));"\
\
\
"create table tbl_ugroup (" \
"  id       INTEGER PRIMARY KEY," \
"  name     TEXT UNIQUE NOT NULL," \
"  owner    INTEGER NOT NULL," \
"  descr    TEXT," \
"  version  INTEGER NOT NULL," \
"     foreign key (owner) references tbl_uuser(id));"\
\
\
"create table tbl_uuser_ugroup_map (" \
"  uuser    INTEGER NOT NULL," \
"  ugroup   INTEGER NOT NULL," \
"  version  INTEGER NOT NULL," \
"     foreign key (uuser) references tbl_uuser(id),"\
"     foreign key (ugroup) references tbl_ugroup(id));"\
\
\
/* Permissions  */\
"create table tbl_ugroup_perm ("\
"  resource TEXT," \
"  ugroup   INTEGER NOT NULL," \
"  flags    INTEGER NOT NULL," \
"  version  INTEGER NOT NULL," \
"     PRIMARY KEY (resource, ugroup),"\
"     foreign key (ugroup) references tbl_ugroup(id));"\
\
\
/* Create the default values */\
" INSERT INTO tbl_version values ('repo_version',         0);"\
" INSERT INTO tbl_version values ('tbl_config',           0);"\
" INSERT INTO tbl_version values ('tbl_path',             0);"\
" INSERT INTO tbl_version values ('tbl_dir',              0);"\
" INSERT INTO tbl_version values ('tbl_file',             0);"\
" INSERT INTO tbl_version values ('tbl_uuser',            0);"\
" INSERT INTO tbl_version values ('tbl_uuser_perm',       0);"\
" INSERT INTO tbl_version values ('tbl_ugroup',           0);"\
" INSERT INTO tbl_version values ('tbl_uuser_ugroup_map', 0);"\
" INSERT INTO tbl_version values ('tbl_ugroup_perm',      0);"\
\
" INSERT INTO tbl_config values ('email', '', "\
"  'The email address you use to login to this repository');"\
\
" INSERT INTO tbl_config values ('password', '', "\
"  'The password you use to login to this repository."\
" Note that this must only be set with the password command');"\
\
" INSERT INTO tbl_config values ('salt', '', "\
"  'The salt used to hash the password."\
" Note that this must only be set with the password command');"\
\
" INSERT INTO tbl_config values ('remote', '', "\
"  'The server url in the form of \"protocol://domain:port/path\".');"\
\
" INSERT INTO tbl_config values ('proxy', '', "\
"  'The proxy url in the form of ip-address:port.');"\
\
" INSERT INTO tbl_config values ('proxy-credentials', '', "\
"  'The credentials for the proxy in the form of username:password.');"\
\
" INSERT INTO tbl_config values ('proxy-auth', '', "\
"  'The authentication for the proxy: basic, digest, ntlm or negotiate.');"\
\
" INSERT INTO tbl_config values ('proxy-type', '', "\
"  'The type of proxy: none, http or socks4.');"\
\
"COMMIT;"



#define STMT(x)      {#x, x }
static const struct {
   const char *name;
   const char *stmt;
} stmts[] ={

   STMT (init_sqlite),

   STMT (create_tables),

};
#undef STMT

static size_t stmts_len = sizeof stmts / sizeof stmts[0];

const char *sqldb_auth_query (const char *qname);
{
   for (size_t i=0; i<stmts_len; i++) {
      if (strcmp (stmts[i].name, name)==0) {
         return stmts[i].stmt;
      }
   }

#ifdef DEBUG
    fprintf (stderr, "[%s] Statement not found\n", name);
#endif

   return "error"; // Invalid statement forces an error to occur.
}


