#include <stdlib.h>

#include "sqldb.h"
#include "sqldb_queue.h"
#include "sqldb_queue_query.h"

struct sqldb_queue_t {
   char        *name;
   char        *description;
   uint64_t     queue_expiry;
   uint64_t     msg_expiry;
   uint64_t     max_length;
   int          lasterr;
};

static void sqldb_queue_del (sqldb_queue_t *queue)
{
   if (!queue)
      return;

   free (queue->name);
   free (queue->description);
   free (queue);
}

static sqldb_queue_t *sqldb_queue_new (const char *name,
                                       const char *description,
                                       uint64_t queue_expiry,
                                       uint64_t msg_expiry,
                                       uint64_t max_length)
{
   return NULL;
}


int sqldb_queue_open (sqldb_t *db, const char *queue_name, sqldb_queue_t **dst)
{
   return NULL;
}

void sqldb_queue_close (sqldb_queue_t *queue)
{
   sqldb_queue_del (queue);
}

