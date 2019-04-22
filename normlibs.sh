#!/bin/bash

LIBDIR="lib/`gcc -dumpmachine`"
VERSION=`cat VERSION`

echo "LIBDIR=$LIBDIR"
pushd $PWD
cd $LIBDIR
ln -s libsqldb-${VERSION}.a   libsqldb.a
ln -s libsqldb-${VERSION}d.a  libsqldbd.a
ln -s libsqldb-${VERSION}d.so libsqldbd.so
ln -s libsqldb-${VERSION}.so  libsqldb.so
cd ..
cp -v `find .` .
popd

