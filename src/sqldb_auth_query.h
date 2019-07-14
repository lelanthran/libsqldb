
#ifndef H_SQLDB_AUTH_QUERY
#define H_SQLDB_AUTH_QUERY

#define SQLDB_AUTH_GLOBAL_RESOURCE       ("_ALL_")

#ifdef __cplusplus
extern "C" {
#endif

   const char *sqldb_auth_query (const char *qname);

#ifdef __cplusplus
};
#endif

#endif
