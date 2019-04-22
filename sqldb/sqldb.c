#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
#include <time.h>

// TODO: The blob type was not tested!

#include "sqlite3/sqlite3.h"
#include <postgresql/libpq-fe.h>

#include "sqldb/sqldb.h"

static char *lstr_dup (const char *src)
{
   if (!src)
      return NULL;

   size_t len = strlen (src) + 1;

   char *ret = malloc (len);
   if (!ret)
      return NULL;

   return strcpy (ret, src);
}

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
      SQLDB_ERR ("(%s) Unable to create sqlite file - %s [%m]\n",
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
   PGconn *pg_db;
   uint64_t nchanges;
};

static void db_seterr (sqldb_t *db, const char *msg)
{
   if (!db || !msg)
      return;

   char *tmp = lstr_dup (msg);
   if (!tmp)
      return;

   free (db->lasterr);
   db->lasterr = tmp;
}

// A lot of the following functions will be refactored only when working
// on the postgresql integration
static sqldb_t *sqlitedb_open (sqldb_t *ret, const char *dbname)
{
   bool error = true;
   int mode = SQLITE_OPEN_READWRITE;
   int rc = sqlite3_open_v2 (dbname, &ret->sqlite_db, mode, NULL);
   if (rc!=SQLITE_OK) {
      const char *tmp =  sqlite3_errstr (rc);
      db_seterr (ret, tmp);
      SQLDB_ERR ("(%s) Unable to open database: %s\n", dbname, tmp);
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

static sqldb_t *pgdb_open (sqldb_t *ret, const char *dbname)
{
   bool error = false;

   if (!(ret->pg_db = PQconnectdb (dbname))) {
      SQLDB_OOM (dbname);
      goto errorexit;
   }

   if ((PQstatus (ret->pg_db))==CONNECTION_BAD) {
      SQLDB_ERR ("[%s] Connection failure: [%s]\n",
                     dbname,
                     PQerrorMessage (ret->pg_db));
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
   sqldb_t *ret = malloc (sizeof *ret);
   if (!ret) {
      SQLDB_OOM (dbname);
      goto errorexit;
   }

   memset (ret, 0, sizeof *ret);

   ret->type = type;

   if (!dbname)
      goto errorexit;

   switch (type) {
      case sqldb_SQLITE:   return sqlitedb_open (ret, dbname);
      case sqldb_POSTGRES: return pgdb_open (ret, dbname);
      default:             SQLDB_ERR ("Error: dbtype [%u] is unknown\n", type);
                           goto errorexit;
   }

errorexit:
   free (ret);
   return NULL;
}

void sqldb_close (sqldb_t *db)
{
   if (!db)
      return;

   switch (db->type) {
      case sqldb_SQLITE:   sqlite3_close (db->sqlite_db);               break;
      case sqldb_POSTGRES: PQfinish (db->pg_db);                        break;
      default:             SQLDB_ERR ("Unknown type %i\n", db->type);   break;
   }

   free (db->lasterr);
   memset (db, 0, sizeof *db);
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
      SQLDB_ERR ("(%i) Unknown type\n", type);
      return NULL;
   }

   ret = lstr_dup (string);
   if (!ret) {
      SQLDB_OOM (string);
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
   PGresult *pgr;
   int current_row;
   int nrows;
   uint64_t last_id;
};

static void res_seterr (sqldb_res_t *res, const char *msg)
{
   if (!res || !msg)
      return;

   free (res->lasterr);
   res->lasterr = lstr_dup (msg);
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

uint64_t sqldb_count_changes (sqldb_t *db)
{
   uint64_t ret = 0;

   if (!db)
      return 0;

   switch (db->type) {
      case sqldb_SQLITE:   ret = sqlite3_changes (db->sqlite_db);
                           break;

      case sqldb_POSTGRES: ret = db->nchanges;
                           break;

      default:             ret = 0;
                           break;
   }

   return ret;
}

uint64_t sqldb_res_last_id (sqldb_res_t *res)
{
   uint64_t ret = 0;

   if (!res)
      return 0;

   switch (res->type) {
      case sqldb_SQLITE:   ret = sqlite3_last_insert_rowid (res->dbcon->sqlite_db);
                           break;

      case sqldb_POSTGRES: ret = res->last_id;
                           break;

      default:             ret = 0;
                           break;
   }

   return ret;
}

uint64_t sqldb_res_num_columns (sqldb_res_t *res)
{
   if (!res)
      return 0;

   uint64_t ret = 0;

   switch (res->type) {
      case  sqldb_SQLITE:  ret = sqlite3_column_count (res->sqlite_stmt);
                           break;

      case sqldb_POSTGRES: ret = PQnfields (res->pgr);
                           break;
   }

   return ret;
}

char **sqldb_res_column_names (sqldb_res_t *res)
{
   bool error = true;
   char **ret = NULL;
   uint64_t ncols = 0;

   if (!res)
      goto errorexit;

   if (!(ncols = sqldb_res_num_columns (res)))
      goto errorexit;

   if (!(ret = malloc ((sizeof *ret) * (ncols + 1))))
      goto errorexit;

   memset (ret, 0, (sizeof *ret) * (ncols + 1));

   for (uint64_t i=0; i<ncols; i++) {
      char *tmp = NULL;
      switch (res->type) {
         case  sqldb_SQLITE:  tmp = sqlite3_column_name (res->sqlite_stmt, i);
                              break;

         case sqldb_POSTGRES: tmp = PQfname (res->pgr, i);
                              break;
      }
      if (!(ret[i] = lstr_dup (tmp)))
         goto errorexit;
   }

   error = false;

errorexit:
   if (error) {
      for (size_t i=0; ret && ret[i]; i++)
         free (ret[i]);

      free (ret);
      ret = NULL;
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
      SQLDB_ERR ("Fatal error: %s/%i\n", tmp, rc);
      SQLDB_ERR ("[%s]\n", qstring);
      goto errorexit;
   }

   sqlite3_stmt *stmt = ret->sqlite_stmt;
   sqldb_coltype_t coltype = va_arg (*ap, sqldb_coltype_t);
   while (coltype!=sqldb_col_UNKNOWN) {
      int32_t *v_int;
      int64_t *v_int64;
      char **v_text;
      void *v_blob;
      void *v_ptr;
      uint32_t *v_bloblen;
      int err = 0;
      int index = counter + 1;

      switch (coltype) {
         case sqldb_col_UNKNOWN:
            SQLDB_ERR ("Impossible error\n");
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
            v_text = va_arg (*ap, char **);
            err = sqlite3_bind_text (stmt, index, (*v_text),
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
            SQLDB_ERR ("Unknown column type: %u\n", coltype);
            break;
      }

      if (err!=SQLITE_OK) {
         SQLDB_ERR ("Unable to bind %i\n", index);
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

static sqldb_res_t *pgdb_exec (sqldb_t *db, char *qstring, va_list *ap)
{
   bool error = true;
   int nParams = 0;
   sqldb_res_t *ret = NULL;

   char **paramValues = NULL;

   static const Oid *paramTypes = NULL;
   static const int *paramLengths = NULL;
   static const int *paramFormats = NULL;

   va_list ac;
   va_copy (ac, (*ap));
   sqldb_coltype_t coltype = va_arg (ac, sqldb_coltype_t);
   while (coltype!=sqldb_col_UNKNOWN) {
      nParams++;
      const void *ignore = va_arg (ac, const void *);
      ignore = ignore;
      coltype = va_arg (ac, sqldb_coltype_t);
   }
   va_end (ac);


   ret = malloc (sizeof *ret);
   if (!ret)
      goto errorexit;
   memset (ret, 0, sizeof *ret);

   paramValues = malloc ((sizeof *paramValues) * (nParams + 1));
   if (!paramValues)
      goto errorexit;
   memset (paramValues, 0, (sizeof *paramValues) * (nParams + 1));

   ret->type = sqldb_POSTGRES;

   size_t index = 0;
   coltype = va_arg (*ap, sqldb_coltype_t);
   while (coltype!=sqldb_col_UNKNOWN) {
      uint32_t *v_uint;
      uint64_t *v_uint64;
      int32_t *v_int;
      int64_t *v_int64;
      char **v_text;
      void *v_blob;
      void *v_ptr;
      uint32_t *v_bloblen;

      char intstr[42];
      char *value = intstr;

      switch (coltype) {
         case sqldb_col_UNKNOWN:
            SQLDB_ERR ("Impossible error\n");
            goto errorexit;

         case sqldb_col_UINT32:
            v_uint = va_arg (*ap, uint32_t *);
            sprintf (intstr, "%i", *v_uint);
            break;

         case sqldb_col_INT32:
            v_int = va_arg (*ap, int32_t *);
            sprintf (intstr, "%i", *v_int);
            break;

         case sqldb_col_UINT64:
            v_uint64 = va_arg (*ap, uint64_t *);
            sprintf (intstr, "%" PRIu64, *v_uint64);
            break;

         case sqldb_col_INT64:
            v_int64 = va_arg (*ap, int64_t *);
            sprintf (intstr, "%" PRIi64, *v_int64);
            break;

         case sqldb_col_DATETIME:
            // TODO: ???

         case sqldb_col_TEXT:
            v_text = va_arg (*ap, char **);
            value = *v_text;
            break;

         case sqldb_col_BLOB:
            v_blob = va_arg (*ap, void *);
            v_bloblen = va_arg (*ap, uint32_t *);
            // TODO: ???
            v_bloblen = v_bloblen;
            v_blob = v_blob;
            value = NULL;
            break;

         case sqldb_col_NULL:
            v_ptr = va_arg (*ap, void *); // Discard the next argument
            v_ptr = v_ptr;
            value = NULL;
            break;

         default:
            SQLDB_ERR ("Unknown column type: %u\n", coltype);
            break;
      }

      paramValues[index++] = lstr_dup (value);
      coltype = va_arg (*ap, sqldb_coltype_t);
   }

   ret->pgr = PQexecParams (db->pg_db, qstring, nParams,
                                                paramTypes,
                           (const char *const *)paramValues,
                                                paramLengths,
                                                paramFormats,
                                                0);
   if (!ret->pgr) {
      db_seterr (db, "OOM error (pg)");
      goto errorexit;
   }

   ExecStatusType rs = PQresultStatus (ret->pgr);
   if (rs != PGRES_COMMAND_OK && rs != PGRES_TUPLES_OK) {
      db_seterr (db, PQresultErrorMessage (ret->pgr));
      goto errorexit;
   }

   ret->current_row = 0;
   ret->nrows = PQntuples (ret->pgr);
   const char *tmp = PQcmdTuples (ret->pgr);
   if (tmp) {
      sscanf (tmp, "%" PRIu64, &db->nchanges);
   } else {
      db->nchanges = 0;
   }

   Oid last_id = PQoidValue (ret->pgr);
   ret->last_id = last_id == InvalidOid ? (uint64_t)-1 : last_id;

   error = false;

errorexit:

   for (int i=0; paramValues && i<nParams; i++) {
      free (paramValues[i]);
   }
   free (paramValues);

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
      SQLDB_OOM (query);
      goto errorexit;
   }

   sqldb_clearerr (db);

   switch (db->type) {
      case sqldb_SQLITE:   ret = sqlitedb_exec (db, qstring, ap);       break;
      case sqldb_POSTGRES: ret = pgdb_exec (db, qstring, ap);           break;
      default:             SQLDB_ERR ("(%i) Unknown type\n", db->type);
                           goto errorexit;
   }
   if (!ret) {
      SQLDB_ERR ("Failed to execute stmt [%s]\n", qstring);
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

      default:             SQLDB_ERR ("(%i) Format unsupported\n", db->type);
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

static int pgdb_res_step (sqldb_res_t *res)
{
   return res->nrows - res->current_row ? 1 : 0;
}

int sqldb_res_step (sqldb_res_t *res)
{
   int ret = -1;
   if (!res)
      return ret;

   switch (res->type) {
      case sqldb_SQLITE:   ret = sqlite_res_step (res);  break;
      case sqldb_POSTGRES: ret = pgdb_res_step (res);    break;
      default:             ret = -1;                     break;
   }
   return ret;
}

static uint64_t convert_ISO8601_to_uint64 (const char *src)
{
   struct tm tm;

   memset (&tm, 0, sizeof tm);

   if ((sscanf (src, "%i-%i-%i%i:%i:%i", &tm.tm_year,
                                          &tm.tm_mon,
                                          &tm.tm_mday,
                                          &tm.tm_hour,
                                          &tm.tm_min,
                                          &tm.tm_sec)) != 6)
      return (uint64_t)-1;

   tm.tm_year -= 1900;
   tm.tm_mon -= 1;

   return mktime (&tm);
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
            SQLDB_ERR ("Possibly corrupt stack");
            return (uint32_t)-1;

         case sqldb_col_UINT32:
         case sqldb_col_INT32:
            *(uint32_t *)dst = sqlite3_column_int (stmt, ret);
            break;

         case sqldb_col_DATETIME:
            tmp = sqlite3_column_text (stmt, ret);
            *(uint64_t *)dst = convert_ISO8601_to_uint64 (tmp);
            if ((*(uint64_t *)dst) == (uint64_t)-1)
               return (uint32_t)-1;
            break;

         case sqldb_col_UINT64:
         case sqldb_col_INT64:
            *(uint64_t *)dst = sqlite3_column_int (stmt, ret);
            break;

         case sqldb_col_TEXT:
            *(char **)dst =
               lstr_dup ((char *)sqlite3_column_text (stmt, ret));
            if (!(*(char **)dst)) {
               SQLDB_OOM (sqlite3_column_text (stmt, ret));
               return (uint32_t)-1;
            }
            break;

         case sqldb_col_BLOB:
            blen = va_arg (*ap, uint32_t *);
            tmp = sqlite3_column_blob (stmt, ret);
            *blen = sqlite3_column_bytes (stmt, ret);
            *(void **)dst = malloc ((*blen));
            if (!dst) {
               SQLDB_OOM ("Blob type");
               return (uint32_t)-1;
            }
            memcpy (*(void **)dst, tmp, *blen);
            break;

         case sqldb_col_NULL:
            SQLDB_ERR ("Error: NULL type not supported\n");
            return (uint32_t)-1;

      }
      coltype = va_arg (*ap, sqldb_coltype_t);
      ret++;
   }

   return ret;
}

uint32_t pgdb_scan (sqldb_res_t *res, va_list *ap)
{
   uint32_t ret = 0;

   if (!res)
      return (uint32_t)-1;

   sqldb_coltype_t coltype = va_arg (*ap, sqldb_coltype_t);

   int numcols = PQnfields (res->pgr);
   int index = 0;

   while (coltype!=sqldb_col_UNKNOWN && numcols--) {

      void *dst = va_arg (*ap, void *);

      int32_t  i32;
      uint32_t u32;
      int64_t  i64;
      uint64_t u64;

      const char *value = PQgetvalue (res->pgr, res->current_row, index++);
      if (!value)
         return (uint32_t)-1;

      switch (coltype) {

         case sqldb_col_UNKNOWN:
            SQLDB_ERR ("Possibly corrupt stack");
            return (uint32_t)-1;

         case sqldb_col_UINT32:
            if ((sscanf (value, "%u", &u32))!=1)
               return (uint32_t)-1;
            *(uint32_t *)dst = u32;
            break;

         case sqldb_col_INT32:
            if ((sscanf (value, "%i", &i32))!=1)
               return (uint32_t)-1;
            *(uint32_t *)dst = i32;
            break;

         case sqldb_col_UINT64:
            if ((sscanf (value, "%" PRIu64, &u64))!=1)
               return (uint32_t)-1;
            *(uint32_t *)dst = u64;
            break;

         case sqldb_col_INT64:
            if ((sscanf (value, "%" PRIu64, &i64))!=1)
               return (uint32_t)-1;
            *(uint32_t *)dst = i64;
            break;

         case sqldb_col_TEXT:
            if ((*(char **)dst = lstr_dup (value))==NULL)
               return (uint32_t)-1;
            break;

         case sqldb_col_DATETIME:
            *(uint64_t *)dst = convert_ISO8601_to_uint64 (value);
            if ((*(uint64_t *)dst) == (uint64_t)-1)
               return (uint32_t)-1;
            break;

         case sqldb_col_BLOB:
            SQLDB_ERR ("Error: BLOB type not supported\n");
            return (uint32_t)-1;

         case sqldb_col_NULL:
            SQLDB_ERR ("Error: NULL type not supported\n");
            return (uint32_t)-1;

      }
      coltype = va_arg (*ap, sqldb_coltype_t);
      ret++;
   }

   res->current_row++;
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
      case sqldb_POSTGRES: ret = pgdb_scan (res, ap);    break;
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
      SQLDB_ERR ("[%s] - sql exection failed: %s\n", query, sqldb_lasterr (db));
      goto errorexit;
   }

   int step = sqldb_res_step (res);
   if (step==0) {
      SQLDB_ERR ("[%s] - No results (%s)!\n", query, sqldb_lasterr (db));
      goto errorexit;
   }
   if (step==-1) {
      SQLDB_ERR ("[%s] - Error during fetch (%s)\n", query, sqldb_lasterr (db));
      goto errorexit;
   }
   if (step!=1) {
      SQLDB_ERR ("[%s] - Unknown error (%s)\n", query, sqldb_lasterr (db));
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
      case sqldb_POSTGRES: PQclear (res->pgr);                          break;
      default:             SQLDB_ERR ("(%i) Unknown type\n", res->type);   break;
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

