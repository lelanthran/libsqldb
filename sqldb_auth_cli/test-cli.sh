#!/bin/bash

set -e

rm -fv sqldb_auth.sql3

export PROG=./sqldb_auth_cli/main-d.elf
export VALGRIND="valgrind --error-exitcode=127"
if [ -z "$VGOPTS" ]; then
   export VALGRIND=""
fi

# Ensure taht the help message works
$VALGRIND $VGOPTS $PROG --help

# Create a new database for use
$VALGRIND $VGOPTS $PROG create sqldb_auth.sql3
$VALGRIND $VGOPTS $PROG init sqlite sqldb_auth.sql3

# Add a few users
$VALGRIND $VGOPTS $PROG user_new mone@example.com   mONE-USER    123456
$VALGRIND $VGOPTS $PROG user_new mtwo@example.com   mTWO-USER    123456
$VALGRIND $VGOPTS $PROG user_new mthree@example.com mTHREE-USER  123456
$VALGRIND $VGOPTS $PROG user_new mfour@example.com  mFOUR-USER   123456
$VALGRIND $VGOPTS $PROG user_new mfive@example.com  mFIVE-USER   123456
$VALGRIND $VGOPTS $PROG user_new msix@example.com   mSIX-USER    123456
$VALGRIND $VGOPTS $PROG user_new mseven@example.com mSEVEN-USER  123456
$VALGRIND $VGOPTS $PROG user_new meight@example.com mEIGHT-USER  123456
$VALGRIND $VGOPTS $PROG user_new mnine@example.com  mNINE-USER   123456
$VALGRIND $VGOPTS $PROG user_new mten@example.com   mTEN-USER    123456

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


# List the users

# Add a few groups

# Remove some of the groups

# Modify some of the groups (remove the 'm' prefix)

# List the groups

# Add users to a group

# Remove users from a few groups

# List group membership

# Grant permissions to users

# Grant permissions to groups

# Remove permissions from users

# Remove permissions from groups

# List user non-effective perms

# List effective user perms
