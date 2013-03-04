# Test for bug 528752: xtrabackup does not handle innodb=force in my.cnf

. inc/common.sh

# XB fails with exit code 3 when the bug is present
xtrabackup --print-param --innodb=force
