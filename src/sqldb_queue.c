#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "sqldb.h"
#include "sqldb_queue.h"
#include "sqldb_queue_query.h"

// Redefining it here because don't want to pollute public namespace with
// small irrelevant functions.
static char *lstrdup (const char *src)
{
   if (!src)
      src = "";

   size_t len = strlen (src) + 1;

   char *ret = malloc (len);
   if (!ret)
      return NULL;

   return strcpy (ret, src);
}


/* **************************************************************** */

const char *sqldb_queue_strerror (int code)
{
   static char unknown_error[25 + 25];
   static const struct {
      int code;
      const char *msg;
   } msgs[] = {
      { SQLDB_QUEUE_SUCCESS,             "Success" },
      { SQLDB_QUEUE_EEXISTS,             "Resource exists" },
   };

   for (size_t i=0; i<sizeof msgs/sizeof msgs[0]; i++) {
      if (msgs[i].code == code)
         return msgs[i].msg;
   }

   snprintf (unknown_error, "Unknown error %i", code);
   return unknown_error;
}

/* **************************************************************** */

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
   bool error = true;
   sqldb_queue_t *ret = NULL;

   if (!(ret = malloc (sizeof *ret)))
      goto errorexit;

   memset (ret, 0, sizeof *ret);
   ret->name = lstrdup (name);
   ret->description = lstrdup (description);

   error = false;

errorexit:
   if (error) {
      sqldb_queue_del (ret);
      ret = NULL;
   }

   return ret;
}


/* **************************************************************** */

int sqldb_queue_create (sqldb_t *db, const char *queue_name)
{
   return SQLDB_QUEUE_EEXISTS;
}

int sqldb_queue_open (sqldb_t *db, const char *queue_name, sqldb_queue_t **dst)
{
   return SQLDB_QUEUE_EEXISTS;
}

void sqldb_queue_close (sqldb_queue_t *queue)
{
   sqldb_queue_del (queue);
}

