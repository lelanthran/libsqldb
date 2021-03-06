#!/bin/bash

export PS4='Line ${LINENO}:'

set -e
set -o xtrace

rm -fv sqldb_auth.sql3

# export PROG="test-scripts/sqldb_auth_cli.exe"
export PROG="test-scripts/sqldb_auth_cli.elf"
export VALGRIND="valgrind --error-exitcode=127"
if [ -z "$VGOPTS" ]; then
   export VALGRIND=""
fi

# Ensure that the help message works
$VALGRIND $VGOPTS $PROG --help

# Create a new database for use
$VALGRIND $VGOPTS $PROG create sqldb_auth.sql3 sqlite
$VALGRIND $VGOPTS $PROG init sqlite sqldb_auth.sql3
# gdb $PROG init sqlite sqldb_auth.sql3

# Add a few users
$VALGRIND $VGOPTS $PROG user_create mone@example.com   mONE-USER    123456
$VALGRIND $VGOPTS $PROG user_create mtwo@example.com   mTWO-USER    123456
$VALGRIND $VGOPTS $PROG user_create mthree@example.com mTHREE-USER  123456
$VALGRIND $VGOPTS $PROG user_create mfour@example.com  mFOUR-USER   123456
$VALGRIND $VGOPTS $PROG user_create mfive@example.com  mFIVE-USER   123456
$VALGRIND $VGOPTS $PROG user_create msix@example.com   mSIX-USER    123456
$VALGRIND $VGOPTS $PROG user_create mseven@example.com mSEVEN-USER  123456
$VALGRIND $VGOPTS $PROG user_create meight@example.com mEIGHT-USER  123456
$VALGRIND $VGOPTS $PROG user_create mnine@example.com  mNINE-USER   123456
$VALGRIND $VGOPTS $PROG user_create mten@example.com   mTEN-USER    123456

# Set a few user flags
$VALGRIND $VGOPTS $PROG user_flags_set mone@example.com   0 1 2 3 4 5
$VALGRIND $VGOPTS $PROG user_flags_set mtwo@example.com   0 1 2 3 4 5
$VALGRIND $VGOPTS $PROG user_flags_set mthree@example.com 0 1 2 3 4 5
$VALGRIND $VGOPTS $PROG user_flags_set mfour@example.com  0 1 2 3 4 5
$VALGRIND $VGOPTS $PROG user_flags_set mfive@example.com  0 1 2 3 4 5
$VALGRIND $VGOPTS $PROG user_flags_set msix@example.com   0 1 2 3 4 5
$VALGRIND $VGOPTS $PROG user_flags_set mseven@example.com 0 1 2 3 4 5
$VALGRIND $VGOPTS $PROG user_flags_set meight@example.com 0 1 2 3 4 5
$VALGRIND $VGOPTS $PROG user_flags_set mnine@example.com  0 1 2 3 4 5
$VALGRIND $VGOPTS $PROG user_flags_set mten@example.com   0 1 2 3 4 5

# Clear a few user flags
$VALGRIND $VGOPTS $PROG user_flags_clear mone@example.com   0 2 4 5
$VALGRIND $VGOPTS $PROG user_flags_clear mtwo@example.com   0 2 4 5
$VALGRIND $VGOPTS $PROG user_flags_clear mthree@example.com 0 2 4 5
$VALGRIND $VGOPTS $PROG user_flags_clear mfour@example.com  0 2 4 5
$VALGRIND $VGOPTS $PROG user_flags_clear mfive@example.com  0 2 4 5
$VALGRIND $VGOPTS $PROG user_flags_clear msix@example.com   0 2 4 5
$VALGRIND $VGOPTS $PROG user_flags_clear mseven@example.com 0 2 4 5
$VALGRIND $VGOPTS $PROG user_flags_clear meight@example.com 0 2 4 5
$VALGRIND $VGOPTS $PROG user_flags_clear mnine@example.com  0 2 4 5
$VALGRIND $VGOPTS $PROG user_flags_clear mten@example.com   0 2 4 5

# Remove some of the users
$VALGRIND $VGOPTS $PROG user_rm mseven@example.com
$VALGRIND $VGOPTS $PROG user_rm meight@example.com
$VALGRIND $VGOPTS $PROG user_rm mnine@example.com

