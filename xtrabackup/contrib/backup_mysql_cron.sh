#!/bin/bash
#
# Backup script for MySQL 5.5 on RHEL5 using XtraBackup
#
# Requires: xtrabackup, bash, awk, coreutils
#
# Copyright (C) 2011 Daniel van Eeden
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

## Settings ##
PATH="/bin:/usr/bin"
BACKUPDIR="/var/mysql/backups/xtrabackup"
MINFREE_M="5000"
INNOBACKUPEX_OPTIONS=""
RETENTION_DAYS="7"

## Logic ##
timestamp=`date +%Y%m%d_%H%M%S_%Z`
export PATH;

if [ ! -x "/usr/bin/innobackupex-1.5.1" ]; then
  echo "ERROR: /usr/bin/innobackupex-1.5.1 is not executable."
  exit 1
fi 

if [ ! -d ${BACKUPDIR} ]; then
  echo "ERROR: ${BACKUPDIR} is not a directory."
  exit 2
fi

freespace_m=`df -k ${BACKUPDIR} | awk '{ if ($4 ~ /^[0-9]*$/) { print int($4/1024) } }'`
if [ ${freespace_m} -le ${MINFREE_M} ]; then
  echo "ERROR: There is less than ${MINFREE_M} MB of free space on ${BACKUPDIR}"
  exit 3
fi

# Remove backups older than $RETENTION_DAYS
find /var/mysql/backups/xtrabackup -name "*_*_*_xtrabackup\.tar\.bz2" -type f -mtime +${RETENTION_DAYS}

# Create the backup
/usr/bin/innobackupex-1.5.1 ${INNOBACKUPEX_OPTIONS} --stream=tar /tmp 2> ${BACKUPDIR}/${timestamp}_xtrabackup.log | bzip2 > ${BACKUPDIR}/${timestamp}_xtrabackup.tar.bz2
