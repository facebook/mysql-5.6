#! /bin/bash
# Usage: source prepare_testdir.inc directory_name file_name
# This will check that directory_name exists under /tmp/dbeng and
# remove file_name under directory_name

if [ ! -d "/tmp/dbeng" ]
then
    mkdir /tmp/dbeng
fi

if [ ! -d "/tmp/dbeng/$1" ]
then
    mkdir /tmp/dbeng/$1
fi

if [ $2 ]
then
    if [ -f "/tmp/dbeng/$1/$2" ]
    then
        rm /tmp/dbeng/$1/$2
    fi
elif [ "$(ls -A /tmp/dbeng/$1/)" ]
then
    rm /tmp/dbeng/$1/*
fi
