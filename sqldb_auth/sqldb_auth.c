#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <inttypes.h>

#include <sys/types.h>
#include <unistd.h>

#include "sqldb_auth/sqldb_auth.h"
#include "sqldb_auth/sqldb_auth_query.h"
#include "sqldb_auth/sha-256.h"

#define LOG_ERR(...)      do {\
      fprintf (stderr, ":%s:%d: ", __FILE__, __LINE__);\
      fprintf (stderr, __VA_ARGS__);\
} while (0)

#define SVALID(s)   ((s && s[0]))

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

uint64_t sqldb_auth_user_create (sqldb_t    *db,
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

bool sqldb_auth_user_info (sqldb_t    *db,
                           const char *email,
                           uint64_t   *id_dst,
                           char      **nick_dst,
                           char        session_dst[65])
{
   bool error = true;
   const char *qstring = NULL;
   sqldb_res_t *res = NULL;
   char *tmp_sess = NULL;

   if (!db || !email)
      return false;

   if (!(qstring = sqldb_auth_query ("user_info"))) {
      LOG_ERR ("Qstring failure: Failed to find user_info qstring\n");
      goto errorexit;
   }

   if (!(res = sqldb_exec (db, qstring, sqldb_col_TEXT, &email,
                                        sqldb_col_UNKNOWN))) {
      LOG_ERR ("Exec failure: [%s]\n", qstring);
      goto errorexit;
   }

   if ((sqldb_res_step (res))!=1) {
      LOG_ERR ("Step failure: [%s]\n", qstring);
      goto errorexit;
   }

   if ((sqldb_scan_columns (res, sqldb_col_UINT64, id_dst,
                                 sqldb_col_TEXT,   nick_dst,
                                 sqldb_col_TEXT,   &tmp_sess,
                                 sqldb_col_UNKNOWN))!=3) {
      LOG_ERR ("Scan failure: [%s]\n", qstring);
      goto errorexit;
   }

   strncpy (session_dst, tmp_sess, 65);
   session_dst[64] = 0;

   error = false;

errorexit:

   free (tmp_sess);
   sqldb_res_del (res);

   return !error;
}

static bool make_password_hash (char dst[65], const char  sz_salt[65],
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

bool sqldb_auth_user_mod (sqldb_t    *db,
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
                                         sqldb_col_TEXT, &nick,
                                         sqldb_col_TEXT, &ptr_salt,
                                         sqldb_col_TEXT, &ptr_hash,
                                         sqldb_col_UNKNOWN);
   error = false;

errorexit:

   return !error;
}


uint64_t sqldb_auth_group_create (sqldb_t    *db,
                                  const char *name,
                                  const char *description)
{
   uint64_t ret = (uint64_t)-1;
   const char *qstring = NULL;

   if (!db ||
       !(SVALID (name)) || !(SVALID (description)))
      goto errorexit;

   if (!(qstring = sqldb_auth_query ("group_create"))) {
      LOG_ERR ("Failed to find qstring: group_create\n");
      goto errorexit;
   }

   ret = sqldb_exec_ignore (db, qstring, sqldb_col_TEXT, &name,
                                         sqldb_col_TEXT, &description,
                                         sqldb_col_UNKNOWN);

errorexit:

   return ret;
}

bool sqldb_auth_group_rm (sqldb_t *db, const char *name)
{
   const char *qstring = sqldb_auth_query ("group_rm");

   if (!db || !(SVALID (name)))
      return false;

   uint64_t rc = sqldb_exec_ignore (db, qstring, sqldb_col_TEXT, &name,
                                                 sqldb_col_UNKNOWN);

   return rc == (uint64_t)-1 ? false : true;
}

bool sqldb_auth_group_info (sqldb_t    *db,
                            const char *name,
                            uint64_t   *id_dst,
                            char      **description)
{
   bool error = true;
   const char *qstring = NULL;
   sqldb_res_t *res = NULL;

   if (!db || !name)
      return false;

   if (!(qstring = sqldb_auth_query ("group_info"))) {
      LOG_ERR ("Qstring failure: Failed to find group_info qstring\n");
      goto errorexit;
   }

   if (!(res = sqldb_exec (db, qstring, sqldb_col_TEXT, &name,
                                        sqldb_col_UNKNOWN))) {
      LOG_ERR ("Exec failure: [%s]\n", qstring);
      goto errorexit;
   }

   if ((sqldb_res_step (res))!=1) {
      LOG_ERR ("Step failure: [%s]\n", qstring);
      goto errorexit;
   }

   if ((sqldb_scan_columns (res, sqldb_col_UINT64, id_dst,
                                 sqldb_col_TEXT,   description,
                                 sqldb_col_UNKNOWN))!=2) {
      LOG_ERR ("Scan failure: [%s]\n", qstring);
      goto errorexit;
   }

   error = false;

errorexit:

   sqldb_res_del (res);

   return !error;

}

bool sqldb_auth_group_mod (sqldb_t    *db,
                           const char *oldname,
                           const char *newname,
                           const char *description)
{
   bool error = true;
   uint64_t rc = 0;
   const char *qstring = NULL;

   if (!db ||
       !SVALID (oldname) || !SVALID (newname) || !SVALID (description))
      goto errorexit;

   if (!(qstring = sqldb_auth_query ("group_mod")))
      goto errorexit;

   rc = sqldb_exec_ignore (db, qstring, sqldb_col_TEXT, &oldname,
                                        sqldb_col_TEXT, &newname,
                                        sqldb_col_TEXT, &description,
                                        sqldb_col_UNKNOWN);

   if (rc==-1)
      goto errorexit;

   error = false;

errorexit:

   return !error;

}

bool sqldb_auth_group_adduser (sqldb_t    *db,
                               const char *name, const char *email)
{
   bool error = true;

   const char *qstring = NULL;
   uint64_t rc = 0;

   if (!(qstring = sqldb_auth_query ("group_adduser"))) {
      goto errorexit;
   }

   rc = sqldb_exec_ignore (db, qstring, sqldb_col_TEXT, &email,
                                        sqldb_col_TEXT, &name,
                                        sqldb_col_UNKNOWN);
   if (rc==(uint64_t)-1) {
      goto errorexit;
   }

   error = false;

errorexit:

   return !error;
}

bool sqldb_auth_group_rmuser (sqldb_t    *db,
                              const char *name, const char *email)
{
   bool error = true;
   const char *qstring = NULL;
   uint64_t rc = 0;

   if (!(qstring = sqldb_auth_query ("group_rmuser")))
      goto errorexit;

   rc = sqldb_exec_ignore (db, qstring, sqldb_col_TEXT, &name,
                                        sqldb_col_TEXT, &email,
                                        sqldb_col_UNKNOWN);

   if (rc==-1)
      goto errorexit;

   error = false;

errorexit:

   return !error;
}

bool sqldb_auth_user_find (sqldb_t    *db,
                           const char *email_pattern,
                           const char *nick_pattern,
                           uint64_t   *nitems,
                           char     ***emails,
                           char     ***nicks,
                           uint64_t  **ids)
{
#define BASE_QS      "SELECT c_email, c_nick, c_id FROM t_user "
   bool error = true;
   const char *qstrings[] = {
      BASE_QS ";",
      BASE_QS " WHERE c_email like #1;",
      BASE_QS " WHERE c_nick like #1;",
      BASE_QS " WHERE c_email like #1 OR c_nick like #2;",
   };
#undef BASE_QS

   const char *qstring = NULL;

   const char *p1 = NULL,
              *p2 = NULL;

   sqldb_coltype_t col1 = sqldb_col_UNKNOWN,
                   col2 = sqldb_col_UNKNOWN,
                   col3 = sqldb_col_UNKNOWN;

   sqldb_res_t *res = NULL;

   if (!(SVALID (email_pattern)) && !(SVALID (nick_pattern))) {
      qstring = qstrings[0];
   }

   if (SVALID (email_pattern) && !(SVALID (nick_pattern))) {
      qstring = qstrings[1];
      p1 = email_pattern;
      col1 = sqldb_col_TEXT;
   }

   if (!(SVALID (email_pattern)) && SVALID (nick_pattern)) {
      qstring = qstrings[2];
      p1 = nick_pattern;
      col1 = sqldb_col_TEXT;
   }

   if (SVALID (email_pattern) && SVALID (nick_pattern)) {
      qstring = qstrings[3];
      p2 = nick_pattern;
      col2 = sqldb_col_TEXT;
      p1 = email_pattern;
      col1 = sqldb_col_TEXT;
   }

   if (!(res = sqldb_exec (db, qstring, col1, &p1, col2, &p2, col3)))
      goto errorexit;

   *nitems = 0;

   if (emails) {
      free (*emails); *emails = NULL;
   }
   if (nicks) {
      free (*nicks); *nicks = NULL;
   }
   if (ids) {
      free (*ids); *ids = NULL;
   }

   while (sqldb_res_step (res) == 1) {
      char *email, *nick;
      uint64_t id;

      uint64_t newlen = *nitems + 1;
      if (emails) {
         char **tmp = realloc (*emails, (newlen + 1) * (sizeof *tmp));
         if (!tmp)
            goto errorexit;
         tmp[newlen - 1] = NULL;
         tmp[newlen] = NULL;
         *emails = tmp;
      }
      if (nicks) {
         char **tmp = realloc (*nicks, (newlen + 1) * (sizeof *tmp));
         if (!tmp)
            goto errorexit;
         tmp[newlen - 1] = NULL;
         tmp[newlen] = NULL;
         *nicks = tmp;
      }
      if (ids) {
         uint64_t *tmp = realloc ((*ids), (newlen + 1) * (sizeof *tmp));
         if (!tmp)
            goto errorexit;
         *ids = tmp;
      }

      if ((sqldb_scan_columns (res, sqldb_col_TEXT,   &email,
                                    sqldb_col_TEXT,   &nick,
                                    sqldb_col_UINT64, &id,
                                    sqldb_col_UNKNOWN))!=3)
         goto errorexit;

      if (emails) {
         (*emails)[newlen - 1] = email;
      } else {
         free (email);
      }

      if (nicks) {
         (*nicks)[newlen - 1] = nick;
      } else {
         free (nick);
      }

      if (ids) {
         (*ids)[newlen - 1] = id;
      }

      *nitems = newlen;
   }

   error = false;

errorexit:

   if (error) {
      if (emails) {
         for (size_t i=0; emails && *emails && (*emails)[i]; i++) {
            free ((*emails)[i]);
         }
         free (*emails); *emails = NULL;
      }

      if (nicks) {
         for (size_t i=0; nicks && *nicks && (*nicks)[i]; i++) {
            free ((*nicks)[i]);
         }
         free (*nicks); *nicks = NULL;
      }
   }

   sqldb_res_del (res);

   return !error;
}

bool sqldb_auth_group_find (sqldb_t    *db,
                            const char *name_pattern,
                            const char *description_pattern,
                            uint64_t   *nitems,
                            char     ***names,
                            char     ***descriptions,
                            uint64_t  **ids)
{
#define BASE_QS      "SELECT c_name, c_description, c_id FROM t_group "
   bool error = true;
   const char *qstrings[] = {
      BASE_QS ";",
      BASE_QS " WHERE c_name like #1;",
      BASE_QS " WHERE c_description like #1;",
      BASE_QS " WHERE c_name like #1 OR c_description like #2;",
   };
#undef BASE_QS

   const char *qstring = NULL;

   const char *p1 = NULL,
              *p2 = NULL;

   sqldb_coltype_t col1 = sqldb_col_UNKNOWN,
                   col2 = sqldb_col_UNKNOWN,
                   col3 = sqldb_col_UNKNOWN;

   sqldb_res_t *res = NULL;

   if (!(SVALID (name_pattern)) && !(SVALID (description_pattern))) {
      qstring = qstrings[0];
   }

   if (SVALID (name_pattern) && !(SVALID (description_pattern))) {
      qstring = qstrings[1];
      p1 = name_pattern;
      col1 = sqldb_col_TEXT;
   }

   if (!(SVALID (name_pattern)) && SVALID (description_pattern)) {
      qstring = qstrings[2];
      p1 = description_pattern;
      col1 = sqldb_col_TEXT;
   }

   if (SVALID (name_pattern) && SVALID (description_pattern)) {
      qstring = qstrings[3];
      p2 = description_pattern;
      col2 = sqldb_col_TEXT;
      p1 = name_pattern;
      col1 = sqldb_col_TEXT;
   }

   if (!(res = sqldb_exec (db, qstring, col1, &p1, col2, &p2, col3))) {
      LOG_ERR ("Failed to execute [%s]\n", qstring);
      goto errorexit;
   }

   *nitems = 0;

   if (names) {
      free (*names); *names = NULL;
   }
   if (descriptions) {
      free (*descriptions); *descriptions = NULL;
   }
   if (ids) {
      free (*ids); *ids = NULL;
   }

   while (sqldb_res_step (res) == 1) {
      char *name, *description;
      uint64_t id;

      uint32_t ncols = 0;

      uint64_t newlen = *nitems + 1;
      if (names) {
         char **tmp = realloc (*names, (newlen + 1) * (sizeof *tmp));
         if (!tmp) {
            LOG_ERR ("OOM error\n");
            goto errorexit;
         }
         tmp[newlen - 1] = NULL;
         tmp[newlen] = NULL;
         *names = tmp;
      }
      if (descriptions) {
         char **tmp = realloc (*descriptions, (newlen + 1) * (sizeof *tmp));
         if (!tmp) {
            LOG_ERR ("OOM error\n");
            goto errorexit;
         }
         tmp[newlen - 1] = NULL;
         tmp[newlen] = NULL;
         *descriptions = tmp;
      }
      if (ids) {
         uint64_t *tmp = realloc ((*ids), (newlen + 1) * (sizeof *tmp));
         if (!tmp) {
            LOG_ERR ("OOM error\n");
            goto errorexit;
         }
         *ids = tmp;
      }

      if ((ncols = (sqldb_scan_columns (res, sqldb_col_TEXT,   &name,
                                             sqldb_col_TEXT,   &description,
                                             sqldb_col_UINT64, &id,
                                             sqldb_col_UNKNOWN)))!=3) {
         LOG_ERR ("Scan failure [%u, expected 3]\n", ncols);
         goto errorexit;
      }

      if (names) {
         (*names)[newlen - 1] = name;
      } else {
         free (name);
      }

      if (descriptions) {
         (*descriptions)[newlen - 1] = description;
      } else {
         free (description);
      }

      if (ids) {
         (*ids)[newlen - 1] = id;
      }

      *nitems = newlen;
   }

   error = false;

errorexit:

   if (error) {
      if (names) {
         for (size_t i=0; names && (*names) && (*names)[i]; i++) {
            free ((*names)[i]);
         }
         free (*names); *names = NULL;
      }

      if (descriptions) {
         for (size_t i=0; descriptions && (*descriptions) && (*descriptions)[i]; i++) {
            free ((*descriptions)[i]);
         }
         free (*descriptions); *descriptions = NULL;
      }
   }

   sqldb_res_del (res);

   return !error;

}

bool sqldb_auth_group_members (sqldb_t    *db,
                               const char *name,
                               uint64_t   *nitems,
                               char     ***emails,
                               char     ***nicks,
                               uint64_t  **ids)
{
   bool error = true;

   const char *qstring = NULL;
   sqldb_res_t *res = NULL;

   if (!(qstring = sqldb_auth_query ("group_membership"))) {
      goto errorexit;
   }

   if (!(res = sqldb_exec (db, qstring, sqldb_col_TEXT, &name,
                                        sqldb_col_UNKNOWN))) {
      LOG_ERR ("Failed to execute [%s]: %s\n", qstring, sqldb_lasterr (db));
      goto errorexit;
   }

   *nitems = 0;

   if (emails) {
      free (*emails); *emails = NULL;
   }
   if (nicks) {
      free (*nicks); *nicks = NULL;
   }
   if (ids) {
      free (*ids); *ids = NULL;
   }

   while (sqldb_res_step (res) == 1) {
      char *email, *nick;
      uint64_t id;

      uint64_t newlen = *nitems + 1;
      if (emails) {
         char **tmp = realloc (*emails, (newlen + 1) * (sizeof *tmp));
         if (!tmp)
            goto errorexit;
         tmp[newlen - 1] = NULL;
         tmp[newlen] = NULL;
         *emails = tmp;
      }
      if (nicks) {
         char **tmp = realloc (*nicks, (newlen + 1) * (sizeof *tmp));
         if (!tmp)
            goto errorexit;
         tmp[newlen - 1] = NULL;
         tmp[newlen] = NULL;
         *nicks = tmp;
      }
      if (ids) {
         uint64_t *tmp = realloc ((*ids), (newlen + 1) * (sizeof *tmp));
         if (!tmp)
            goto errorexit;
         *ids = tmp;
      }

      if ((sqldb_scan_columns (res, sqldb_col_TEXT,   &email,
                                    sqldb_col_TEXT,   &nick,
                                    sqldb_col_UINT64, &id,
                                    sqldb_col_UNKNOWN))!=3)
         goto errorexit;

      if (emails) {
         (*emails)[newlen - 1] = email;
      } else {
         free (email);
      }

      if (nicks) {
         (*nicks)[newlen - 1] = nick;
      } else {
         free (nick);
      }

      if (ids) {
         (*ids)[newlen - 1] = id;
      }

      *nitems = newlen;
   }

   error = false;

errorexit:

   sqldb_res_del (res);

   return !error;
}

bool sqldb_auth_perms_grant_user (sqldb_t    *db,
                                  const char *email,
                                  const char *resource,
                                  uint64_t    perms)
{
   bool error = true;
   const char *qstring = NULL;
   uint64_t rc = 0;

   if (!db ||
       !SVALID (resource) || !SVALID (email))
      goto errorexit;

   if (!(qstring = sqldb_auth_query ("perms_user_grant"))) {
      LOG_ERR ("Failed to find query string [perms_user_grant]\n");
      goto errorexit;
   }

   rc = sqldb_exec_ignore (db, qstring, sqldb_col_TEXT, &resource,
                                        sqldb_col_TEXT, &email,
                                        sqldb_col_UINT64, &perms,
                                        sqldb_col_UNKNOWN);

   if (rc == (uint64_t)-1) {
      LOG_ERR ("Failed to execute [%s]:\n%s\n", qstring,
                                                sqldb_lasterr (db));
      goto errorexit;
   }

   error = false;

errorexit:

   return !error;
}

bool sqldb_auth_perms_revoke_user (sqldb_t   *db,
                                   const char *email,
                                   const char *resource,
                                   uint64_t    perms)
{
   bool error = true;
   const char *qstring = NULL;
   uint64_t rc = 0;

   if (!db ||
       !SVALID (resource) || !SVALID (email))
      goto errorexit;

   if (!(qstring = sqldb_auth_query ("perms_user_revoke"))) {
      LOG_ERR ("Failed to find query string [perms_user_revoke]\n");
      goto errorexit;
   }

   rc = sqldb_exec_ignore (db, qstring, sqldb_col_TEXT, &resource,
                                        sqldb_col_TEXT, &email,
                                        sqldb_col_UINT64, &perms,
                                        sqldb_col_UNKNOWN);

   if (rc == (uint64_t)-1) {
      LOG_ERR ("Failed to execute [%s]:\n%s\n", qstring,
                                                sqldb_lasterr (db));
      goto errorexit;
   }

   error = false;

errorexit:

   return !error;

}

bool sqldb_auth_perms_grant_group (sqldb_t    *db,
                                   const char *name,
                                   const char *resource,
                                   uint64_t    perms);

bool sqldb_auth_perms_revoke_group (sqldb_t   *db,
                                    const char *name,
                                    const char *resource,
                                    uint64_t    perms);

bool sqldb_auth_perms_get_group (sqldb_t     *db,
                                 uint64_t    *perms_dst,
                                 const char  *resource,
                                 const char  *name);

bool sqldb_auth_perms_get_user (sqldb_t      *db,
                                uint64_t     *perms_dst,
                                const char   *resource,
                                const char   *email);

bool sqldb_auth_perms_get_all (sqldb_t    *db,
                               uint64_t   *perms_dst,
                               const char *resource,
                               const char *email);
