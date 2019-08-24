
#ifndef H_SQLDB_QUERY
#define H_SQLDB_QUERY

#include <stdint.h>
#include <stdlib.h>

// This module is intended to help find queries. All queries must be
// stored in an array of structs. Each query must have a type (database
// type, of type sqldb_dbtype_t), a name and an actual query string that
// will be searched for and returned.
//
// After initialising the struct, the caller must also call the _init()
// function below. After a complete initialisation, the caller must use
// the _find() function below to search for query strings using the type
// (database type) and name.
//
// See the file sqldb_query_test.c for an example of the usage.

struct sqldb_query_t {
   // The database type
   int         type;

   // The name of the query
   const char *name;

   // The query string
   const char *query_string;

   // Reserved for use by the module. The caller must not modify this.
   uint64_t    rfu;
};

#ifdef __cplusplus
extern "C" {
#endif

   // Initiliase the query array queries of length nqueries. Processing
   // stops on the first NULL-name query or NULL-query found, or when
   // nqueries number of queries is processed, whichever comes first.
   void sqldb_query_init (struct sqldb_query_t *queries, size_t nqueries);

   // Find the query identified by name and type in the array of queries
   // specified by queries. nqueries is the number of queries in the
   // array.
   //
   // Processing stops on the first NULL-name query or NULL-query found,
   // or when nqueries number of queries is processed, whichever comes first.
   //
   // Returns the query string if found, or an empty string if the query
   // is not found.
   const char *sqldb_query_find (struct sqldb_query_t *queries,
                                 size_t nqueries,
                                 int type, const char *name);



#ifdef __cplusplus
};
#endif

#endif

