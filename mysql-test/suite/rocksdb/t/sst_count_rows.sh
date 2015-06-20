# deduce path to sst_dump from mysqld path
sst_dump=$2
for f in `ls $1/mysqld.1/data/.rocksdb/*.sst`
do
DELETED=`$sst_dump --command=scan --output_hex --file=$f | grep " : 0" | wc -l`
EXISTS=`$sst_dump --command=scan --output_hex --file=$f | grep " : 1" | wc -l`
echo "${f##*/} $DELETED $EXISTS"
done
