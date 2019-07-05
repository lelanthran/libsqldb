#include <string.h>

#include "sqldb_query.h"

static uint64_t make_hash (int initial, const char *string)
{
   uint64_t ret = initial;

   for (size_t i=0; string[i]; i++) {
      uint8_t tmp = (ret >> 56) & 0x7f;
      ret ^= (ret << 7) | string[i];
      ret |= (ret << 7) | tmp;
   }

   return ret;
}

void sqldb_query_init (struct sqldb_query_t *queries, size_t nqueries)
{
   for (size_t i=0; i<nqueries &&
                    queries &&
                    queries[i].name &&
                    queries[i].query_string; i++) {
      queries[i].rfu = make_hash (queries[i].type, queries[i].name);
   }

   // TODO: Optimisation potentional: sort the array using hash as the key
}

const char *sqldb_query_find (struct sqldb_query_t *queries,
                              size_t nqueries,
                              int type, const char *name)
{
   uint64_t hash;
   if (!queries || !name || !nqueries)
      return "";

   hash = make_hash (type, name);

   // TODO: We can make this a binary search if we order the queries
   // first.
   for (size_t i=0; i<nqueries &&
                    queries[i].name &&
                    queries[i].query_string; i++) {
      if (queries[i].rfu == hash) {
         if ((strcmp (queries[i].name, name))==0) {
            return queries[i].query_string;
         }
      }
   }

   return "";
}

