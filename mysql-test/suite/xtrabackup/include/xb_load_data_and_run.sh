set -e

# Initially loads a chunk of data.
# Then start loading another chunk of data,
# while simultaneously running a backup

suite/xtrabackup/include/xb_load_data.sh $1 2>&1
suite/xtrabackup/include/xb_load_data.sh $1 2>&1 &
suite/xtrabackup/include/xb_run.sh 2>&1
