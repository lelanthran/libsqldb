
#ifndef H_SQLDB_QUEUE
#define H_SQLDB_QUEUE

#include <stdint.h>

#include "sqldb.h"

#define SQLDB_QUEUE_CREATE       (1 << 0)

#define  SQLDB_QUEUE_SUCCESS     (0)
#define  SQLDB_QUEUE_EEXISTS     (1)

typedef struct sqldb_queue_t sqldb_queue_t;

#ifdef __cplusplus
extern "C" {
#endif

   // Return a string describing the error code passed in.
   const char *sqldb_queue_strerror (int code);

   // Initialise the specified database with the tables necessary to
   // support queues
   bool sqldb_queue_init (sqldb_t *db);

   // Create a new queue of the specified name. On success zero is
   // returned. On failure an errorcode will be returned.
   int sqldb_queue_create (sqldb_t *db, const char *queue_name);

   // Opens an existing queue for use and returns a handle to that queue
   // in dst. On success SQLDB_QUEUE_SUCCESS is returned and dst will
   // contain a handle to a queue. On failure an error code is returned
   // and dst will remain untouched.
   int sqldb_queue_open (sqldb_t *db, const char *queue_name,
                         sqldb_queue_t **dst);

   // Closes a queue handle previously opened with sqldb_queue_open() or
   // created with sqldb_queue_create().
   void sqldb_queue_close (sqldb_queue_t *queue);

   // Return a description of the last error that occurred on this handle
   const char *sqldb_queue_lasterr (sqldb_queue_t *queue);

   // Clear the last errorcode for this handle
   void sqldb_queue_clearerr (sqldb_queue_t *queue);

#ifdef __cplusplus
};
#endif

#endif