# Modify some of the users (remove the 'm' prefix)
$VALGRIND $VGOPTS $PROG user_mod mone@example.com   one@example.com   ONE-USER    12345
$VALGRIND $VGOPTS $PROG user_mod mtwo@example.com   two@example.com   TWO-USER    12345
$VALGRIND $VGOPTS $PROG user_mod mthree@example.com three@example.com THREE-USER  12345
$VALGRIND $VGOPTS $PROG user_mod mfour@example.com  four@example.com  FOUR-USER   12345
$VALGRIND $VGOPTS $PROG user_mod mfive@example.com  five@example.com  FIVE-USER   12345
$VALGRIND $VGOPTS $PROG user_mod msix@example.com   six@example.com   SIX-USER    12345
$VALGRIND $VGOPTS $PROG user_mod mten@example.com   ten@example.com   TEN-USER    12345

# Display information on a few users
$VALGRIND $VGOPTS $PROG user_info four@example.com
$VALGRIND $VGOPTS $PROG user_info five@example.com
$VALGRIND $VGOPTS $PROG user_info six@example.com

# List the users
$VALGRIND $VGOPTS $PROG user_find "f%" ""
$VALGRIND $VGOPTS $PROG user_find "" "t%"
$VALGRIND $VGOPTS $PROG user_find "t%" "f%"


# Add a few groups
$VALGRIND $VGOPTS $PROG group_create mGroup-One    "mGroup One Description"
$VALGRIND $VGOPTS $PROG group_create mGroup-Two    "mGroup Two Description"
$VALGRIND $VGOPTS $PROG group_create mGroup-Three  "mGroup Three Description"
$VALGRIND $VGOPTS $PROG group_create mGroup-Four   "mGroup Four Description"
$VALGRIND $VGOPTS $PROG group_create mGroup-Five   "mGroup Five Description"
$VALGRIND $VGOPTS $PROG group_create mGroup-Six    "mGroup Six Description"
$VALGRIND $VGOPTS $PROG group_create mGroup-Seven  "mGroup Seven Description"
$VALGRIND $VGOPTS $PROG group_create mGroup-Eight  "mGroup Eight Description"
$VALGRIND $VGOPTS $PROG group_create mGroup-Nine   "mGroup Nine Description"
$VALGRIND $VGOPTS $PROG group_create mGroup-Ten    "mGroup Ten Description"

# Remove some of the groups
$VALGRIND $VGOPTS $PROG group_rm mGroup-Seven
$VALGRIND $VGOPTS $PROG group_rm mGroup-Eight
$VALGRIND $VGOPTS $PROG group_rm mGroup-Nine

# Modify some of the groups (remove the 'm' prefix)
$VALGRIND $VGOPTS $PROG group_mod mGroup-One   Group-One   "Group One Description"
$VALGRIND $VGOPTS $PROG group_mod mGroup-Two   Group-Two   "Group Two Description"
$VALGRIND $VGOPTS $PROG group_mod mGroup-Three Group-Three "Group Three Description"
$VALGRIND $VGOPTS $PROG group_mod mGroup-Four  Group-Four  "Group Four Description"
$VALGRIND $VGOPTS $PROG group_mod mGroup-Five  Group-Five  "Group Five Description"
$VALGRIND $VGOPTS $PROG group_mod mGroup-Six   Group-Six   "Group Six Description"
$VALGRIND $VGOPTS $PROG group_mod mGroup-Ten   Group-Ten   "Group Ten Description"


# Display information on a few groups
$VALGRIND $VGOPTS $PROG group_info Group-One
$VALGRIND $VGOPTS $PROG group_info Group-Two
$VALGRIND $VGOPTS $PROG group_info Group-Four

# List the groups
$VALGRIND $VGOPTS $PROG group_find "group-t%" ""
$VALGRIND $VGOPTS $PROG group_find "" "group f%"
$VALGRIND $VGOPTS $PROG group_find "group-t%" "group f%"


# Add users to a group
$VALGRIND $VGOPTS $PROG group_adduser Group-One one@example.com
$VALGRIND $VGOPTS $PROG group_adduser Group-One two@example.com
$VALGRIND $VGOPTS $PROG group_adduser Group-One three@example.com
$VALGRIND $VGOPTS $PROG group_adduser Group-One four@example.com
$VALGRIND $VGOPTS $PROG group_adduser Group-One five@example.com
$VALGRIND $VGOPTS $PROG group_adduser Group-One six@example.com
$VALGRIND $VGOPTS $PROG group_adduser Group-One ten@example.com

