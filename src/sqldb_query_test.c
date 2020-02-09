
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "sqldb_query.h"

int main (void)
{
   static struct sqldb_query_t queries[] = {
      { 1, "Name-1", "Query: Name-1-Type-1", 0 },
      { 1, "Name-2", "Query: Name-2-Type-1", 0 },
      { 1, "Name-3", "Query: Name-3-Type-1", 0 },
      { 2, "Name-1", "Query: Name-1-Type-2", 0 },
      { 2, "Name-2", "Query: Name-2-Type-2", 0 },
      { 2, "Name-3", "Query: Name-3-Type-2", 0 },
      { 3, "Name-1", "Query: Name-1-Type-3", 0 },
      { 3, "Name-2", "Query: Name-2-Type-3", 0 },
      { 3, "Name-3", "Query: Name-3-Type-3", 0 },
      { 4, "Name-1", "Query: Name-1-Type-4", 0 },
      { 4, "Name-2", "Query: Name-2-Type-4", 0 },
      { 4, "Name-3", "Query: Name-3-Type-4", 0 },
      { 5, "Name-1", "Query: Name-1-Type-5", 0 },
      { 5, "Name-2", "Query: Name-2-Type-5", 0 },
      { 5, "Name-3", "Query: Name-3-Type-5", 0 },
      { 6, "Name-1", "Query: Name-1-Type-6", 0 },
      { 6, "Name-2", "Query: Name-2-Type-6", 0 },
      { 6, "Name-3", "Query: Name-3-Type-6", 0 },
   };

   size_t nqueries = sizeof queries / sizeof queries[0];

   int ret = EXIT_FAILURE;

   printf ("sqldb_query_test %s\nCopyright L. Manickum 2020\n", SQLDB_VERSION);

   sqldb_query_init (queries, nqueries);

   for (size_t i=0; i<nqueries; i++) {
      printf ("%i: %" PRIu64 " %s: %s [%s]\n",
               queries[i].type,
               queries[i].rfu,
               queries[i].name,
               queries[i].query_string,
               sqldb_query_find (queries, nqueries,
                                 queries[i].type, queries[i].name));
   }

   ret = EXIT_SUCCESS;

   return ret;
}

