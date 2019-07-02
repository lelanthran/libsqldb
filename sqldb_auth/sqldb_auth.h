
#ifndef H_SQLDB_AUTH
#define H_SQLDB_AUTH

#include <stdbool.h>
#include <stdint.h>

/* TODO:
 * What ction should be default on deletion of foreign keys? If we cascade
 * group deletes all that happens is that sometimes a user will find
 * themselves in no group at all. If we cascade group_mapping deletes then
 * a mapping delete is going to remove the user from the database.
 */
#include "sqldb/sqldb.h"

/* ********************************************************************
 * This is the authentication and authorization module. This module provides
 * a minimal session management and access control system using the
 * specified database (see sqldb.h).
 *
 * This module allows the caller to:
 *    1. Create new users and groups,
 *    2. Modify users (their password, email, etc),
 *    3. Add and remove users to and from groups,
 *    4. Authenticate users and create a timed session for future
 *       requests,
 *    5. Add and remove user/group access to named resources,
 *    6. Specify different levels of access for both resources and
 *       operations.
 *
 * Note that this module does not perform any authorisation of the caller
 * before executing any database action. The caller must use the
 * permissions functions in this module to ensure that the user is
 * properly authorised.
 */

#ifdef __cplusplus
extern "C" {
#endif

   ///////////////////////////////////////////////////////////////////////

   // Initialises the database by creating the schema needed to support
   // authorisation access.
   bool sqldb_auth_initdb (sqldb_t *db);

   // Return a random seed.
   uint32_t sqldb_auth_random_seed (void);

   // Return random bytes
   void sqldb_auth_random_bytes (void *dst, size_t len);

   ///////////////////////////////////////////////////////////////////////

   // Authenticates the session specified against the user in the
   // database, returns the user info in nick and id if the specified
   // session is in the database and valid.
   //
   // The caller must free the strings in email_dst and nick_dst
   // regardless of the return value. This function will first free the
   // strings stored at that location before populating them with new
   // values.
   bool sqldb_auth_session_valid (sqldb_t     *db,
                                  const char  *email,
                                  const char   session_id[65],
                                  char       **nick_dst,
                                  uint64_t    *flags_dst,
                                  uint64_t    *id_dst);

   // Creates a new session, returns the session ID in the sess_id_dst
   // array. Returns true on success and false on error.
   bool sqldb_auth_session_authenticate (sqldb_t    *db,
                                         const char *email,
                                         const char *password,
                                         char        sess_id_dst[65]);

   // Invalidates the session specified by session for user specified by email.
   // If the session is not valid for that user then no action is taken.
   bool sqldb_auth_session_invalidate (sqldb_t      *db,
                                       const char   *email,
                                       const char    session_id[65]);

   ///////////////////////////////////////////////////////////////////////

   // Create a new user, returns the user ID. Returns (uint64_t)-1 on
   // error.
   uint64_t sqldb_auth_user_create (sqldb_t    *db,
                                    const char *email,
                                    const char *nick,
                                    const char *password);

   // Removes a user from the database.
   bool sqldb_auth_user_rm (sqldb_t *db, const char *email);

   // Gets the user account information for the specified user. Returns
   // true on success and false on failure. The string destinations nick
   // and session must be freed by the caller regardless of the return
   // value. If they are not-NULL on entry to this function, this function
   // will free them before allocation storage for the output parameters.
   bool sqldb_auth_user_info (sqldb_t    *db,
                              const char *email,
                              uint64_t   *id_dst,
                              uint64_t   *flags,
                              char      **nick_dst,
                              char        session_dst[65]);

   // Updates the non-NULL parameters in the database. Returns true on
   // success and false on error. Uses the old_email parameter to find the
   // record to update.
   bool sqldb_auth_user_mod (sqldb_t    *db,
                             const char *old_email,
                             const char *new_email,
                             const char *nick,
                             const char *password);


   ///////////////////////////////////////////////////////////////////////

   // Set the flags in 'flags' for the user specified by email.
   bool sqldb_auth_user_flags_set (sqldb_t *db, const char *email,
                                                uint64_t flags);

   // Clear the flags in 'flags' for the user specified by email.
   bool sqldb_auth_user_flags_clear (sqldb_t *db, const char *email,
                                                  uint64_t flags);

   ///////////////////////////////////////////////////////////////////////

   // Create a new group, returns the group ID.
   uint64_t sqldb_auth_group_create (sqldb_t    *db,
                                     const char *name,
                                     const char *description);

   // Removes a group from the database.
   bool sqldb_auth_group_rm (sqldb_t *db, const char *name);

   // Gets the group account information for the specified group. Returns
   // true on success and false on failure. The string destination
   // 'description' must be freed by the caller regardless of the return
   // value. If it is not NULL on entry to this function, this function will
   // free it before allocation storage for the description.
   bool sqldb_auth_group_info (sqldb_t    *db,
                               const char *name,
                               uint64_t   *id_dst,
                               char      **description);

   // Updates the non-NULL parameters in the database. Returns true on
   // success and false on error. Uses the name parameter to find the
   // record to update.
   bool sqldb_auth_group_mod (sqldb_t    *db,
                              const char *oldname,
                              const char *newname,
                              const char *description);

   ///////////////////////////////////////////////////////////////////////

   // Add the specified user to the specified group. Returns true on
   // success and false on failure.
   bool sqldb_auth_group_adduser (sqldb_t    *db,
                                  const char *name, const char *email);

   // Remove the specified user from the specified group. Returns true on
   // success and false on failure.
   bool sqldb_auth_group_rmuser (sqldb_t    *db,
                                 const char *name, const char *email);


   ///////////////////////////////////////////////////////////////////////

   // Generates a list of user records from the database that match the
   // email_pattern or the nick_pattern. If either pattern is NULL it is
   // ignored.
   //
   // The results are returned in three arrays. The caller must free all
   // three arrays.
   //
   // The number of results is stored in nitems.
   //
   // The emails and nicks string arrays are allocated to store the number
   // of strings in the results set for the respective columns (emails and
   // nicks) and each array is terminated with a NULL pointer. The caller
   // must free both the array as well as each element of the array, for
   // both the arrays.
   //
   // The ids array stores each ID and the entire array (but not each
   // element) must be freed by the caller.
   bool sqldb_auth_user_find (sqldb_t    *db,
                              const char *email_pattern,
                              const char *nick_pattern,
                              uint64_t   *nitems,
                              char     ***emails,
                              char     ***nicks,
                              uint64_t  **flags,
                              uint64_t  **ids);

   // Generates a list of group records from the database that match the
   // email_pattern or the nick_pattern. If either pattern is NULL it is
   // ignored.
   //
   // The results are returned in three arrays. The caller must free all
   // three arrays.
   //
   // The number of results is stored in nitems.
   //
   // The names and descriptions string arrays are allocated to store the
   // number of strings in the results set for the respective columns (names
   // and descriptions) and each array is terminated with a NULL pointer.
   // The caller must free both the array as well as each element of the
   // array, for both the arrays.
   //
   // The ids array stores each ID and the entire array (but not each
   // element) must be freed by the caller.
   bool sqldb_auth_group_find (sqldb_t    *db,
                               const char *name_pattern,
                               const char *description_pattern,
                               uint64_t   *nitems,
                               char     ***names,
                               char     ***descriptions,
                               uint64_t  **ids);

   // Generates a list of records containing all users who are members of
   // the specified group.
   //
   // The results are returned in three arrays. The caller must free all
   // three arrays.
   //
   // The number of results is stored in nitems.
   //
   // The emails and nicks string arrays are allocated to store the number
   // of strings in the results set for the respective columns (emails and
   // nicks) and each array is terminated with a NULL pointer. The caller
   // must free both the array as well as each element of the array, for
   // both the arrays.
   //
   // The ids array stores each ID and the entire array (but not each
   // element) must be freed by the caller.
   bool sqldb_auth_group_members (sqldb_t    *db,
                                  const char *name,
                                  uint64_t   *nitems,
                                  char     ***emails,
                                  char     ***nicks,
                                  uint64_t  **flags,
                                  uint64_t  **ids);

   ///////////////////////////////////////////////////////////////////////

   // Grant the specified permissions to the specified user for the
   // specified resource. Returns true on success and false on failure.
   bool sqldb_auth_perms_grant_user (sqldb_t    *db,
                                     const char *email,
                                     const char *resource,
                                     uint64_t    perms);

   // Revoke the specified permissions to the specified user for the
   // specified resource. Returns true on success and false on failure.
   bool sqldb_auth_perms_revoke_user (sqldb_t   *db,
                                      const char *email,
                                      const char *resource,
                                      uint64_t    perms);

   bool sqldb_auth_perms_grant_group (sqldb_t    *db,
                                      const char *name,
                                      const char *resource,
                                      uint64_t    perms);

   // Revoke the specified permissions to the specified user for the
   // specified resource. Returns true on success and false on failure.
   bool sqldb_auth_perms_revoke_group (sqldb_t   *db,
                                       const char *name,
                                       const char *resource,
                                       uint64_t    perms);

   // Retrieve the user's permissions for the specified resource and
   // stores in in 'perms'. Returns true on success and false on failure.
   bool sqldb_auth_perms_get_user (sqldb_t      *db,
                                   uint64_t     *perms_dst,
                                   const char   *email,
                                   const char   *resource);

   // Retrieve the group's permissions for the specified resource and
   // stores in in 'perms'. Returns true on success and false on failure.
   bool sqldb_auth_perms_get_group (sqldb_t     *db,
                                    uint64_t    *perms_dst,
                                    const char  *name,
                                    const char  *resource);

   // Retrieve the effective permissions of the specified user for the
   // specified resource and stores it in 'perms'. Returns true on success
   // and false on failure.
   //
   // The effective permissions is the bitwise 'OR' of all the permission
   // bits of the user as well as all of the groups that the user belongs
   // to.
   bool sqldb_auth_perms_get_all (sqldb_t    *db,
                                  uint64_t   *perms_dst,
                                  const char *email,
                                  const char *resource);
#ifdef __cplusplus
};
#endif


#endif
