sst_dump=$2
TOTAL_D=0
TOTAL_E=0
for f in `ls $1/mysqld.1/data/.rocksdb/*.sst`
do
DELETED=`$sst_dump --command=scan --output_hex --file=$f | grep " : 0" | wc -l`
EXISTS=`$sst_dump --command=scan --output_hex --file=$f | grep " : 1" | wc -l`
TOTAL_D=$(($TOTAL_D+$DELETED))
TOTAL_E=$(($TOTAL_E+$EXISTS))
# echo "${f##*/} $DELETED $EXISTS"
done

if [ "$TOTAL_E" = "0" ]
then
  echo "No records in the database"
  exit
fi

if [ "$TOTAL_D" = "0" ]
then
  echo "No more deletes left"
else
  echo "There are deletes left"
fi
