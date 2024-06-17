drop table if exists lineitem;
create table lineitem (
        l_orderkey int,
        l_partkey int,
        l_suppkey int,
        l_linenumber bigint,
        l_quantity decimal(12,2),
        l_extendedprice decimal(12,2),
        l_discount decimal(12,2),
        l_tax decimal(12,2),
        l_returnflag char (1),
        l_linestatus char (1),
        l_shipdate date,
        l_commitdate date,
        l_receiptdate date,
        l_shipinstruct char (25),
        l_shipmode char (10),
        l_comment varchar (44),
        id int not null auto_increment,
        primary key (id)
) engine=Rocksdb
;