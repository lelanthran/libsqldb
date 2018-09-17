#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

// TODO: The blob type was not tested!

#include "xerror/xerror.h"
#include "xstring/xstring.h"
#include "util/util.h"

#include "sqlite3/sqlite3.h"

#include "sqldb/sqldb.h"

bool sqldb_create (const char *dbname, sqldb_dbtype_t type)
{
   sqlite3 *newdb = NULL;
   bool ret = false;

   if (type!=sqldb_SQLITE)
      return true;

   if (!dbname)
      return false;

   int mode = (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
   int rc = sqlite3_open_v2 (dbname, &newdb, mode, NULL);
   if (rc!=SQLITE_OK) {
      XERROR ("(%s) Unable to create sqlite file - %s [%m]\n",
               dbname, sqlite3_errstr (rc));
      goto errorexit;
   }

   ret = true;
errorexit:
   if (newdb)
      sqlite3_close (newdb);

   return ret;
}

// TODO: Do we need a lockfile for SQLITE?
struct sqldb_t {
   sqldb_dbtype_t type;
   char *lasterr;

   // Used for sqlite only
   sqlite3 *sqlite_db;

   // Used for postgres only
};

static void db_seterr (sqldb_t *db, const char *msg)
{
   if (!db || !msg)
      return;

   free (db->lasterr);
   db->lasterr = xstr_dup (msg);
}

// A lot of the following functions will be refactored only when working
// on the postgresql integration
static sqldb_t *sqlitedb_open (const char *dbname)
{
   bool error = true;
   sqldb_t *ret = malloc (sizeof *ret);
   if (!ret) {
      MALLOC_ERROR (dbname);
      goto errorexit;
   }
   memset (ret, 0, sizeof *ret);

   ret->type = sqldb_SQLITE;

   int mode = SQLITE_OPEN_READWRITE;
   int rc = sqlite3_open_v2 (dbname, &ret->sqlite_db, mode, NULL);
   if (rc!=SQLITE_OK) {
      const char *tmp =  sqlite3_errstr (rc);
      db_seterr (ret, tmp);
      XERROR ("(%s) Unable to open database: %s\n", dbname, tmp);
      goto errorexit;
   }

   error = false;

errorexit:
   if (error) {
      sqldb_close (ret);
      ret = NULL;
   }
   return ret;
}

sqldb_t *sqldb_open (const char *dbname, sqldb_dbtype_t type)
{
   if (!dbname)
      return NULL;

   switch (type) {
      case sqldb_SQLITE:   return sqlitedb_open (dbname);
      default:             XERROR ("Error: dbtype [%u] is unknown\n", type);
                           return NULL;
   }
   return NULL;
}

void sqldb_close (sqldb_t *db)
{
   if (!db)
      return;

   switch (db->type) {
      case sqldb_SQLITE:   sqlite3_close (db->sqlite_db);            break;
      default:             XERROR ("Unknown type %i\n", db->type);   break;
   }

   free (db->lasterr);
   free (db);
}

const char *sqldb_lasterr (sqldb_t *db)
{
   return db ? db->lasterr : "(NULL SQLDB OBJECT)";
}

void sqldb_clearerr (sqldb_t *db)
{
   if (!db)
      return;

   free (db->lasterr);
   db->lasterr = NULL;
}

static char *fix_string (sqldb_dbtype_t type, const char *string)
{
   char *ret = NULL;
   char r = 0;

   switch (type) {
      case sqldb_SQLITE:      r = '?'; break;
      case sqldb_POSTGRES:    r = '$'; break;
      default:                r = 0;   break;
   }

   if (!r) {
      XERROR ("(%i) Unknown type\n", type);
      return NULL;
   }

   ret = xstr_dup (string);
   if (!ret) {
      MALLOC_ERROR (string);
      return NULL;
   }

   char *tmp = ret;
   while (*tmp) {
      if (*tmp=='#')
         *tmp = r;
      tmp++;
   }

   return ret;
}

struct sqldb_res_t {
   sqldb_dbtype_t type;
   sqldb_t       *dbcon;
   char          *lasterr;

   // For sqlite
   sqlite3_stmt *sqlite_stmt;

   // For postgres
   int current_row;
};

static void res_seterr (sqldb_res_t *res, const char *msg)
{
   if (!res || !msg)
      return;

   free (res->lasterr);
   res->lasterr = xstr_dup (msg);
}

const char *sqldb_res_lasterr (sqldb_res_t *res)
{
   if (!res)
      return NULL;

   return res->lasterr;
}

void sqldb_res_clearerr (sqldb_res_t *res)
{
   if (!res)
      return;

   free (res->lasterr);
   res->lasterr = NULL;
}

uint64_t sqldb_res_last_id (sqldb_res_t *res)
{
   uint64_t ret = 0;

   if (!res)
      return 0;

   switch (res->type) {
      case sqldb_SQLITE:   ret = sqlite3_last_insert_rowid (res->dbcon->sqlite_db);
                           break;
      default:             ret = 0;
                           break;
   }

   return ret;

}

static sqldb_res_t *sqlitedb_exec (sqldb_t *db, char *qstring, va_list *ap)
{
   int counter = 0;
   bool error = true;
   sqldb_res_t *ret = malloc (sizeof *ret);
   if (!ret)
      return NULL;
   memset (ret, 0, sizeof *ret);

   ret->type = sqldb_SQLITE;

   int rc = sqlite3_prepare_v2 (db->sqlite_db, qstring, -1,
                                &ret->sqlite_stmt, NULL);
   if (rc!=SQLITE_OK) {
      const char *tmp = sqlite3_errstr (rc);
      db_seterr (db, tmp);
      XERROR ("Fatal error: %s/%i\n", tmp, rc);
      XERROR ("[%s]\n", qstring);
      goto errorexit;
   }

   sqlite3_stmt *stmt = ret->sqlite_stmt;
   sqldb_coltype_t coltype = va_arg (*ap, sqldb_coltype_t);
   while (coltype!=sqldb_col_UNKNOWN) {
      int32_t *v_int;
      int64_t *v_int64;
      char *v_text;
      void *v_blob;
      void *v_ptr;
      uint32_t *v_bloblen;
      int err = 0;
      int index = counter + 1;

      switch (coltype) {
         case sqldb_col_UNKNOWN:
            XERROR ("Impossible error\n");
            goto errorexit;

         case sqldb_col_UINT32:
         case sqldb_col_INT32:
            v_int = va_arg (*ap, int32_t *);
            err = sqlite3_bind_int (stmt, index, *v_int);
            break;

         case sqldb_col_DATETIME:
         case sqldb_col_UINT64:
         case sqldb_col_INT64:
            v_int64 = va_arg (*ap, int64_t *);
            err = sqlite3_bind_int64 (stmt, index, *v_int64);
            break;

         case sqldb_col_TEXT:
            v_text = va_arg (*ap, char *);
            err = sqlite3_bind_text (stmt, index, v_text,
                                     -1, SQLITE_TRANSIENT);
            break;

         case sqldb_col_BLOB:
            v_blob = va_arg (*ap, void *);
            v_bloblen = va_arg (*ap, uint32_t *);
            err = sqlite3_bind_blob (stmt, index, *(uint8_t **)v_blob,
                                     *v_bloblen, SQLITE_TRANSIENT);
            break;

         case sqldb_col_NULL:
            v_ptr = va_arg (*ap, void *); // Discard the next argument
            v_ptr = v_ptr;
            err = sqlite3_bind_null (stmt, index);
            break;

         default:
            XERROR ("Unknown column type: %u\n", coltype);
            break;
      }

      if (err!=SQLITE_OK) {
         XERROR ("Unable to bind %i\n", index);
         goto errorexit;
      }
      counter++;
      coltype = va_arg (*ap, sqldb_coltype_t);
   }

   error = false;

errorexit:
   if (error) {
      sqldb_res_del (ret);
      ret = NULL;
   }
   return ret;
}

sqldb_res_t *sqldb_exec (sqldb_t *db, const char *query, ...)
{
   sqldb_res_t *ret = NULL;
   va_list ap;

   va_start (ap, query);
   ret = sqldb_execv (db, query, &ap);
   va_end (ap);

   return ret;
}

sqldb_res_t *sqldb_execv (sqldb_t *db, const char *query, va_list *ap)
{
   bool error = true;
   sqldb_res_t *ret = NULL;
   char *qstring = NULL;

   if (!db || !query)
      return NULL;

   qstring = fix_string (db->type, query);
   if (!qstring) {
      MALLOC_ERROR (query);
      goto errorexit;
   }

   sqldb_clearerr (db);

   switch (db->type) {
      case sqldb_SQLITE:   ret = sqlitedb_exec (db, qstring, ap);    break;
      default:             XERROR ("(%i) Unknown type\n", db->type);
                           goto errorexit;
   }
   if (!ret) {
      XERROR ("Failed to execute stmt\n");
      goto errorexit;
   }

   ret->dbcon = db;
   error = false;

errorexit:
   free (qstring);
   if (error) {
      sqldb_res_del (ret);
      ret = NULL;
   }
   return ret;
}

static bool sqlitedb_batch (sqldb_t *db, va_list ap)
{
   bool ret = true;
   if (!db->sqlite_db)
      return false;

   char *qstring = va_arg (ap, char *);

   while (ret && qstring) {
      char *errmsg = NULL;
      int rc = sqlite3_exec (db->sqlite_db, qstring, NULL, NULL, &errmsg);

      if (rc!=SQLITE_OK) {
         ret = false;
         db_seterr (db, errmsg);
      }

      sqlite3_free (errmsg);
      qstring = va_arg (ap, char *);
   }

   return ret;
}

bool sqldb_batch (sqldb_t *db, ...)
{
   bool ret;
   va_list ap;

   if (!db)
      return false;

   va_start (ap, db);

   switch (db->type) {

      case sqldb_SQLITE:   ret = sqlitedb_batch (db, ap); break;

      default:             XERROR ("(%i) Format unsupported\n", db->type);
                           ret = false;
                           break;
   }
   va_end (ap);
   return ret;
}

static int sqlite_res_step (sqldb_res_t *res)
{
   int rc = sqlite3_step (res->sqlite_stmt);

   if (rc==SQLITE_DONE)
      return 0;

   if (rc!=SQLITE_ROW) {
      res_seterr (res, sqlite3_errstr (rc));
      return -1;
   }

   return 1;
}

int sqldb_res_step (sqldb_res_t *res)
{
   int ret = -1;
   if (!res)
      return ret;

   switch (res->type) {
      case sqldb_SQLITE:   ret = sqlite_res_step (res);  break;
      default:             ret = -1;                     break;
   }
   return ret;
}

uint32_t sqlite_scan (sqldb_res_t *res, va_list *ap)
{
   uint32_t ret = 0;

   if (!res)
      return (uint32_t)-1;

   sqlite3_stmt *stmt = res->sqlite_stmt;

   sqldb_coltype_t coltype = va_arg (*ap, sqldb_coltype_t);

   int numcols = sqlite3_column_count (stmt);

   while (coltype!=sqldb_col_UNKNOWN && numcols--) {

      void *dst = va_arg (*ap, void *);
      uint32_t *blen;
      const void *tmp;

      switch (coltype) {

         case sqldb_col_UNKNOWN:
            XERROR ("Possibly corrupt stack");
            return (uint32_t)-1;

         case sqldb_col_UINT32:
         case sqldb_col_INT32:
            *(uint32_t *)dst = sqlite3_column_int (stmt, ret);
            break;

         case sqldb_col_DATETIME:
         case sqldb_col_UINT64:
         case sqldb_col_INT64:
            *(uint64_t *)dst = sqlite3_column_int (stmt, ret);
            break;

         case sqldb_col_TEXT:
            *(char **)dst =
               xstr_dup ((char *)sqlite3_column_text (stmt, ret));
            if (!(*(char **)dst)) {
               MALLOC_ERROR (sqlite3_column_text (stmt, ret));
               return (uint32_t)-1;
            }
            break;

         case sqldb_col_BLOB:
            blen = va_arg (*ap, uint32_t *);
            tmp = sqlite3_column_blob (stmt, ret);
            *blen = sqlite3_column_bytes (stmt, ret);
            *(void **)dst = malloc ((*blen));
            if (!dst) {
               MALLOC_ERROR ("Blob type");
               return (uint32_t)-1;
            }
            memcpy (*(void **)dst, tmp, *blen);
            break;

         case sqldb_col_NULL:
            XERROR ("Error: NULL type not supported\n");
            return (uint32_t)-1;

      }
      coltype = va_arg (*ap, sqldb_coltype_t);
      ret++;
   }

   return ret;
}

uint32_t sqldb_scan_columns (sqldb_res_t *res, ...)
{
   uint32_t ret = 0;
   va_list ap;

   va_start (ap, res);
   ret = sqldb_scan_columnsv (res, &ap);
   va_end (ap);

   return ret;
}

uint32_t sqldb_scan_columnsv (sqldb_res_t *res, va_list *ap)
{
   uint32_t ret = (uint32_t)-1;

   switch (res->type) {
      case sqldb_SQLITE:   ret = sqlite_scan (res, ap);  break;
      default:             ret = (uint32_t)-1;           break;
   }

   return ret;
}

uint32_t sqldb_exec_and_fetch (sqldb_t *db, const char *query, ...)
{
   va_list ap;
   uint32_t ret = (uint32_t)-1;
   sqldb_res_t *res = NULL;

   va_start (ap, query);

   sqldb_clearerr (db);

   res = sqldb_execv (db, query, &ap);
   if (!res) {
      XERROR ("[%s] - sql exection failed: %s\n", query, sqldb_lasterr (db));
      goto errorexit;
   }

   int step = sqldb_res_step (res);
   if (step==0) {
      XERROR ("[%s] - No results!\n", query, sqldb_lasterr (db));
      goto errorexit;
   }
   if (step==-1) {
      XERROR ("[%s] - Error during fetch\n", query, sqldb_lasterr (db));
      goto errorexit;
   }
   if (step!=1) {
      XERROR ("[%s] - Unknown error\n", query, sqldb_lasterr (db));
      goto errorexit;
   }

   ret = sqldb_scan_columnsv (res, &ap);

errorexit:
   sqldb_res_del (res);
   va_end (ap);

   return ret;
}

void sqldb_res_del (sqldb_res_t *res)
{
   if (!res)
      return;

   sqldb_res_clearerr (res);

   switch (res->type) {
      case sqldb_SQLITE:   sqlite3_finalize (res->sqlite_stmt);         break;
      default:             XERROR ("(%i) Unknown type\n", res->type);   break;
   }
   free (res);
}

void sqldb_print (sqldb_t *db, FILE *outf)
{
   if (!outf) {
      outf = stdout;
   }

   if (!db) {
      fprintf (outf, "sqldb_t obvject is NULL\n");
      return;
   }

   fprintf (outf, "%30s: %s\n", "type", db->type==sqldb_SQLITE ?
                                        "SQLITE" : "POSTGRES");
   fprintf (outf, "%30s: %s\n", "lasterr", db->lasterr);
}

