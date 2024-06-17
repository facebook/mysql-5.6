#!/bin/bash
#
#  waitTimeInHours
   waitTime=$1
#
   waitTimeInSeconds=$((waitTime*3600))
   sleep $waitTimeInSeconds
#