# Remove users from a few groups
$VALGRIND $VGOPTS $PROG group_rmuser Group-One three@example.com
$VALGRIND $VGOPTS $PROG group_rmuser Group-One five@example.com

# List group membership
$VALGRIND $VGOPTS $PROG group_members Group-One

# Grant permissions to users
$VALGRIND $VGOPTS $PROG grant_user one@example.com Resource-1 0
$VALGRIND $VGOPTS $PROG grant_user one@example.com Resource-1 1
$VALGRIND $VGOPTS $PROG grant_user one@example.com Resource-1 2
$VALGRIND $VGOPTS $PROG grant_user one@example.com Resource-1 3
$VALGRIND $VGOPTS $PROG grant_user one@example.com Resource-1 4

# Remove permissions from users
$VALGRIND $VGOPTS $PROG revoke_user one@example.com Resource-1 1
$VALGRIND $VGOPTS $PROG revoke_user one@example.com Resource-1 3

# Resulting permission value should be 21

# Grant permissions to groups
$VALGRIND $VGOPTS $PROG grant_group Group-One Resource-1 0
$VALGRIND $VGOPTS $PROG grant_group Group-One Resource-1 1
$VALGRIND $VGOPTS $PROG grant_group Group-One Resource-1 2
$VALGRIND $VGOPTS $PROG grant_group Group-One Resource-1 3
$VALGRIND $VGOPTS $PROG grant_group Group-One Resource-1 4

# Remove permissions from groups
$VALGRIND $VGOPTS $PROG revoke_group Group-One Resource-1 2
$VALGRIND $VGOPTS $PROG revoke_group Group-One Resource-1 4

# Resulting permission value should be 11

# List user non-effective perms
$VALGRIND $VGOPTS $PROG user_perms one@example.com Resource-1
# echo 'set args one@example.com Resource-1'
# gdb $PROG user_perms one@example.com Resource-1

# List group perms
$VALGRIND $VGOPTS $PROG group_perms Group-One Resource-1

# List effective user perms
$VALGRIND $VGOPTS $PROG perms one@example.com Resource-1

set +e

# Check for a valid password, then check for an invalid password
$VALGRIND $VGOPTS $PROG password_valid one@example.com 12345 > tmptest
if [ "`cat tmptest`" != 'true' ]; then
   echo "Password validation failed for one@example.com/12345"
   cat tmptest
   exit 127
fi
$VALGRIND $VGOPTS $PROG password_valid one@example.com 12344 > tmptest
if [ "`cat tmptest`" != 'false' ]; then
   echo "Password validation succeeded for one@example.com/12344"
   cat tmptest
   exit 127
fi

# Check for a valid session, then start a session,
# then check if it is valid, invalidate it, then check again
$VALGRIND $VGOPTS $PROG session_valid 123456789 > tmptest
if [ 0 -eq "$?" ]; then
   echo Incorrectly validated one@example.com:$SESS_ID
   cat tmptest
   exit 127;
fi
cat tmptest

$VALGRIND $VGOPTS $PROG session_authenticate one@example.com 12345 > tmptest
if [ 0 -ne "$?" ]; then
   echo Failed to authenticate one@example.com:12345
   cat tmptest
   exit 126;
fi
export SESS_ID=`cat tmptest`
echo Using session $SESS_ID

# Testing unique session ID
$VALGRIND $VGOPTS $PROG session_authenticate two@example.com 12345 > tmptest
if [ 0 -ne "$?" ]; then
   echo Failed to authenticate two@example.com:12345
   cat tmptest
   exit 126;
fi

$VALGRIND $VGOPTS $PROG session_valid $SESS_ID > tmptest
if [ 0 -ne "$?" ]; then
   echo Failed to validate one@example.com:$SESS_ID
   cat tmptest
   exit 125;
fi
cat tmptest

$VALGRIND $VGOPTS $PROG session_invalidate one@example.com $SESS_ID > tmptest
if [ 0 -ne "$?" ]; then
   echo Failed to INvalidate one@example.com:$SESS_ID
   cat tmptest
   exit 124;
fi
cat tmptest

$VALGRIND $VGOPTS $PROG session_valid $SESS_ID > tmptest
if [ 0 -eq "$?" ]; then
   echo Incorrectly validated one@example.com:$SESS_ID
   cat tmptest
   exit 123;
fi
echo "SUCCESS"


