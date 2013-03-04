#!/bin/bash

XB_BUILD="autodetect"
while getopts "fc:" options; do
	case $options in
	        c ) XB_BUILD="$OPTARG";;
		f ) ;; # ignored
	esac
done

rm -rf results/ var/ test_results.subunit 

CFLAGS=-g make testrun
./testrun -c $XB_BUILD
