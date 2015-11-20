# Check if the tablespace file was removed
let IBD=$MYSQLD_DATADIR/test/t.ibd;
perl;
print "tablespace file removed\n" if (! -e "$ENV{IBD}");
EOF

--error ER_TABLESPACE_DISCARDED
SELECT COUNT(*) FROM t;

--error ER_TABLESPACE_DISCARDED
INSERT INTO t(a) VALUES (1), (2), (3);

perl;
do 'include/innodb-util.inc';
ib_restore_tablespaces("test", "t");
EOF

ALTER TABLE t IMPORT TABLESPACE;
CHECK TABLE t;

SELECT COUNT(*) FROM t;

INSERT INTO t(a) VALUES(1), (2), (3);

SELECT * FROM t;

DROP TABLE t;
