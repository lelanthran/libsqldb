#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <inttypes.h>

#include <sys/types.h>
#include <unistd.h>

#include "sqldb_auth.h"
#include "sqldb_auth_query.h"
#include "sha-256.h"

#define LOG_ERR(...)      do {\
      fprintf (stderr, ":%s:%d: ", __FILE__, __LINE__);\
      fprintf (stderr, __VA_ARGS__);\
} while (0)

#define SVALID(s)   ((s && s[0]))

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

   if (!(sqldb_batch (db, init_sqlite_stmt, create_tables_stmt, NULL))) {
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
   for (size_t i=0; i<0x000f; i++) {
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
#ifndef PLATFORM_Windows
   extern int main (void);
#endif

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
#ifndef PLATFORM_Windows
   int (*ptr_main) (void) = main;
#else
   int (*ptr_main) (void) = NULL;
#endif
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

#if 0
   /* TEST CASE: Used to test unique constraints on session tables */
   memset (dst, 0, len);
   return;
#endif

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

bool sqldb_auth_session_valid (sqldb_t     *db,
                               const char   session_id[65],
                               char       **email_dst,
                               char       **nick_dst,
                               uint64_t    *flags_dst,
                               uint64_t    *id_dst)
{
   bool error = true;
   const char *qstring = NULL;
   sqldb_res_t *res = NULL;

    char       *l_nick_dst = NULL;
    char       *l_email_dst = NULL;
    uint64_t    l_flags_dst = 0;
    uint64_t    l_id_dst = 0;

   if (!db || !SVALID (session_id))
      goto errorexit;

   if (!(qstring = sqldb_auth_query ("session_valid"))) {
      LOG_ERR ("Failed to get query-string [session_valid]\n");
      goto errorexit;
   }

   if (!(res = sqldb_exec (db, qstring, sqldb_col_TEXT, &session_id,
                                        sqldb_col_UNKNOWN))) {
      LOG_ERR ("Failed to execute session query for session [%s]\n%s\n",
                session_id, sqldb_lasterr (db));
      goto errorexit;
   }

   if ((sqldb_res_step (res)) != 1) {
      LOG_ERR ("Failed to find a session for [%s]\n",
               session_id);
      goto errorexit;
   }

   if ((sqldb_scan_columns (res, sqldb_col_TEXT,    &l_email_dst,
                                 sqldb_col_TEXT,    &l_nick_dst,
                                 sqldb_col_UINT64,  &l_flags_dst,
                                 sqldb_col_UINT64,  &l_id_dst,
                                 sqldb_col_UNKNOWN)) != 4) {
      LOG_ERR ("Failed to scan 4 columns in for session [%s]\n",
               session_id);
      goto errorexit;
   }

   if (email_dst) {
      (*email_dst) = l_email_dst;
      l_email_dst = NULL;
   }

   if (nick_dst) {
      (*nick_dst) = l_nick_dst;
      l_nick_dst = NULL;
   }

   if (flags_dst)
      (*flags_dst) = l_flags_dst;

   if (id_dst)
      (*id_dst) = l_id_dst;

   error = false;

errorexit:

   free (l_nick_dst);

   sqldb_res_del (res);

   return !error;
}

bool sqldb_auth_session_authenticate (sqldb_t    *db,
                                      const char *email,
                                      const char *password,
                                      char        sess_id_dst[65])
{
   bool error = true;
   const char *qstring = NULL;
   uint64_t rc = 0;
   size_t retries = 0;
   sqldb_res_t *res = NULL;

   char *salt = NULL,
        *nick = NULL,
        *dbhash = NULL;

   char phash[65];
   uint8_t sess_id_bin[32];

   memset (phash, 0, sizeof phash);

   if (!db || !SVALID (email) || !SVALID (password))
      goto errorexit;

   if (!(qstring = sqldb_auth_query ("user_salt_nick_hash"))) {
      LOG_ERR ("Failed to get query-string for [user_salt_nick]\n");
      goto errorexit;
   }

   if (!(res = sqldb_exec (db, qstring, sqldb_col_TEXT, &email,
                                        sqldb_col_UNKNOWN))) {
      LOG_ERR ("Failed to execute [%s] with #1=[%s]\n%s\n",
               qstring, email, sqldb_lasterr (db));
      goto errorexit;
   }

   if ((sqldb_res_step (res)) != 1) {
      LOG_ERR ("Failed to step results set for [%s] #1 = %s: %s\n",
            qstring, email, sqldb_lasterr (db));
      goto errorexit;
   }

   if (!(sqldb_scan_columns (res, sqldb_col_TEXT, &salt,
                                  sqldb_col_TEXT, &nick,
                                  sqldb_col_TEXT, &dbhash,
                                  sqldb_col_UNKNOWN))) {
      LOG_ERR ("Failed to scan columns set for [%s] #1 = %s: %s\n",
            qstring, email, sqldb_lasterr (db));
      goto errorexit;
   }

   sqldb_res_del (res);
   res = NULL;

   if (!(make_password_hash (phash, salt, email, nick, password))) {
      LOG_ERR ("Failed to make password hash for [%s]\n", email);
      goto errorexit;
   }

   if ((strncmp (dbhash, phash, sizeof phash))!=0)
      goto errorexit;

   if (!(qstring = sqldb_auth_query ("user_update_session_id"))) {
      LOG_ERR ("Failed to find query-string for [user_update_session_id]\n");
      goto errorexit;
   }

   // Make 100 attempts at most to generate a session ID that is unique.
   // We give up after that (something is wrong).
   for (retries=0; retries<100; retries++) {
      sqldb_auth_random_bytes (sess_id_bin, 32);
      memset (sess_id_dst, 0, 65);
      for (size_t i=0; i<32; i++) {
         sprintf (&sess_id_dst[i*2], "%02x", sess_id_bin[i]);
      }

      rc = sqldb_exec_ignore (db, qstring, sqldb_col_TEXT, &email,
                                           sqldb_col_TEXT, &sess_id_dst,
                                           sqldb_col_UNKNOWN);
      if (rc != (uint64_t)-1)
         break;
   }

   if (rc == (uint64_t)-1) {
      LOG_ERR ("Failed to update db with new session ID after %zu attempts "
               "[%s:%s]\n%s\n",
               retries, email, sess_id_dst, sqldb_lasterr (db));
      goto errorexit;
   }

   error = false;

errorexit:

   free (salt);
   free (nick);
   free (dbhash);

   sqldb_res_del (res);

   return !error;
}

bool sqldb_auth_session_invalidate (sqldb_t      *db,
                                    const char   *email,
                                    const char    session_id[65])
{
   const char *qstring = NULL;
   uint64_t rc = 0;

   if (!db || !SVALID (email) || !SVALID (session_id))
      return false;

   if (!(qstring = sqldb_auth_query ("session_invalidate")))
      return false;

   rc = sqldb_exec_ignore (db, qstring, sqldb_col_TEXT, &email,
                                        sqldb_col_TEXT, &session_id,
                                        sqldb_col_UNKNOWN);

   if (rc==(uint64_t)-1) {
      LOG_ERR ("Failed to invalidate session [%s/%s]: %s\n",
               email, session_id,
               sqldb_lasterr (db));
      return false;
   }

   return true;

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
   if (ret==(uint64_t)-1 || ret==0) {
      LOG_ERR ("Failed to create user [%s]: %s\n", email,
                                                   sqldb_lasterr (db));
      goto errorexit;
   }

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

   if (rc==(uint64_t)-1) {
      LOG_ERR ("Failed to remove group [%s]: %s\n", email,
                                                    sqldb_lasterr (db));
      return false;
   }

   return true;
}

bool sqldb_auth_user_info (sqldb_t    *db,
                           const char *email,
                           uint64_t   *id_dst,
                           uint64_t   *flags_dst,
                           char      **nick_dst,
                           char        session_dst[65])
{
   bool error = true;
   const char *qstring = NULL;
   sqldb_res_t *res = NULL;
   char *tmp_sess = NULL;

   uint64_t   l_id_dst = 0;
   uint64_t   l_flags_dst = 0;
   char      *l_nick_dst = NULL;

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

   if ((sqldb_scan_columns (res, sqldb_col_UINT64, &l_id_dst,
                                 sqldb_col_TEXT,   &l_nick_dst,
                                 sqldb_col_TEXT,   &tmp_sess,
                                 sqldb_col_UINT64, &l_flags_dst,
                                 sqldb_col_UNKNOWN))!=4) {
      LOG_ERR ("Scan failure: [%s]\n", qstring);
      goto errorexit;
   }

   if (nick_dst) {
      (*nick_dst) = l_nick_dst;
      l_nick_dst = NULL;
   }

   if (flags_dst)
      (*flags_dst) = l_flags_dst;

   if (id_dst)
      (*id_dst) = l_id_dst;

   strncpy (session_dst, tmp_sess, 65);
   session_dst[64] = 0;

   error = false;

errorexit:

   free (tmp_sess);
   free (l_nick_dst);
   sqldb_res_del (res);

   return !error;
}

bool sqldb_auth_user_mod (sqldb_t    *db,
                          const char *old_email,
                          const char *new_email,
                          const char *nick,
                          const char *password)
{
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
      return false;

   if (!(qstring = sqldb_auth_query ("user_mod")))
      return false;

   sqldb_auth_random_bytes (salt, sizeof salt);
   STRINGIFY (sz_salt, sizeof sz_salt, salt, 32);
#undef STRINGIFY


   if (!(make_password_hash (sz_hash, sz_salt, new_email, nick, password)))
      return false;

   ret = sqldb_exec_ignore (db, qstring, sqldb_col_TEXT,    &old_email,
                                         sqldb_col_TEXT,    &new_email,
                                         sqldb_col_TEXT,    &nick,
                                         sqldb_col_TEXT,    &ptr_salt,
                                         sqldb_col_TEXT,    &ptr_hash,
                                         sqldb_col_UNKNOWN);

   if (ret==(uint64_t)-1) {
      LOG_ERR ("Failed to modify user [%s]: %s\n", old_email,
                                                   sqldb_lasterr (db));
      return false;
   }

   return true;
}


static bool sqldb_auth_user_flags (sqldb_t *db, const char *qname,
                                                const char *email,
                                                uint64_t flags)
{
   const char *qstring = NULL;
   uint64_t rc = 0;

   if (!db || !SVALID (email) || !SVALID (qname))
      return false;

   if (!(qstring = sqldb_auth_query (qname))) {
      LOG_ERR ("Failed to get query-string for [%s][%s]\n", qname, email);
      return false;
   }

   rc = sqldb_exec_ignore (db, qstring, sqldb_col_TEXT,     &email,
                                        sqldb_col_UINT64,   &flags,
                                        sqldb_col_UNKNOWN);

   if (rc==(uint64_t)-1) {
      LOG_ERR ("Failed to execute [%s] for user [%s]\n", qstring, email);
      return false;
   }

   return true;
}

bool sqldb_auth_user_flags_set (sqldb_t *db, const char *email,
                                             uint64_t flags)
{
   return sqldb_auth_user_flags (db, "user_flags_set", email, flags);
}

bool sqldb_auth_user_flags_clear (sqldb_t *db, const char *email,
                                               uint64_t flags)
{
   return sqldb_auth_user_flags (db, "user_flags_clear", email, flags);
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
   if (ret==(uint64_t)-1) {
      LOG_ERR ("Failed to create group [%s]: %s\n", name,
                                                    sqldb_lasterr (db));
   }


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

   if (rc==(uint64_t)-1) {
      LOG_ERR ("Failed to remove group [%s]: %s\n", name,
                                                    sqldb_lasterr (db));
      return false;
   }

   return true;
}

bool sqldb_auth_group_info (sqldb_t    *db,
                            const char *name,
                            uint64_t   *id_dst,
                            char      **description_dst)
{
   bool error = true;
   const char *qstring = NULL;
   sqldb_res_t *res = NULL;

   uint64_t   l_id_dst = 0;
   char      *l_description_dst = NULL;

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

   if ((sqldb_scan_columns (res, sqldb_col_UINT64, &l_id_dst,
                                 sqldb_col_TEXT,   &l_description_dst,
                                 sqldb_col_UNKNOWN))!=2) {
      LOG_ERR ("Scan failure: [%s]\n", qstring);
      goto errorexit;
   }

   if (description_dst) {
      (*description_dst) = l_description_dst;
      l_description_dst = NULL;
   }

   if (id_dst)
      (*id_dst) = l_id_dst;

   error = false;

errorexit:

   free (l_description_dst);

   sqldb_res_del (res);

   return !error;

}

bool sqldb_auth_group_mod (sqldb_t    *db,
                           const char *oldname,
                           const char *newname,
                           const char *description)
{
   uint64_t rc = 0;
   const char *qstring = NULL;

   if (!db ||
       !SVALID (oldname) || !SVALID (newname) || !SVALID (description))
      return false;

   if (!(qstring = sqldb_auth_query ("group_mod"))) {
      LOG_ERR ("Failed to find query_string [group_mod]: %s\n",
                sqldb_lasterr (db));
      return false;
   }

   rc = sqldb_exec_ignore (db, qstring, sqldb_col_TEXT, &oldname,
                                        sqldb_col_TEXT, &newname,
                                        sqldb_col_TEXT, &description,
                                        sqldb_col_UNKNOWN);

   if (rc==(uint64_t)-1) {
      LOG_ERR ("Failed to modify group [%s]: %s\n", oldname,
                                                    sqldb_lasterr (db));
      return false;
   }

   return true;
}

bool sqldb_auth_group_adduser (sqldb_t    *db,
                               const char *name, const char *email)
{
   const char *qstring = NULL;
   uint64_t rc = 0;

   if (!(qstring = sqldb_auth_query ("group_adduser"))) {
      LOG_ERR ("Failed to find query_string [group_adduser]: %s\n",
                sqldb_lasterr (db));
      return false;
   }

   rc = sqldb_exec_ignore (db, qstring, sqldb_col_TEXT, &email,
                                        sqldb_col_TEXT, &name,
                                        sqldb_col_UNKNOWN);
   if (rc==(uint64_t)-1) {
      LOG_ERR ("Failed to add user to group [%s/%s]: %s\n",
               email,
               name,
               sqldb_lasterr (db));
      return false;
   }

   return true;
}

bool sqldb_auth_group_rmuser (sqldb_t    *db,
                              const char *name, const char *email)
{
   const char *qstring = NULL;
   uint64_t rc = 0;

   if (!(qstring = sqldb_auth_query ("group_rmuser")))
      return false;

   rc = sqldb_exec_ignore (db, qstring, sqldb_col_TEXT, &name,
                                        sqldb_col_TEXT, &email,
                                        sqldb_col_UNKNOWN);

   if (rc==(uint64_t)-1) {
      LOG_ERR ("Failed to remove user from group [%s/%s]: %s\n",
               email, name, sqldb_lasterr (db));
      return false;
   }

   return true;
}

bool sqldb_auth_user_find (sqldb_t    *db,
                           const char *email_pattern,
                           const char *nick_pattern,
                           uint64_t   *nitems_dst,
                           char     ***emails_dst,
                           char     ***nicks_dst,
                           uint64_t  **flags_dst,
                           uint64_t  **ids_dst)
{
#define BASE_QS      "SELECT c_email, c_nick, c_id, c_flags FROM t_user "
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

   *nitems_dst = 0;

   if (emails_dst) {
      free (*emails_dst); *emails_dst = NULL;
   }
   if (nicks_dst) {
      free (*nicks_dst); *nicks_dst = NULL;
   }
   if (ids_dst) {
      free (*ids_dst); *ids_dst = NULL;
   }
   if (flags_dst) {
      free (*flags_dst); *flags_dst = NULL;
   }

   while (sqldb_res_step (res) == 1) {
      char *email, *nick;
      uint64_t id, flag;

      uint64_t newlen = *nitems_dst + 1;
      if (emails_dst) {
         char **tmp = realloc (*emails_dst, (newlen + 1) * (sizeof *tmp));
         if (!tmp)
            goto errorexit;
         tmp[newlen - 1] = NULL;
         tmp[newlen] = NULL;
         *emails_dst = tmp;
      }
      if (nicks_dst) {
         char **tmp = realloc (*nicks_dst, (newlen + 1) * (sizeof *tmp));
         if (!tmp)
            goto errorexit;
         tmp[newlen - 1] = NULL;
         tmp[newlen] = NULL;
         *nicks_dst = tmp;
      }
      if (ids_dst) {
         uint64_t *tmp = realloc ((*ids_dst), (newlen + 1) * (sizeof *tmp));
         if (!tmp)
            goto errorexit;
         *ids_dst = tmp;
      }
      if (flags_dst) {
         uint64_t *tmp = realloc ((*flags_dst), (newlen + 1) * (sizeof *tmp));
         if (!tmp)
            goto errorexit;
         *flags_dst = tmp;
      }

      if ((sqldb_scan_columns (res, sqldb_col_TEXT,   &email,
                                    sqldb_col_TEXT,   &nick,
                                    sqldb_col_UINT64, &id,
                                    sqldb_col_UINT64, &flag,
                                    sqldb_col_UNKNOWN))!=4)
         goto errorexit;

      if (emails_dst) {
         (*emails_dst)[newlen - 1] = email;
      } else {
         free (email);
      }

      if (nicks_dst) {
         (*nicks_dst)[newlen - 1] = nick;
      } else {
         free (nick);
      }

      if (ids_dst) {
         (*ids_dst)[newlen - 1] = id;
      }

      if (flags_dst) {
         (*flags_dst)[newlen - 1] = flag;
      }

      *nitems_dst = newlen;
   }

   error = false;

errorexit:

   if (error) {
      if (emails_dst) {
         for (size_t i=0; emails_dst && *emails_dst && (*emails_dst)[i]; i++) {
            free ((*emails_dst)[i]);
         }
         free (*emails_dst); *emails_dst = NULL;
      }

      if (nicks_dst) {
         for (size_t i=0; nicks_dst && *nicks_dst && (*nicks_dst)[i]; i++) {
            free ((*nicks_dst)[i]);
         }
         free (*nicks_dst); *nicks_dst = NULL;
      }

      if (ids_dst) {
         free (*ids_dst); *ids_dst = NULL;
      }

      if (flags_dst) {
         free (*flags_dst); *flags_dst = NULL;
      }

   }

   sqldb_res_del (res);

   return !error;
}

bool sqldb_auth_group_find (sqldb_t    *db,
                            const char *name_pattern,
                            const char *description_pattern,
                            uint64_t   *nitems_dst,
                            char     ***names_dst,
                            char     ***descriptions_dst,
                            uint64_t  **ids_dst)
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

   *nitems_dst = 0;

   if (names_dst) {
      free (*names_dst); *names_dst = NULL;
   }
   if (descriptions_dst) {
      free (*descriptions_dst); *descriptions_dst = NULL;
   }
   if (ids_dst) {
      free (*ids_dst); *ids_dst = NULL;
   }

   while (sqldb_res_step (res) == 1) {
      char *name, *description;
      uint64_t id;

      uint32_t ncols = 0;

      uint64_t newlen = *nitems_dst + 1;
      if (names_dst) {
         char **tmp = realloc (*names_dst, (newlen + 1) * (sizeof *tmp));
         if (!tmp) {
            LOG_ERR ("OOM error\n");
            goto errorexit;
         }
         tmp[newlen - 1] = NULL;
         tmp[newlen] = NULL;
         *names_dst = tmp;
      }
      if (descriptions_dst) {
         char **tmp = realloc (*descriptions_dst, (newlen + 1) * (sizeof *tmp));
         if (!tmp) {
            LOG_ERR ("OOM error\n");
            goto errorexit;
         }
         tmp[newlen - 1] = NULL;
         tmp[newlen] = NULL;
         *descriptions_dst = tmp;
      }
      if (ids_dst) {
         uint64_t *tmp = realloc ((*ids_dst), (newlen + 1) * (sizeof *tmp));
         if (!tmp) {
            LOG_ERR ("OOM error\n");
            goto errorexit;
         }
         *ids_dst = tmp;
      }

      if ((ncols = (sqldb_scan_columns (res, sqldb_col_TEXT,   &name,
                                             sqldb_col_TEXT,   &description,
                                             sqldb_col_UINT64, &id,
                                             sqldb_col_UNKNOWN)))!=3) {
         LOG_ERR ("Scan failure [%u, expected 3]\n", ncols);
         goto errorexit;
      }

      if (names_dst) {
         (*names_dst)[newlen - 1] = name;
      } else {
         free (name);
      }

      if (descriptions_dst) {
         (*descriptions_dst)[newlen - 1] = description;
      } else {
         free (description);
      }

      if (ids_dst) {
         (*ids_dst)[newlen - 1] = id;
      }

      *nitems_dst = newlen;
   }

   error = false;

errorexit:

   if (error) {
      if (names_dst) {
         for (size_t i=0; names_dst && (*names_dst) && (*names_dst)[i]; i++) {
            free ((*names_dst)[i]);
         }
         free (*names_dst); *names_dst = NULL;
      }

      if (descriptions_dst) {
         for (size_t i=0; descriptions_dst && (*descriptions_dst) && (*descriptions_dst)[i]; i++) {
            free ((*descriptions_dst)[i]);
         }
         free (*descriptions_dst); *descriptions_dst = NULL;
      }
   }

   sqldb_res_del (res);

   return !error;

}

bool sqldb_auth_group_members (sqldb_t    *db,
                               const char *name,
                               uint64_t   *nitems_dst,
                               char     ***emails_dst,
                               char     ***nicks_dst,
                               uint64_t  **flags_dst,
                               uint64_t  **ids_dst)

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

   *nitems_dst = 0;

   if (emails_dst) {
      free (*emails_dst); *emails_dst = NULL;
   }
   if (nicks_dst) {
      free (*nicks_dst); *nicks_dst = NULL;
   }
   if (ids_dst) {
      free (*ids_dst); *ids_dst = NULL;
   }
   if (flags_dst) {
      free (*flags_dst); *flags_dst = NULL;
   }

   while (sqldb_res_step (res) == 1) {
      char *email, *nick;
      uint64_t id, flag;

      uint64_t newlen = *nitems_dst + 1;
      if (emails_dst) {
         char **tmp = realloc (*emails_dst, (newlen + 1) * (sizeof *tmp));
         if (!tmp)
            goto errorexit;
         tmp[newlen - 1] = NULL;
         tmp[newlen] = NULL;
         *emails_dst = tmp;
      }
      if (nicks_dst) {
         char **tmp = realloc (*nicks_dst, (newlen + 1) * (sizeof *tmp));
         if (!tmp)
            goto errorexit;
         tmp[newlen - 1] = NULL;
         tmp[newlen] = NULL;
         *nicks_dst = tmp;
      }
      if (ids_dst) {
         uint64_t *tmp = realloc ((*ids_dst), (newlen + 1) * (sizeof *tmp));
         if (!tmp)
            goto errorexit;
         *ids_dst = tmp;
      }
      if (flags_dst) {
         uint64_t *tmp = realloc ((*flags_dst), (newlen + 1) * (sizeof *tmp));
         if (!tmp)
            goto errorexit;
         *flags_dst = tmp;
      }

      if ((sqldb_scan_columns (res, sqldb_col_TEXT,   &email,
                                    sqldb_col_TEXT,   &nick,
                                    sqldb_col_UINT64, &id,
                                    sqldb_col_UINT64, &flag,
                                    sqldb_col_UNKNOWN))!=4)
         goto errorexit;

      if (emails_dst) {
         (*emails_dst)[newlen - 1] = email;
      } else {
         free (email);
      }

      if (nicks_dst) {
         (*nicks_dst)[newlen - 1] = nick;
      } else {
         free (nick);
      }

      if (ids_dst) {
         (*ids_dst)[newlen - 1] = id;
      }

      if (flags_dst) {
         (*flags_dst)[newlen - 1] = flag;
      }

      *nitems_dst = newlen;
   }

   error = false;

errorexit:

   if (error) {
      if (emails_dst) {
         for (size_t i=0; emails_dst && *emails_dst && (*emails_dst)[i]; i++) {
            free ((*emails_dst)[i]);
         }
         free (*emails_dst); *emails_dst = NULL;
      }

      if (nicks_dst) {
         for (size_t i=0; nicks_dst && *nicks_dst && (*nicks_dst)[i]; i++) {
            free ((*nicks_dst)[i]);
         }
         free (*nicks_dst); *nicks_dst = NULL;
      }

      if (ids_dst) {
         free (*ids_dst); *ids_dst = NULL;
      }

      if (flags_dst) {
         free (*flags_dst); *flags_dst = NULL;
      }

   }

   sqldb_res_del (res);

   return !error;
}

