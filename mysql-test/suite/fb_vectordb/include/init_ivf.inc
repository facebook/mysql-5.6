disable_query_log;
disable_warnings;
DROP TABLE IF EXISTS VECTORDB_DATA;
DROP USER IF EXISTS 'admin:sys.database';
enable_warnings;
# create table to store trained faiss index for ivf
create table VECTORDB_DATA (
  id varchar(128) not null,
  type varchar(128) not null,
  seqno int not null,
  value JSON not null,
  primary key (id, type, seqno)
);

CREATE USER 'admin:sys.database';
GRANT ALL ON *.* TO 'admin:sys.database'@'%';

FLUSH PRIVILEGES;

enable_query_log;
