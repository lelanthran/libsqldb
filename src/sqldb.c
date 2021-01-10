#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
#include <time.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

// TODO: The blob type was not tested!

#include "sqlite3.h"
#include <postgresql/libpq-fe.h>

#include "sqldb.h"

#define SQLDB_OOM(s)          fprintf (stderr, "OOM [%s]\n", s)

#ifdef DEBUG
#define PROG_ERR(...)      do {\
      fprintf (stderr, "%s:%d: ", __FILE__, __LINE__);\
      fprintf (stderr, __VA_ARGS__);\
} while (0)
#else
#define PROG_ERR(...)
#endif


static char *lstr_dup (const char *src)
{
   if (!src)
      src = "";

   size_t len = strlen (src) + 1;

   char *ret = malloc (len);
   if (!ret)
      return NULL;

   return strcpy (ret, src);
}

static char *lstr_cat (const char *s1, const char *s2)
{
   size_t nbytes = 0;

   if (s1)
      nbytes += strlen (s1);

   if (s2)
      nbytes += strlen (s2);

   nbytes++;

   char *ret = malloc (nbytes);
   if (!ret) {
      return NULL;
   }

   strcpy (ret, s1);
   strcat (ret, s2);

   return ret;
}

static bool valid_db_char (char c)
{
   static const char *valid =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz"
      "0123456789"
      "_./";
   return (strchr (valid, c));
}

static bool valid_db_identifier (const char *ident)
{
   for (size_t i=0; ident && ident[i]; i++) {
      if (!(valid_db_char (ident[i])))
         return false;
   }
   return true;
}


static bool row_append (char ***dst, size_t dstlen, const char *string)
{
   if (!dst || !string)
      return false;

   size_t newlen = dstlen + 2;
   char **tmp = realloc (*dst, sizeof *tmp * newlen);
   if (!tmp)
      return false;

   *dst = tmp;

   tmp[dstlen+1] = NULL;
   if (!(tmp[dstlen] = lstr_dup (string)))
      return false;

   return true;
}

static bool matrix_append (char ****dst, size_t dstlen, char **row)
{
   if (!dst || !row)
      return false;

   size_t newlen = dstlen + 2;
   char ***tmp = realloc (*dst, sizeof *tmp * newlen);
   if (!tmp)
      return false;

   *dst = tmp;

   tmp[dstlen+1] = NULL;
   tmp[dstlen] = row;

   return true;
}

/* *****************************************************************
 * Generating some entropy: see https://nullprogram.com/blog/2019/04/30/
 */

static uint32_t hash_buffer (uint32_t current, void *buf, size_t len)
{
   uint8_t *b = buf;
   for (size_t i=0; i<len; i++) {
      current ^= (current * 31) + b[i];
   }
   current = current ^ (current >> 15);
   current = current ^ (current * 0x4e85ca7d);
   current = current ^ (current >> 15);
   current = current ^ (current * 0x4e85ca7d);
   return current;
}

static uint32_t getclock (uint32_t current)
{
   for (size_t i=0; i<0x000f; i++) {
      // Clock may not have enough resolution over repeated calls, so
      // add some entropy by counting how long it takes to change
      clock_t start = clock ();
      uint16_t counter = 0;
      while (start == clock()) {
         counter++;
      }
      current = hash_buffer (current, &counter, sizeof counter);
      current = hash_buffer (current, &start, sizeof start);
   }
   return current;
}