bool sqldb_auth_perms_grant_user (sqldb_t    *db,
                                  const char *email,
                                  const char *resource,
                                  uint64_t    perms)
{
   const char *qstring = NULL;
   uint64_t rc = 0;

   if (!db ||
       !SVALID (resource) || !SVALID (email))
      return false;

   if (!(qstring = sqldb_auth_query ("perms_user_grant"))) {
      LOG_ERR ("Failed to find query string [perms_user_grant]\n");
      return false;
   }

   rc = sqldb_exec_ignore (db, qstring, sqldb_col_TEXT,     &resource,
                                        sqldb_col_TEXT,     &email,
                                        sqldb_col_UINT64,   &perms,
                                        sqldb_col_UNKNOWN);

   if (rc == (uint64_t)-1) {
      LOG_ERR ("Failed to execute [%s]:\n%s\n", qstring,
                                                sqldb_lasterr (db));
      return false;
   }

   return true;
}

bool sqldb_auth_perms_revoke_user (sqldb_t   *db,
                                   const char *email,
                                   const char *resource,
                                   uint64_t    perms)
{
   const char *qstring = NULL;
   uint64_t rc = 0;

   if (!db ||
       !SVALID (resource) || !SVALID (email))
      return false;

   if (!(qstring = sqldb_auth_query ("perms_user_revoke"))) {
      LOG_ERR ("Failed to find query string [perms_user_revoke]\n");
      return false;
   }

   rc = sqldb_exec_ignore (db, qstring, sqldb_col_TEXT,     &resource,
                                        sqldb_col_TEXT,     &email,
                                        sqldb_col_UINT64,   &perms,
                                        sqldb_col_UNKNOWN);

   if (rc == (uint64_t)-1) {
      LOG_ERR ("Failed to execute [%s]:\n%s\n", qstring,
                                                sqldb_lasterr (db));
      return false;
   }

   return true;
}

