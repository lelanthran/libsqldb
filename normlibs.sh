#!/bin/bash

LIBDIR="lib/`gcc -dumpmachine`"
VERSION=`cat VERSION`

echo "LIBDIR=$LIBDIR"
pushd $PWD
cd $LIBDIR
cp -v libsqldb-${VERSION}.a   libsqldb.a
cp -v libsqldb-${VERSION}d.a  libsqldbd.a
cp -v libsqldb-${VERSION}d.so libsqldbd.so
cp -v libsqldb-${VERSION}.so  libsqldb.so
cd ..
cp -v `find .` .
popd