uint32_t sqldb_random_seed (void)
{
#ifndef PLATFORM_Windows
   extern int main (void);
#endif

   uint32_t ret = 0;

   /* Sources of randomness:
    * 1. Epoch in seconds since 1970:
    * 2. Processor time since program start:
    * 3. Pointer to current stack position:
    * 4. Pointer to heap:
    * 5. Pointer to function (self):
    * 6. Pointer to main():
    * 7. PID:
    * 8. mkstemp()
    */

   time_t epoch = time (NULL);
   uint32_t proc_time = getclock (ret);
   uint32_t *ptr_stack = &ret;
   uint32_t (*ptr_self) (void) = sqldb_random_seed;
#ifndef PLATFORM_Windows
   int (*ptr_main) (void) = main;
#else
   int (*ptr_main) (void) = NULL;
#endif
   void *(*ptr_malloc) (size_t) = malloc;
   pid_t pid = getpid ();

   uint32_t int_stack = (uintptr_t)ptr_stack;
   uint32_t *tmp = malloc (1);
   uint32_t *ptr_heap_small = tmp ? tmp :
                              (void *) (uintptr_t) ((int_stack >> 1) * (int_stack >> 1));
   free (tmp);
   tmp = malloc (1024 * 1024);
   uint32_t *ptr_heap_large = tmp ? tmp :
                              (void *) (uintptr_t) ((proc_time >> 1) * (proc_time >> 1));
   free (tmp);

   char tmp_fname[] = "tmpfileXXXXXX";
   int fd = mkstemp (tmp_fname);
   if (fd >= 0) {
      close (fd);
      if ((remove (tmp_fname))!=0) {
         PROG_ERR ("Failed to remove temporary file [%s]: %m\n"
                   "Please delete this file manually\n", tmp_fname);
      }
   }

   ret = hash_buffer (ret, &epoch, sizeof epoch);
   ret = hash_buffer (ret, &proc_time, sizeof proc_time);
   ret = hash_buffer (ret, &ptr_stack, sizeof ptr_stack);
   ret = hash_buffer (ret, &ptr_self, sizeof ptr_self);
   ret = hash_buffer (ret, &ptr_main, sizeof ptr_main);
   ret = hash_buffer (ret, &ptr_malloc, sizeof ptr_malloc);
   ret = hash_buffer (ret, &pid, sizeof pid);
   ret = hash_buffer (ret, &ptr_heap_small, sizeof ptr_heap_small);
   ret = hash_buffer (ret, &ptr_heap_large, sizeof ptr_heap_large);
   ret = hash_buffer (ret, tmp_fname, strlen (tmp_fname));

#if 0
   fprintf (stderr, "[%p][%p][%p][%p][%p][%p]\n",
                                          ptr_stack,
                                          ptr_self,
                                          ptr_main,
                                          ptr_malloc,
                                          ptr_heap_small,
                                          ptr_heap_large);
#endif
   return ret;
}

void sqldb_random_bytes (void *dst, size_t len)
{
   static uint64_t seed = 0;

#if 0
   /* TEST CASE: Used to test unique constraints on session tables */
   memset (dst, 0, len);
   return;
#endif

   if (!seed) {
      seed = sqldb_random_seed ();
      seed <<= 4;
      seed |= sqldb_random_seed ();
      srand ((unsigned int)seed);
   }

   uint8_t *b = dst;
   for (size_t i=0; i<len; i++) {
      b[i] = (rand () >> 8) & 0xff;
   }
}