bool sqldb_auth_perms_grant_group (sqldb_t    *db,
                                   const char *name,
                                   const char *resource,
                                   uint64_t    perms)
{
   const char *qstring = NULL;
   uint64_t rc = 0;

   if (!db ||
       !SVALID (resource) || !SVALID (name))
      return false;

   if (!(qstring = sqldb_auth_query ("perms_group_grant"))) {
      LOG_ERR ("Failed to find query string [perms_group_grant]\n");
      return false;
   }

   rc = sqldb_exec_ignore (db, qstring, sqldb_col_TEXT,     &resource,
                                        sqldb_col_TEXT,     &name,
                                        sqldb_col_UINT64,   &perms,
                                        sqldb_col_UNKNOWN);

   if (rc == (uint64_t)-1) {
      LOG_ERR ("Failed to execute [%s]:\n%s\n", qstring,
                                                sqldb_lasterr (db));
      return false;
   }

   return true;
}

bool sqldb_auth_perms_revoke_group (sqldb_t   *db,
                                    const char *name,
                                    const char *resource,
                                    uint64_t    perms)
{
   const char *qstring = NULL;
   uint64_t rc = 0;

   if (!db ||
       !SVALID (resource) || !SVALID (name))
      return false;

   if (!(qstring = sqldb_auth_query ("perms_group_revoke"))) {
      LOG_ERR ("Failed to find query string [perms_group_revoke]\n");
      return false;
   }

   rc = sqldb_exec_ignore (db, qstring, sqldb_col_TEXT,     &resource,
                                        sqldb_col_TEXT,     &name,
                                        sqldb_col_UINT64,   &perms,
                                        sqldb_col_UNKNOWN);

   if (rc == (uint64_t)-1) {
      LOG_ERR ("Failed to execute [%s]:\n%s\n", qstring,
                                                sqldb_lasterr (db));
      return false;
   }

   return true;
}

