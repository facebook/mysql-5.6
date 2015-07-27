--echo 'innodb_defragment_max_runtime_pct_defrag_table(send)'

# Clear data
TRUNCATE TABLE t1;

# Populate table.
INSERT INTO t1 VALUES (1, REPEAT('A', 256));
INSERT INTO t1 VALUES (2, REPEAT('A', 256));
INSERT INTO t1 VALUES (3, REPEAT('A', 256));
INSERT INTO t1 (b) SELECT b from t1;
INSERT INTO t1 (b) SELECT b from t1;
INSERT INTO t1 (b) SELECT b from t1;
INSERT INTO t1 (b) SELECT b from t1;
INSERT INTO t1 (b) SELECT b from t1;
INSERT INTO t1 (b) SELECT b from t1;
INSERT INTO t1 (b) SELECT b from t1;
INSERT INTO t1 (b) SELECT b from t1;
INSERT INTO t1 (b) SELECT b from t1;
INSERT INTO t1 (b) SELECT b from t1;
INSERT INTO t1 (b) SELECT b from t1;

# Invoke defrag in the background
if ($send)
{
  send alter table t1 defragment;
}

# Invoke defrag inline
if (!$send)
{
  alter table t1 defragment;
}
