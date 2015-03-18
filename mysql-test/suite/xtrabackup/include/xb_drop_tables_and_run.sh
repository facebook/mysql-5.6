set -e

# Initially creates a lot of tables.
# Then start modifying and dropping those tables
# while simultaneously running a backup

suite/xtrabackup/include/xb_create_tables.sh $1 2>&1
suite/xtrabackup/include/xb_modify_and_drop_tables.sh $1 2>&1 &
suite/xtrabackup/include/xb_run.sh 2>&1