bool sqldb_auth_perms_get_user (sqldb_t      *db,
                                uint64_t     *perms_dst,
                                const char   *email,
                                const char   *resource)
{
   bool error = true;
   sqldb_res_t *res = NULL;
   const char *qstring = NULL;

   if (!db || !perms_dst ||
       !SVALID (resource) || !SVALID (email))
      return false;

   if (!(qstring = sqldb_auth_query ("perms_get_user"))) {
      LOG_ERR ("Failed to find query for [perms_get_user]\n");
      goto errorexit;
   }

   if (!(res = sqldb_exec (db, qstring, sqldb_col_TEXT,  &email,
                                        sqldb_col_TEXT,  &resource,
                                        sqldb_col_UNKNOWN))) {
      LOG_ERR ("Failed to execute query [%s]:%s\n", qstring,
                                                    sqldb_lasterr (db));
      goto errorexit;
   }

   if ((sqldb_res_step (res))!=1) {
      LOG_ERR ("Failed to retrieve query results for [%s]\n"
               "#1=%s, #2=%s\n%s\n%s\n",
                qstring, email, resource, sqldb_res_lasterr (res),
                                          sqldb_lasterr (db));
      goto errorexit;
   }

   if ((sqldb_scan_columns (res, sqldb_col_UINT64, perms_dst,
                                 sqldb_col_UNKNOWN))!=1) {

      LOG_ERR ("Failed to scan query results for [%s]\n"
               "#1=%s, #2%s\n%s\n%s\n",
                qstring, email, resource, sqldb_res_lasterr (res),
                                          sqldb_lasterr (db));
      goto errorexit;
   }

   error = false;

errorexit:

   sqldb_res_del (res);

   return !error;
}

