
#ifndef H_LIBRARY_SQLDB
#define H_LIBRARY_SQLDB

/* This is an agnostic interface to the sql backend. While it is possible
 * for the caller to use SQL statements that are specific to a single SQL
 * platform it is not advised as this means that different statements
 * would be needed for different backends.
 *
 * There are two datatypes: a connection object and a resultset object.
 * Both must be closed after use. Only parameterised statements should be
 * used, with the parameters defined as #1, #2, etc. within the statement.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>

typedef struct sqldb_t sqldb_t;
typedef struct sqldb_res_t sqldb_res_t;

typedef enum {
   sqldb_UNKNOWN = 0,
   sqldb_SQLITE,
   sqldb_POSTGRES,
   sqldb_MYSQL,
} sqldb_dbtype_t;

typedef enum {
   sqldb_col_UNKNOWN = 0,  // Mark end of list
   sqldb_col_INT32,        // int32_t
   sqldb_col_INT64,        // int64_t
   sqldb_col_UINT32,       // uint32_t
   sqldb_col_UINT64,       // uint64_t
   sqldb_col_DATETIME,     // int64_t
   sqldb_col_TEXT,         // char *
   sqldb_col_BLOB,
   sqldb_col_NULL
} sqldb_coltype_t;

#ifdef __cplusplus
extern "C" {
#endif

   // Return a random seed.
   uint32_t sqldb_random_seed (void);

   // Return random bytes
   void sqldb_random_bytes (void *dst, size_t len);


   // When type==sqlite:      Creates a new sqlite3 database using dbname
   //                         as the filename. The db parameter is ignored.
   // When type==postgres     Creates a new postgres database called
   //                         'dbname' using the specified db to connect.
   bool sqldb_create (sqldb_t *db, const char *dbname, sqldb_dbtype_t type);

   // Open a connection to the database, using the type specified. Returns
   // NULL on error.
   sqldb_t *sqldb_open (const char *dbname, sqldb_dbtype_t type);

   // Get the database type of the specified database object. Returns
   // sqldb_UNKNOWN on error.
   sqldb_dbtype_t sqldb_type (sqldb_t *db);

   // Close the connection to the database. All resources will be freed
   // except existing sqldb_res_t objects.
   void sqldb_close (sqldb_t *db);

   // Return a description of the last error that occurred. The caller
   // must not free this string. NULL will be returned if no
   // error message is available.

   const char *sqldb_lasterr (sqldb_t *db);
   const char *sqldb_res_lasterr (sqldb_res_t *res);

   // Clear the last error messages stored.
   void sqldb_clearerr (sqldb_t *db);
   void sqldb_res_clearerr (sqldb_res_t *res);

   // Get the number of rows affected by the last operation executed on
   // this database. Only applies to insert/update/delete operations.
   uint64_t sqldb_count_changes (sqldb_t *db);

   // Get the last inserted ID for this result if query was an insert
   // operation. If query was NOT an insert operation then the return
   // value is not defined.
   uint64_t sqldb_res_last_id (sqldb_res_t *res);

   // Get the number of columns in this result-set.
   uint32_t sqldb_res_num_columns (sqldb_res_t *res);

   // Get the column name in this result-set. The return value is an array
   // of strings (char pointers) terminated with a NULL pointer that the
   // caller must free. The caller must also free the array itself.
   //
   // On error NULL is returned.
   char **sqldb_res_column_names (sqldb_res_t *res);

   // Uses the specified parameterised querystring and tuples in the
   // variadic arguments to construct a query that is executed on the
   // database. See explanation of tuple format in scan_columns below.
   //
   // Parameters in querystring are of the format "#n" where n is the
   // number of the parameter.
   //
   // Returns a result object (that may be empty if the query returned no
   // results) on success or NULL on error.
   sqldb_res_t *sqldb_exec (sqldb_t *db, const char *query, ...);
   sqldb_res_t *sqldb_execv (sqldb_t *db, const char *query, va_list *ap);

   // Same as the exec functions above, with the difference being that
   // results are ignored and only a success or failure is indicated with
   // the return value, which is:
   //    error (any):                     (uint64_t)-1
   //    success (insert):                Last inserted ID
   //    success (non-insert statements): 0
   uint64_t sqldb_exec_ignore (sqldb_t *db, const char *query, ...);
   uint64_t sqldb_exec_ignorev (sqldb_t *db, const char *query, va_list *ap);

   // Executes a batch of statements and returns no results. Multiple
   // statements can be specified, ending with a NULL pointer. Each
   // statement may be composed of multiple statements itself, with each
   // statement separated with a semicolon.
   //
   // Statements must not have parameters. Execution stops on the first
   // error encountered.
   //
   // Return value is true if no errors were encountered and false if
   // any errors were encountered. The lasterr() function will return
   // the error message.
   //
   // This is a dangerous function to use on unknown input, so use with
   // care.
   bool sqldb_batch (sqldb_t *db, ...);

   // This is similar to the sqldb_batch() function, with the exception
   // being that the sql statements are all read from the FILE * passed in.
   //
   // NOTE: No parameterisation is possible, hence no parameters can be
   // passed in.
   //
   // Other than the exception listed above all the other behaviour,
   // including return value and error messages, remain the same as
   // sqldb_batch().
   //
   // This is a dangerous function to use on unknown input, so use with
   // care.
   bool sqldb_batchfile (sqldb_t *db, FILE *inf);

   // Returns 0 if no rows are available, 1 if a row is available and -1
   // if an error occurred.
   int sqldb_res_step (sqldb_res_t *res);

   // Scans in all the columns in the current row. Returns the number of
   // columns scanned in.
   //
   // Each variadic argument is a tuple consisting of the column type (see
   // enum above) and a pointer to a destination to store the data read.
   // In the case of TEXT columns the memory to store a string is malloced
   // by this function and must be freed by the caller.
   //
   // In the case of a BLOB field an extra variadic argument of type
   // (uint32_t *) is used to store the length of the blob.
   //
   // On success the number of fields scanned and stored is returned
   // (which may be zero if the query had no results). On error
   // (uint32_t)-1 is returned.
   uint32_t sqldb_scan_columns (sqldb_res_t *res, ...);
   uint32_t sqldb_scan_columnsv (sqldb_res_t *res, va_list *ap);

   // This is a bit of a tricky function. It executes the given query
   // using all the arguments in (...) as pairs of {type,value} until it
   // reaches type_UNKNOWN. It then fetches the results into tuples
   // *after* type_UNKNOWN using the remainder of the arguments on the
   // stack.
   //
   // In short, it first executes sqldb_exec(...), and then
   // sqldb_scan_columns(...) - the caller must take care to provide the
   // correct number of arguments or else the stack will be damaged.
   //
   // On any error prior to the fetching of the results (uint32_t)-1 is
   // returned. On any error during the fetching of the results processing
   // stops. The return value indicates how many columns were successfully
   // read in and stored, which may be less than expected if errors
   // occurred during processing.
   uint32_t sqldb_exec_and_fetch (sqldb_t *db, const char *query, ...);

   // Closes and deletes the resultset and all resources associated with
   // it.
   void sqldb_res_del (sqldb_res_t *res);

   // For diagnostics during development
   void sqldb_print (sqldb_t *db, FILE *outf);

#ifdef __cplusplus
};
#endif




#endif
