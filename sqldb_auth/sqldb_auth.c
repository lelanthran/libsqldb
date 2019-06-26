#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#include <sys/types.h>
#include <unistd.h>

#include "sqldb_auth/sqldb_auth.h"
#include "sqldb_auth/sqldb_auth_query.h"
#include "sqldb_auth/sha-256.h"

#define LOG_ERR(...)      do {\
      fprintf (stderr, ":%s:%d: ", __FILE__, __LINE__);\
      fprintf (stderr, __VA_ARGS__);\
} while (0)


bool sqldb_auth_initdb (sqldb_t *db)
{
   bool error = true;
   const char *init_sqlite_stmt = NULL,
              *create_tables_stmt = NULL;

   if (!db)
      goto errorexit;

   if (!(init_sqlite_stmt = sqldb_auth_query ("init_sqlite")))
      LOG_ERR ("Could not find statement for 'init_sqlite'\n");

   if (!(create_tables_stmt = sqldb_auth_query ("create_tables")))
      LOG_ERR ("Could not find statement for 'create_tables'\n");

   if (!init_sqlite_stmt || !create_tables_stmt)
      goto errorexit;

   if (!(sqldb_batch (db, init_sqlite_stmt, create_tables_stmt))) {
      LOG_ERR ("Failed to execute init/create statement.\n");
      goto errorexit;
   }

   error = false;

errorexit:

   return !error;
}

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
   for (size_t i=0; i<0x0fff; i++) {
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

uint32_t sqldb_auth_random_seed (void)
{
   extern int main (void);

   uint32_t ret = 0;

   char char_array[50];

   /* Sources of randomness:
    * 1. Epoch in seconds since 1970:
    * 2. Processor time since program start:
    * 3. Pointer to current stack position:
    * 4. Pointer to heap:
    * 5. Pointer to function (self):
    * 6. Pointer to main():
    * 7. PID:
    * 8. tmpnam()
    */

   time_t epoch = time (NULL);
   uint32_t proc_time = getclock (ret);
   uint32_t *ptr_stack = &ret;
   uint32_t (*ptr_self) (void) = sqldb_auth_random_seed;
   int (*ptr_main) (void) = main;
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

   char *tmp_fname = tmpnam (NULL);
   if (!tmp_fname) {
      snprintf (char_array, sizeof char_array, "[%p]", ptr_self);
      tmp_fname = char_array;
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

void sqldb_auth_random_bytes (void *dst, size_t len)
{
   static uint64_t seed = 0;

   if (!seed) {
      seed = sqldb_auth_random_seed ();
      seed <<= 4;
      seed |= sqldb_auth_random_seed ();
      srand ((unsigned int)seed);
   }

   uint8_t *b = dst;
   for (size_t i=0; i<len; i++) {
      b[i] = (rand () >> 8) & 0xff;
   }
}

uint64_t sqldb_auth_user_create (sqldb_t *db,
                                 const char *email,
                                 const char *nick,
                                 const char *password)
{
   bool error = true;
   uint64_t ret = (uint64_t)-1;
   const char *qstring = NULL;

   if (!db ||
       !email     || !nick    || !password ||
       !email[0]  || !nick[0] || !password[0])
      goto errorexit;

   if (!(qstring = sqldb_auth_query ("user_create")))
      goto errorexit;

   ret = sqldb_exec_ignore (db, qstring, sqldb_col_TEXT, &email,
                                         sqldb_col_UNKNOWN);
   if (ret==(uint64_t)-1 || ret==0)
      goto errorexit;

   if (!(sqldb_auth_user_mod (db, email, email, nick, password)))
      goto errorexit;

   error = false;

errorexit:

   return error ? (uint64_t)-1 : ret;
}


bool sqldb_auth_user_rm (sqldb_t *db, const char *email)
{
   const char *qstring = NULL;
   uint64_t rc = 0;

   if (!db || !email || !email[0])
      return false;

   if (!(qstring = sqldb_auth_query ("user_rm")))
      return false;

   rc = sqldb_exec_ignore (db, qstring, sqldb_col_TEXT, &email,
                                         sqldb_col_UNKNOWN);

   return (rc==(uint64_t)-1) ? false : true;
}

static bool make_password_hash (char dst[65], const char sz_salt[65],
                                              const char *new_email,
                                              const char *nick,
                                              const char *password)
{
   bool error = true;

   uint8_t  hash[32];

   char *tmp = NULL;
   size_t tmplen = 0;

   tmplen =   strlen (sz_salt)
            + strlen (new_email)
            + strlen (nick)
            + strlen (password)
            + 1;

   if (!(tmp = malloc (tmplen)))
      goto errorexit;

   strcpy (tmp, sz_salt);
   strcat (tmp, new_email);
   strcat (tmp, nick);
   strcat (tmp, password);

   calc_sha_256 (hash, tmp, tmplen);

#define STRINGIFY(dst,dstlen,src,srclen)    do {\
   for (size_t i=0; i<srclen && (i*2)<dstlen; i++) {\
      sprintf (&dst[i*2], "%02x", src[i]);\
   }\
} while (0)

   STRINGIFY (dst, 64, hash, sizeof hash);

   error = false;

errorexit:

   free (tmp);

   return !error;
}

bool sqldb_auth_user_mod (sqldb_t *db,
                          const char *old_email,
                          const char *new_email,
                          const char *nick,
                          const char *password)
{
   bool error = true;

   const char *qstring = NULL;

   uint8_t  salt[32];
   char     sz_salt[(32 * 2) + 1];
   char     sz_hash[65];

   char *ptr_salt = sz_salt,
        *ptr_hash = sz_hash;

   uint64_t ret = (uint64_t)-1;

   if (!db ||
       !old_email    || !new_email    || !nick    || !password ||
       !old_email[0] || !new_email[0] || !nick[0] || !password[0])
      goto errorexit;

   if (!(qstring = sqldb_auth_query ("user_mod")))
      goto errorexit;

   sqldb_auth_random_bytes (salt, sizeof salt);
   STRINGIFY (sz_salt, sizeof sz_salt, salt, 32);
#undef STRINGIFY


   if (!(make_password_hash (sz_hash, sz_salt, new_email, nick, password)))
      goto errorexit;

   ret = sqldb_exec_ignore (db, qstring, sqldb_col_TEXT, &old_email,
                                         sqldb_col_TEXT, &new_email,
                                         sqldb_col_TEXT, &ptr_salt,
                                         sqldb_col_TEXT, &ptr_hash,
                                         sqldb_col_UNKNOWN);
   error = false;

errorexit:

   return !error;
}