bool sqldb_auth_perms_get_group (sqldb_t     *db,
                                 uint64_t    *perms_dst,
                                 const char  *name,
                                 const char  *resource)
{
   bool error = true;
   sqldb_res_t *res = NULL;
   const char *qstring = NULL;

   if (!db || !perms_dst ||
       !SVALID (resource) || !SVALID (name))
      return false;

   if (!(qstring = sqldb_auth_query ("perms_get_group"))) {
      LOG_ERR ("Failed to find query for [perms_get_group]\n");
      goto errorexit;
   }

   if (!(res = sqldb_exec (db, qstring, sqldb_col_TEXT,  &name,
                                        sqldb_col_TEXT,  &resource,
                                        sqldb_col_UNKNOWN))) {
      LOG_ERR ("Failed to execute query [%s]:%s\n", qstring,
                                                    sqldb_lasterr (db));
      goto errorexit;
   }

   if ((sqldb_res_step (res))!=1) {
      LOG_ERR ("Failed to retrieve query results for [%s]\n"
               "#1=%s, #2%s\n%s\n%s\n",
                qstring, name, resource, sqldb_res_lasterr (res),
                                         sqldb_lasterr (db));
      goto errorexit;
   }

   if ((sqldb_scan_columns (res, sqldb_col_UINT64, perms_dst,
                                 sqldb_col_UNKNOWN))!=1) {

      LOG_ERR ("Failed to scan query results for [%s]\n"
               "#1=%s, #2%s\n%s\n%s\n",
                qstring, name, resource, sqldb_res_lasterr (res),
                                         sqldb_lasterr (db));
      goto errorexit;
   }

   error = false;

errorexit:

   sqldb_res_del (res);

   return !error;
}