/* ******************************************************************* */
static bool sqlitedb_create (const char *dbname)
{
   sqlite3 *newdb = NULL;
   bool ret = false;
   struct stat sb;

   if (!dbname)
      return false;

   memset (&sb, 0, sizeof sb);
   if ((stat (dbname, &sb))==0) {
      PROG_ERR ("File [%s] exists; refusing to overwrite\n", dbname);
      goto errorexit;
   }

   int mode = (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
   int rc = sqlite3_open_v2 (dbname, &newdb, mode, NULL);
   if (rc!=SQLITE_OK) {
      PROG_ERR ("(%s) Unable to create sqlite file - %s [%m]\n",
                dbname, sqlite3_errstr (rc));
      goto errorexit;
   }

   ret = true;
errorexit:
   if (newdb)
      sqlite3_close (newdb);

   return ret;
}

static bool pgdb_create (sqldb_t *db, const char *dbname)
{
   if (!dbname || !db)
      return false;

   char *qstring = lstr_cat ("CREATE DATABASE ", dbname);
   bool rc = sqldb_batch (db, qstring, NULL);
   free (qstring);
   return rc;
}

bool sqldb_create (sqldb_t *db, const char *dbname, sqldb_dbtype_t type)
{
   if (!(valid_db_identifier (dbname))) {
      PROG_ERR ("[%s] is not a valid database name\n", dbname);
      return false;
   }

   switch (type) {

      case sqldb_SQLITE:   return sqlitedb_create (dbname);

      case sqldb_POSTGRES: return pgdb_create (db, dbname);

      default:
         return false;
   }
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


static void err_printf (char **dst, const char *fmts, va_list ap)
{
   va_list apc;
   char *tmp = NULL;
   size_t len = 0;

   if (!dst || !fmts)
      return;

   va_copy (apc, ap);

   if (!(len = vsnprintf (tmp, 0, fmts, apc)))
      return;

   if (!(tmp = malloc (len + 1))) {
      PROG_ERR ("FATAL ERROR: Out of memory in err_printf()\n");
      return;
   }

   memset (tmp, 0, len + 1);

   vsnprintf (tmp, len, fmts, ap);
   free (*dst);
   (*dst) = tmp;

   va_end (apc);
}

static void db_err_printf (sqldb_t *db, const char *fmts, ...)
{
   va_list ap;
   if (!db)
      return;

   va_start (ap, fmts);
   err_printf (&db->lasterr, fmts, ap);
   va_end (ap);
}

static void res_err_printf (sqldb_res_t *res, const char *fmts, ...)
{
   va_list ap;
   if (!res)
      return;

   va_start (ap, fmts);
   err_printf (&res->lasterr, fmts, ap);
   va_end (ap);
}

static char **sqlitedb_row_get (sqldb_res_t *res)
{
   bool error = true;
   char **ret = NULL;
   char *tmpstring = NULL;

   sqlite3_stmt *stmt = res->sqlite_stmt;

   int numcols = sqlite3_column_count (stmt);

   for (int i=0; i<numcols; i++) {
      if (!(tmpstring = ((char *)sqlite3_column_text (stmt, i)))) {
         SQLDB_OOM (sqlite3_column_text (stmt, i));
         goto errorexit;
      }
      if (!(row_append (&ret, i, tmpstring))) {
         SQLDB_OOM (tmpstring);
         goto errorexit;
      }
   }

   error = false;
errorexit:
   if (error) {
      sqldb_row_free (ret);
      ret = NULL;
   }
   return ret;
}

static char **pgdb_row_get (sqldb_res_t *res)
{
   bool error = true;
   char **ret = NULL;

   int numcols = PQnfields (res->pgr);
   int index = 0;

   for (int i=0; i<numcols; i++) {
      const char *value = PQgetvalue (res->pgr, res->current_row, i);
      if (!(row_append (&ret, i, value))) {
         SQLDB_OOM (value);
         goto errorexit;
      }
   }

   res->current_row++;
   error = false;
errorexit:
   if (error) {
      sqldb_row_free (ret);
      ret = NULL;
   }
   return ret;
}

static char **row_get (sqldb_res_t *res)
{
   char **ret = NULL;
   switch (res->type) {
      case sqldb_SQLITE:   ret = sqlitedb_row_get (res);
                           break;

      case sqldb_POSTGRES: ret = pgdb_row_get (res);
                           break;

      default:             ret = NULL;
                           break;
   }

   return ret;
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
      PROG_ERR ("(%s) Unable to open database: %s\n", dbname, tmp);
      goto errorexit;
   }

   if (!(sqldb_batch (ret, "PRAGMA foreign_keys = ON", NULL))) {
      const char *tmp =  sqlite3_errstr (rc);
      PROG_ERR ("(%s) Unable to open database: %s\n", dbname, tmp);
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
      PROG_ERR ("[%s] Connection failure: [%s]\n",
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
      default:             PROG_ERR ("Error: dbtype [%u] is unknown\n",
                                            type);
                           goto errorexit;
   }

errorexit:
   free (ret);
   return NULL;
}

sqldb_dbtype_t sqldb_type (sqldb_t *db)
{
   return db ? db->type : sqldb_UNKNOWN;
}

void sqldb_close (sqldb_t *db)
{
   if (!db)
      return;

   switch (db->type) {
      case sqldb_SQLITE:   sqlite3_close (db->sqlite_db);               break;
      case sqldb_POSTGRES: PQfinish (db->pg_db);                        break;
      default:             db_err_printf (db, "Unknown type %i\n", db->type);
                           break;
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
      PROG_ERR ("(%i) Unknown type\n", type);
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

uint32_t sqldb_res_num_columns (sqldb_res_t *res)
{
   if (!res)
      return 0;

   uint32_t ret = 0;

   switch (res->type) {
      case  sqldb_SQLITE:  ret = sqlite3_column_count (res->sqlite_stmt);
                           break;

      case sqldb_POSTGRES: ret = PQnfields (res->pgr);
                           break;

      default:             ret = 0;
                           break;
   }

   return ret;
}

char **sqldb_res_column_names (sqldb_res_t *res)
{
   bool error = true;
   char **ret = NULL;
   uint32_t ncols = 0;

   if (!res)
      goto errorexit;

   if (!(ncols = sqldb_res_num_columns (res)))
      goto errorexit;

   if (!(ret = malloc ((sizeof *ret) * (ncols + 1))))
      goto errorexit;

   memset (ret, 0, (sizeof *ret) * (ncols + 1));

   for (uint32_t i=0; i<ncols; i++) {
      const char *tmp = NULL;
      switch (res->type) {
         case  sqldb_SQLITE:  tmp = sqlite3_column_name (res->sqlite_stmt, i);
                              break;

         case sqldb_POSTGRES: tmp = PQfname (res->pgr, i);
                              break;

         default:             tmp = NULL;
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
      const char *tmp2 = sqlite3_errmsg (db->sqlite_db);
      db_err_printf (db, "Fatal error: %s/%i\n%s\n[%s]\n", tmp,
                                                           rc,
                                                           tmp2,
                                                           qstring);
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
            db_err_printf (db, "Impossible error\n");
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
            db_err_printf (db, "Unknown column type: %u\n", coltype);
            break;
      }

      if (err!=SQLITE_OK) {
         db_err_printf (db, "Unable to bind %i\n", index);
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
            db_err_printf (db, "Impossible error\n");
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
            db_err_printf (db, "Unknown column type: %u\n", coltype);
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
      db_err_printf (db, "Possible OOM error (pg)\n[%s]\n", qstring);
      goto errorexit;
   }

   ExecStatusType rs = PQresultStatus (ret->pgr);
   if (rs != PGRES_COMMAND_OK && rs != PGRES_TUPLES_OK) {
      db_err_printf (db, "Bad postgres return status [%s]\n[%s]\n",
                          PQresultErrorMessage (ret->pgr),
                          qstring);
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
      default:             db_err_printf (db, "(%i) Unknown type\n", db->type);
                           goto errorexit;
   }
   if (!ret) {
      // db_err_printf (db, "Failed to execute stmt [%s]\n", qstring);
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

sqldb_res_t *sqldb_exec (sqldb_t *db, const char *query, ...)
{
   sqldb_res_t *ret = NULL;
   va_list ap;

   va_start (ap, query);
   ret = sqldb_execv (db, query, &ap);
   va_end (ap);

   return ret;
}

char ***sqldb_matrix_exec (sqldb_t *db, const char *query, ...)
{
   va_list ap;
   va_start (ap, query);
   char ***ret = sqldb_matrix_execv (db, query, &ap);
   va_end (ap);
   return ret;
}

char ***sqldb_matrix_execv (sqldb_t *db, const char *query, va_list *ap)
{
   bool error = true;
   char ***ret = NULL;
   size_t retlen = 0;
   char **colnames = NULL;
   size_t nrows = 0;
   size_t ncols = 0;
   char **row = NULL;
   size_t rowlen = 0;

   sqldb_res_t *res = NULL;

   if (!(res = sqldb_execv (db, query, ap)))
      goto errorexit;

   ncols = sqldb_res_num_columns (res);
   if (!(colnames = sqldb_res_column_names (res)))
      goto errorexit;

   for (size_t i=0; colnames[i]; i++) {
      if (!(row_append (&row, rowlen++, colnames[i])))
         goto errorexit;
   }
   if (!(matrix_append (&ret, retlen++, row)))
      goto errorexit;

   int rc = -1;
   while ((rc = sqldb_res_step (res)) == 1) {
      if (!(row = row_get (res)))
         goto errorexit;
      if (!(matrix_append (&ret, retlen++, row)))
         goto errorexit;
   }

   if (rc!=0)
      goto errorexit;

   error = false;
errorexit:

   sqldb_res_del (res);
   sqldb_row_free (colnames);

   if (error) {
      sqldb_matrix_free (ret);
      ret = NULL;
   }
   return ret;
}

void sqldb_matrix_free (char ***matrix)
{
   if (!matrix)
      return;

   size_t nrows = sqldb_matrix_num_rows (matrix);
   size_t ncols = sqldb_matrix_num_cols (matrix[0]);
   for (size_t i=0; i<nrows; i++) {
      sqldb_row_free (matrix[i]);
   }
   free (matrix);
}

void sqldb_row_free (char **row)
{
   for (size_t i=0; row[i]; i++) {
      free (row[i]);
   }
   free (row);
}

size_t sqldb_matrix_num_rows (char ***matrix)
{
   size_t nrows = 0;
   if (!matrix)
      return 0;

   for (nrows=0; matrix[nrows]; nrows++)
      ;

   return nrows;
}

size_t sqldb_matrix_num_cols (char **row)
{
   size_t ncols = 0;
   if (!row)
      return 0;

   for (ncols=0; row[ncols]; ncols++)
      ;

   return ncols;
}



uint64_t sqldb_exec_ignore (sqldb_t *db, const char *query, ...)
{
   va_list ap;

   va_start (ap, query);
   uint64_t ret = sqldb_exec_ignorev (db, query, &ap);
   va_end (ap);

   return ret;
}

uint64_t sqldb_exec_ignorev (sqldb_t *db, const char *query, va_list *ap)
{
   uint64_t ret = (uint64_t)-1;

   sqldb_res_t *res = NULL;

   if (!(res = sqldb_execv (db, query, ap)))
      goto errorexit;

   if ((sqldb_res_step (res))==-1)
      goto errorexit;

   ret = sqldb_res_last_id (res);

errorexit:
   if (res && res->lasterr) {
      db_err_printf (res->dbcon, "Error in [%s]: %s\n", query, res->lasterr);
   }
   sqldb_res_del (res);

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
         db_err_printf (db, "DB exec failure [%s]\n[%s]\n", qstring, errmsg);
      }

      sqlite3_free (errmsg);
      qstring = va_arg (ap, char *);
   }

   return ret;
}

static bool pgdb_batch (sqldb_t *db, va_list ap)
{
   bool ret = true;

   if (!db->pg_db)
      return false;

   char *qstring = va_arg (ap, char *);

   while (ret && qstring) {
      PGresult *result = PQexec (db->pg_db, qstring);
      if (!result) {
         db_err_printf (db, "PGSQL - OOM error\n[%s]\n", qstring);
         ret = false;
         break;
      }
      ExecStatusType rc = PQresultStatus (result);
      if (rc == PGRES_BAD_RESPONSE || rc == PGRES_FATAL_ERROR) {
         const char *rc_msg = PQresStatus (rc),
                    *res_msg = PQresultErrorMessage (result);

         db_err_printf (db, "pgdb_batch: Bad postgres result[%s]\n[%s]\n[%s]\n",
                            rc_msg,
                            res_msg,
                            qstring);
         ret = false;
         PQclear (result);
         break;
      }

      PQclear (result);
      qstring =  va_arg (ap, char *);
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

      case sqldb_SQLITE:   ret = sqlitedb_batch (db, ap);   break;

      case sqldb_POSTGRES: ret = pgdb_batch (db, ap);       break;

      default:             db_err_printf (db, "(%i) Format unsupported\n",
                                              db->type);
                           ret = false;
                           break;
   }
   va_end (ap);
   return ret;
}

static char *push_char (char **dst, size_t *len, char c)
{
   char *tmp = realloc ((*dst), (*len)+2);
   if (!tmp) {
      return NULL;
   }
   (*dst) = tmp;
   (*dst)[(*len)] = c & 0xff;
   (*dst)[(*len)+1] = 0;
   (*len)++;

   return (*dst);
}

static char *read_stmt (FILE *inf)
{
   char *ret = NULL;
   size_t len = 0;
   int c = 0;
   int pc = 0;
   char cquote = 0;
   bool comment = false;

   while (!feof (inf) && !ferror (inf) && ((c = fgetc (inf))!=EOF)) {

      if (c=='\n')
         comment = false;

      if (c=='-' && pc=='-')
         comment = true;

      if (!cquote) {
         if (c == '\'') cquote = '\'';
         if (c == '"')  cquote = '"';
         if (c == ';' && !comment) {
            // We can probably do without the terminating ';' here, we are
            // returning a full statement anyway and the caller will
            // execute if immediately, hence there is no reason to free
            // ret and return NULL.
            ret = push_char (&ret, &len, c);
            break;
         }
      } else {
         if (c == '\'' || c == '"')
            cquote = 0;
      }

      if (!(ret = push_char (&ret, &len, c))) {
         free (ret);
         return NULL;
      }

      pc = c;
   }

   return ret;
}

bool sqldb_batchfile (sqldb_t *db, FILE *inf)
{
   char *stmt = NULL;

   while ((stmt = read_stmt (inf))) {
      printf ("FOUND STATEMENT: [%s]\n", stmt);
      if (!(sqldb_batch (db, stmt, NULL))) {
         db_err_printf (db, "Batchfile statement failed: \n%s\n", stmt);
         free (stmt);
         return false;
      }

      free (stmt);
   }
   return true;
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

   if (ret==-1) {
      PROG_ERR ("sqldb_res error: %s\n", sqldb_res_lasterr (res));
   }

   return ret;
}

static uint64_t convert_ISO8601_to_uint64 (const char *src)
{
   struct tm tm;

   memset (&tm, 0, sizeof tm);

   if ((sscanf (src, "%4d-%2d-%2d %2d:%2d:%2d", &tm.tm_year,
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
      char *tmpstring = NULL;

      switch (coltype) {

         case sqldb_col_UNKNOWN:
            res_err_printf (res, "Possibly corrupt stack");
            // TODO: Store value in result
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
            tmpstring = ((char *)sqlite3_column_text (stmt, ret));
            *(char **)dst = lstr_dup (tmpstring ? tmpstring : "");
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
            res_err_printf (res, "Error: NULL type not supported\n");
            // TODO: Store value in result
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
            res_err_printf (res, "Possibly corrupt stack");
            // TODO: Store value in result
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
            res_err_printf (res, "Error: BLOB type not supported\n");
            // TODO: Store value in result
            return (uint32_t)-1;

         case sqldb_col_NULL:
            res_err_printf (res, "Error: NULL type not supported\n");
            // TODO: Store value in result
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
      db_err_printf (db, "[%s] - sql exection failed: %s\n",
                         query, sqldb_lasterr (db));
      goto errorexit;
   }

   int step = sqldb_res_step (res);
   if (step==0) {
      db_err_printf (db, "[%s] - No results (%s)!\n",
                         query, sqldb_lasterr (db));
      goto errorexit;
   }
   if (step==-1) {
      db_err_printf (db, "[%s] - Error during fetch (%s)\n",
                         query, sqldb_lasterr (db));
      goto errorexit;
   }
   if (step!=1) {
      db_err_printf (db, "[%s] - Unknown error (%s)\n",
                         query, sqldb_lasterr (db));
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
      default:             res_err_printf (res, "(%i) Unknown type\n",
                                                res->type);
                           // TODO: Store value in result
                           break;
   }
   free (res);
}

void sqldb_print (sqldb_t *db, FILE *outf)
{
   if (!outf) {
      outf = stdout;
   }

   if (!db) {
      PROG_ERR ("sqldb_t obvject is NULL\n");
      return;
   }

   PROG_ERR ("%30s: %s\n", "type", db->type==sqldb_SQLITE ?
                                        "SQLITE" : "POSTGRES");
   PROG_ERR ("%30s: %s\n", "lasterr", db->lasterr);
}


