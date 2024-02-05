disable_query_log;
disable_warnings;
DROP TABLE IF EXISTS VECTORDB_DATA;
enable_warnings;
# create table to store trained faiss index for ivf
create table VECTORDB_DATA (
  id varchar(64) not null,
  type varchar(16) not null,
  seqno int not null,
  value JSON not null,
  primary key (id, type, seqno)
);

enable_query_log;