bool sqldb_auth_perms_get_all (sqldb_t    *db,
                               uint64_t   *perms_dst,
                               const char *email,
                               const char *resource)
{
   bool error = true;
   sqldb_res_t *res = NULL;
   const char *qstring = NULL;
   int rc = 0;

   if (!db || !perms_dst ||
       !SVALID (resource) || !SVALID (email))
      return false;

   if (!(sqldb_auth_perms_get_user (db, perms_dst, email, resource))) {
      LOG_ERR ("Failed to get permissions for user [%s]\n", email);
      goto errorexit;
   }

   if (!(qstring = sqldb_auth_query ("perms_get_all"))) {
      LOG_ERR ("Failed to find query for [perms_get_all]\n");
      goto errorexit;
   }

   if (!(res = sqldb_exec (db, qstring, sqldb_col_TEXT,  &email,
                                        sqldb_col_TEXT,  &resource,
                                        sqldb_col_UNKNOWN))) {
      LOG_ERR ("Failed to execute query [%s]:%s\n", qstring,
                                                    sqldb_lasterr (db));
      goto errorexit;
   }

   while ((rc = sqldb_res_step (res)) > 0) {
      uint64_t tmp = 0;
      if ((sqldb_scan_columns (res, sqldb_col_UINT64, &tmp,
                                    sqldb_col_UNKNOWN))!=1) {
         LOG_ERR ("Failed to scan query results for [%s]\n"
                  "#1=%s, #2%s\n%s\n%s\n",
                     qstring, email, resource, sqldb_res_lasterr (res),
                     sqldb_lasterr (db));
         goto errorexit;
      }
      *perms_dst = (*perms_dst) | tmp;
   }

   if (rc < 0) {
      LOG_ERR ("Failed to retrieve query results for [%s]\n"
               "#1=%s, #2%s\n%s\n%s\n",
                qstring, email, resource, sqldb_res_lasterr (res),
                                          sqldb_lasterr (db));
      goto errorexit;
   }

   error = false;

errorexit:

   sqldb_res_del (res);

   return !error;
}
