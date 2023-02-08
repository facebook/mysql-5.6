--disable_query_log

if ($use_partial_index) {
CREATE TABLE `link_table` (
  `id1` bigint(20) unsigned NOT NULL DEFAULT '0' ,
  `id1_type` int(10) unsigned NOT NULL DEFAULT '0' ,
  `id2` bigint(20) unsigned NOT NULL DEFAULT '0' ,
  `id2_type` int(10) unsigned NOT NULL DEFAULT '0' ,
  `link_type` bigint(20) unsigned NOT NULL DEFAULT '0' ,
  `visibility` tinyint(3) NOT NULL DEFAULT '0' ,
  `data` varchar(255) COLLATE latin1_bin NOT NULL DEFAULT '' ,
  `time` int(10) unsigned NOT NULL DEFAULT '0' ,
  `version` bigint(20) unsigned NOT NULL DEFAULT '0' ,
  PRIMARY KEY (`link_type` , `id1` , `id2`) COMMENT 'cf_link' ,
  KEY `id1_type` (`link_type` , `id1` , `visibility` , `time` , `id2` ,
    `version` , `data`) COMMENT 'cfname=rev:cf_link_id1_type;partial_group_keyparts=2;partial_group_threshold=5'
) ENGINE=ROCKSDB DEFAULT CHARSET=latin1 COLLATE=latin1_bin
ROW_FORMAT=COMPRESSED KEY_BLOCK_SIZE=8;
}
if (!$use_partial_index) {
CREATE TABLE `link_table` (
  `id1` bigint(20) unsigned NOT NULL DEFAULT '0' ,
  `id1_type` int(10) unsigned NOT NULL DEFAULT '0' ,
  `id2` bigint(20) unsigned NOT NULL DEFAULT '0' ,
  `id2_type` int(10) unsigned NOT NULL DEFAULT '0' ,
  `link_type` bigint(20) unsigned NOT NULL DEFAULT '0' ,
  `visibility` tinyint(3) NOT NULL DEFAULT '0' ,
  `data` varchar(255) COLLATE latin1_bin NOT NULL DEFAULT '' ,
  `time` int(10) unsigned NOT NULL DEFAULT '0' ,
  `version` bigint(20) unsigned NOT NULL DEFAULT '0' ,
  PRIMARY KEY (`link_type` , `id1` , `id2`) COMMENT 'cf_link' ,
  KEY `id1_type` (`id1` , `link_type` , `visibility` , `time` , `id2` ,
    `version` , `data`) COMMENT 'rev:cf_link_id1_type'
) ENGINE=ROCKSDB DEFAULT CHARSET=latin1 COLLATE=latin1_bin
ROW_FORMAT=COMPRESSED KEY_BLOCK_SIZE=8;
}

if ($use_partial_index) {
CREATE TABLE `link_table2` (
  `id1` bigint(20) unsigned NOT NULL DEFAULT '0' ,
  `id1_type` int(10) unsigned NOT NULL DEFAULT '0' ,
  `id2` bigint(20) unsigned NOT NULL DEFAULT '0' ,
  `id2_type` int(10) unsigned NOT NULL DEFAULT '0' ,
  `link_type` bigint(20) unsigned NOT NULL DEFAULT '0' ,
  `visibility` tinyint(3) NOT NULL DEFAULT '0' ,
  `data` varchar(255) COLLATE latin1_bin NOT NULL DEFAULT '' ,
  `time` int(10) unsigned NOT NULL DEFAULT '0' ,
  `version` bigint(20) unsigned NOT NULL DEFAULT '0' ,
  PRIMARY KEY (`link_type` , `id1` , `id2`)
    COMMENT 'cf_link' ,
  KEY `id1_type` (`link_type` , `id1` , `visibility` , `time` , `id2` ,
    `version` , `data`) COMMENT 'cfname=cf_link_id1_type;partial_group_keyparts=2;partial_group_threshold=5'
) ENGINE=ROCKSDB DEFAULT CHARSET=latin1 COLLATE=latin1_bin
ROW_FORMAT=COMPRESSED KEY_BLOCK_SIZE=9;
}
if (!$use_partial_index) {
CREATE TABLE `link_table2` (
  `id1` bigint(20) unsigned NOT NULL DEFAULT '0' ,
  `id1_type` int(10) unsigned NOT NULL DEFAULT '0' ,
  `id2` bigint(20) unsigned NOT NULL DEFAULT '0' ,
  `id2_type` int(10) unsigned NOT NULL DEFAULT '0' ,
  `link_type` bigint(20) unsigned NOT NULL DEFAULT '0' ,
  `visibility` tinyint(3) NOT NULL DEFAULT '0' ,
  `data` varchar(255) COLLATE latin1_bin NOT NULL DEFAULT '' ,
  `time` int(10) unsigned NOT NULL DEFAULT '0' ,
  `version` bigint(20) unsigned NOT NULL DEFAULT '0' ,
  PRIMARY KEY (`link_type` , `id1` , `id2`)
    COMMENT 'cf_link' ,
  KEY `id1_type` (`id1` , `link_type` , `visibility` , `time` , `id2` ,
    `version` , `data`) COMMENT 'cf_link_id1_type'
) ENGINE=ROCKSDB DEFAULT CHARSET=latin1 COLLATE=latin1_bin
ROW_FORMAT=COMPRESSED KEY_BLOCK_SIZE=9;
}

insert into link_table values (1, 1, 1, 2, 3, 4, 'a10', 10, 125);
insert into link_table values (1, 1, 2, 2, 3, 3, 'a10', 10, 125);
insert into link_table values (1, 1, 3, 2, 3, 4, 'a11', 11, 125);
insert into link_table values (1, 1, 4, 2, 3, 4, 'a11', 11, 125);
insert into link_table values (1, 1, 5, 2, 3, 3, 'a12', 12, 125);
insert into link_table values (1, 1, 6, 2, 3, 4, 'a12', 12, 125);
insert into link_table values (1, 1, 7, 2, 3, 4, 'a12', 12, 125);
insert into link_table values (1, 1, 8, 2, 3, 4, 'a13', 13, 125);
insert into link_table values (1, 1, 9, 2, 3, 4, 'a14', 14, 125);
insert into link_table values (1, 1, 10, 2, 3, 4, 'a15', 15, 125);
insert into link_table values (2, 1, 1, 2, 3, 4, 'a10', 10, 125);
insert into link_table values (2, 1, 2, 2, 3, 4, 'a10', 10, 125);
insert into link_table values (2, 1, 3, 2, 3, 4, 'a11', 11, 125);
insert into link_table values (2, 1, 4, 2, 3, 4, 'a11', 11, 125);
insert into link_table values (2, 1, 5, 2, 3, 4, 'a12', 12, 125);
insert into link_table values (2, 1, 6, 2, 3, 4, 'a12', 12, 125);
insert into link_table values (2, 1, 7, 2, 3, 4, 'a12', 12, 125);
insert into link_table values (2, 1, 8, 2, 3, 4, 'a13', 13, 125);
insert into link_table values (2, 1, 9, 2, 3, 4, 'a14', 14, 125);
insert into link_table values (2, 1, 10, 2, 3, 4, 'a15', 15, 125);
insert into link_table values (2, 1, 1, 2, 4, 4, 'a10', 10, 125);
insert into link_table values (2, 1, 2, 2, 4, 4, 'a10', 10, 125);
insert into link_table values (2, 1, 3, 2, 4, 4, 'a11', 11, 125);
insert into link_table values (2, 1, 4, 2, 4, 4, 'a11', 11, 125);
insert into link_table values (2, 1, 5, 2, 4, 4, 'a12', 12, 125);
insert into link_table values (2, 1, 6, 2, 4, 4, 'a12', 12, 125);
insert into link_table values (2, 1, 7, 2, 4, 4, 'a12', 12, 125);
insert into link_table values (2, 1, 8, 2, 4, 4, 'a13', 13, 125);
insert into link_table values (2, 1, 9, 2, 4, 4, 'a14', 14, 125);
insert into link_table values (2, 1, 10, 2, 4, 4, 'a15', 15, 125);
insert into link_table values (3, 1, 10, 2, 3, 4, 'a10', 10, 125);
insert into link_table values (3, 1, 9, 2, 3, 4, 'a10', 10, 125);
insert into link_table values (3, 1, 8, 2, 3, 4, 'a11', 11, 125);
insert into link_table values (3, 1, 7, 2, 3, 4, 'a11', 11, 125);
insert into link_table values (3, 1, 6, 2, 3, 4, 'a12', 12, 125);
insert into link_table values (3, 1, 5, 2, 3, 4, 'a12', 12, 125);
insert into link_table values (3, 1, 4, 2, 3, 4, 'a12', 12, 125);
insert into link_table values (3, 1, 3, 2, 3, 4, 'a13', 13, 125);
insert into link_table values (3, 1, 2, 2, 3, 4, 'a14', 14, 125);
insert into link_table values (3, 1, 1, 2, 3, 4, 'a15', 15, 125);
insert into link_table values (9, 1, 9, 2, 5, 6, '0  ', 10, 125);
insert into link_table values (9, 1, 8, 2, 5, 6, '01  ', 11, 125);
insert into link_table values (9, 1, 7, 2, 5, 6, '012  ', 11, 125);
insert into link_table values (9, 1, 6, 2, 5, 6, '0123  ', 12, 125);
insert into link_table values (9, 1, 5, 2, 5, 6, '01234  ', 12, 125);
insert into link_table values (9, 1, 4, 2, 5, 6, '012345  ', 12, 125);
insert into link_table values (9, 1, 3, 2, 5, 6, '0123456  ', 13, 125);
insert into link_table values (9, 1, 2, 2, 5, 6, '01234567  ', 14, 125);
insert into link_table values (9, 1, 1, 2, 5, 6, '012345678  ', 15, 125);
insert into link_table values (9, 1, 0, 2, 5, 6, '0123456789  ', 15, 125);

insert into link_table2 select * from link_table;

CREATE TABLE `id_table` (
  `id` bigint(20) NOT NULL DEFAULT '0',
  `type` int(11) NOT NULL DEFAULT '0',
  `row_created_time` int(11) NOT NULL DEFAULT '0',
  `hash_key` varchar(255) NOT NULL DEFAULT '',
  `is_deleted` tinyint(4) DEFAULT '0',
  PRIMARY KEY (`id`),
  KEY `type_id` (`type`,`id`),
  UNIQUE KEY `unique_type_id` (`type`,`id`)
) ENGINE=ROCKSDB DEFAULT CHARSET=latin1 ROW_FORMAT=COMPRESSED
KEY_BLOCK_SIZE=8;

insert into id_table values (1, 1, 10, '111', 0);
insert into id_table values (2, 1, 10, '111', 1);
insert into id_table values (3, 1, 10, '111', 0);
insert into id_table values (4, 1, 10, '111', 1);
insert into id_table values (5, 1, 10, '111', 0);
insert into id_table values (6, 1, 10, '111', 1);
insert into id_table values (7, 1, 10, '111', 0);
insert into id_table values (8, 1, 10, '111', 1);
insert into id_table values (9, 1, 10, '111', 0);
insert into id_table values (10, 1, 10, '111', 1);

CREATE TABLE `node_table` (
  `id` bigint(20) unsigned NOT NULL DEFAULT '0',
  `type` int(10) unsigned NOT NULL DEFAULT '0',
  `version` bigint(20) unsigned NOT NULL DEFAULT '0',
  `update_time` int(10) unsigned NOT NULL DEFAULT '0',
  `data` mediumtext COLLATE latin1_bin NOT NULL,
  PRIMARY KEY (`type`,`id`) COMMENT 'cf_node_type_id',
  KEY `id` (`id`) COMMENT 'cf_node'
) ENGINE=ROCKSDB DEFAULT CHARSET=latin1 COLLATE=latin1_bin
ROW_FORMAT=COMPRESSED KEY_BLOCK_SIZE=8;

insert into node_table values (1, 1, 1, 10, 'data');

insert into node_table values (2, 1, 1, 10, 'data');

insert into node_table values (3, 1, 1, 10, 'data');

insert into node_table values (4, 1, 1, 10, 'data');

insert into node_table values (5, 1, 1, 10, 'data');

insert into node_table values (6, 1, 1, 10, 'data');

insert into node_table values (7, 1, 1, 10, 'data');

insert into node_table values (8, 1, 1, 10, 'data');

insert into node_table values (9, 1, 1, 10, 'data');

insert into node_table values (10, 1, 1, 10, 'data');

CREATE TABLE `count_table` (
  `id` bigint(20) unsigned NOT NULL DEFAULT '0',
  `type` int(10) unsigned NOT NULL DEFAULT '0',
  `link_type` bigint(20) unsigned NOT NULL DEFAULT '0',
  `count` int(10) unsigned NOT NULL DEFAULT '0',
  `time` int(10) unsigned NOT NULL DEFAULT '0',
  `version` bigint(20) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`id`,`link_type`) COMMENT 'cf_count_table'
) ENGINE=ROCKSDB DEFAULT CHARSET=latin1 COLLATE=latin1_bin
ROW_FORMAT=COMPRESSED KEY_BLOCK_SIZE=8;


insert into count_table values (2, 1, 1, 1, 10, 20);

insert into count_table values (3, 1, 1, 1, 10, 20);

insert into count_table values (4, 1, 1, 1, 10, 20);

insert into count_table values (5, 1, 1, 1, 10, 20);

insert into count_table values (6, 1, 1, 1, 10, 20);

insert into count_table values (7, 1, 1, 1, 10, 20);

insert into count_table values (8, 1, 1, 1, 10, 20);

insert into count_table values (9, 1, 1, 1, 10, 20);

insert into count_table values (10, 1, 1, 1, 10, 20);

# using latin1_swedish_ci to verify non binary collation fileds can be returned.
CREATE TABLE `link_table5` (
  `id1` bigint(20) unsigned NOT NULL DEFAULT '0',
  `id1_type` int(10) unsigned NOT NULL DEFAULT '0',
  `id2` bigint(20) unsigned NOT NULL DEFAULT '0',
  `id2_type` int(10) unsigned NOT NULL DEFAULT '0',
  `link_type` bigint(20) unsigned NOT NULL DEFAULT '0',
  `visibility` tinyint(3) NOT NULL DEFAULT '0',
  `data` varchar(255) COLLATE latin1_swedish_ci NOT NULL DEFAULT '',
  `time` int(10) unsigned NOT NULL DEFAULT '0',
  `version` bigint(20) unsigned NOT NULL DEFAULT '0',
    PRIMARY KEY (`link_type`,`id1`,`id2`) COMMENT 'cf_link'
) ENGINE=ROCKSDB DEFAULT CHARSET=latin1 COLLATE=latin1_bin
ROW_FORMAT=COMPRESSED KEY_BLOCK_SIZE=8;

insert into link_table5 values (0, 1, 2, 2, 0, 1, 'data12', 1, 1);
insert into link_table5 values (0, 1, 3, 2, 0, 2, 'data13', 1, 1);
insert into link_table5 values (0, 1, 4, 2, 0, 2, 'data14', 1, 1);
insert into link_table5 values (0, 1, 5, 2, 0, 1, 'data15', 1, 1);
insert into link_table5 values (0, 1, 6, 2, 0, 1, 'data15', 1, 1);
insert into link_table5 values (0, 1, 7, 2, 0, 1, 'data15', 1, 1);
insert into link_table5 values (1, 1, 2, 2, 0, 1, 'data12', 1, 1);
insert into link_table5 values (1, 1, 3, 2, 0, 2, 'data13', 1, 1);
insert into link_table5 values (1, 1, 4, 2, 0, 2, 'data14', 1, 1);
insert into link_table5 values (1, 1, 5, 2, 0, 1, 'data15', 1, 1);
insert into link_table5 values (2, 1, 1, 2, 0, 1, 'data21', 1, 1);
insert into link_table5 values (2, 1, 2, 2, 0, 1, 'data22', 1, 1);
insert into link_table5 values (2, 1, 3, 2, 0, 1, 'data32', 1, 1);
insert into link_table5 values (0, 1, 2, 2, 1, 1, 'data12', 1, 1);
insert into link_table5 values (0, 1, 3, 2, 1, 2, 'data13', 1, 1);
insert into link_table5 values (0, 1, 4, 2, 1, 2, 'data14', 1, 1);
insert into link_table5 values (0, 1, 5, 2, 1, 1, 'data15', 1, 1);
insert into link_table5 values (0, 1, 6, 2, 1, 1, 'data15', 1, 1);
insert into link_table5 values (0, 1, 7, 2, 1, 1, 'data15', 1, 1);
insert into link_table5 values (1, 1, 2, 2, 1, 1, 'data12', 1, 1);
insert into link_table5 values (1, 1, 3, 2, 1, 2, 'data13', 1, 1);
insert into link_table5 values (1, 1, 4, 2, 1, 2, 'data14', 1, 1);
insert into link_table5 values (1, 1, 5, 2, 1, 1, 'data15', 1, 1);
insert into link_table5 values (2, 1, 1, 2, 1, 1, 'data21', 1, 1);
insert into link_table5 values (2, 1, 2, 2, 1, 1, 'data22', 1, 1);
insert into link_table5 values (2, 1, 3, 2, 1, 1, 'data32', 1, 1);
insert into link_table5 values (0, 1, 2, 2, 2, 1, 'data12', 1, 1);
insert into link_table5 values (0, 1, 3, 2, 2, 2, 'data13', 1, 1);
insert into link_table5 values (0, 1, 4, 2, 2, 2, 'data14', 1, 1);
insert into link_table5 values (0, 1, 5, 2, 2, 1, 'data15', 1, 1);
insert into link_table5 values (0, 1, 6, 2, 2, 1, 'data15', 1, 1);
insert into link_table5 values (0, 1, 7, 2, 2, 1, 'data15', 1, 1);
insert into link_table5 values (1, 1, 2, 2, 2, 1, 'data12', 1, 1);
insert into link_table5 values (1, 1, 3, 2, 2, 2, 'data13', 1, 1);
insert into link_table5 values (1, 1, 4, 2, 2, 2, 'data14', 1, 1);
insert into link_table5 values (1, 1, 5, 2, 2, 1, 'data15', 1, 1);
insert into link_table5 values (2, 1, 1, 2, 2, 1, 'data21', 1, 1);
insert into link_table5 values (2, 1, 2, 2, 2, 1, 'data22', 1, 1);
insert into link_table5 values (2, 1, 3, 2, 2, 1, 'data32', 1, 1);
insert into link_table5 values
  (100, 101, 102, 103, 104, 1, 'data100', 105, 106);
insert into link_table5 values
  (200, 201, 202, 203, 204, 2, 'data200', 205, 206);
insert into link_table5 values
  (300, 301, 302, 303, 304, 3, 'data300', 305, 306);
insert into link_table5 values
  (400, 401, 402, 403, 404, 4, 'data400', 405, 406);


CREATE TABLE `link_table5_rev` (
  `id1` bigint(20) unsigned NOT NULL DEFAULT '0',
  `id1_type` int(10) unsigned NOT NULL DEFAULT '0',
  `id2` bigint(20) unsigned NOT NULL DEFAULT '0',
  `id2_type` int(10) unsigned NOT NULL DEFAULT '0',
  `link_type` bigint(20) unsigned NOT NULL DEFAULT '0',
  `visibility` tinyint(3) NOT NULL DEFAULT '0',
  `data` varchar(255) COLLATE latin1_swedish_ci NOT NULL DEFAULT '',
  `time` int(10) unsigned NOT NULL DEFAULT '0',
  `version` bigint(20) unsigned NOT NULL DEFAULT '0',
    PRIMARY KEY (`link_type`,`id1`,`id2`) COMMENT 'rev:cf_link'
) ENGINE=ROCKSDB DEFAULT CHARSET=latin1 COLLATE=latin1_bin
ROW_FORMAT=COMPRESSED KEY_BLOCK_SIZE=8;

insert into link_table5_rev SELECT * FROM link_table5;

if ($use_partial_index) {
CREATE TABLE `link_table3` (
  `id1` bigint(20) unsigned NOT NULL DEFAULT '0',
  `id1_type` int(10) unsigned NOT NULL DEFAULT '0',
  `id2` bigint(20) unsigned NOT NULL DEFAULT '0',
  `id2_type` int(10) unsigned NOT NULL DEFAULT '0',
  `link_type` bigint(20) unsigned NOT NULL DEFAULT '0',
  `visibility` tinyint(4) NOT NULL DEFAULT '0',
  `data` text COLLATE latin1_bin NOT NULL,
  `time` int(10) unsigned NOT NULL DEFAULT '0',
  `version` bigint(20) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`link_type`,`id1`,`id2`) COMMENT 'cf_link',
  KEY `id1_type` (`link_type`,`id1`,`visibility`,`time`,`id2`,`version`)
    COMMENT 'cfname=rev:cf_link_id1_type;partial_group_keyparts=2;partial_group_threshold=3'
) ENGINE=ROCKSDB DEFAULT CHARSET=latin1 COLLATE=latin1_bin
ROW_FORMAT=COMPRESSED KEY_BLOCK_SIZE=4;
}
if (!$use_partial_index) {
CREATE TABLE `link_table3` (
  `id1` bigint(20) unsigned NOT NULL DEFAULT '0',
  `id1_type` int(10) unsigned NOT NULL DEFAULT '0',
  `id2` bigint(20) unsigned NOT NULL DEFAULT '0',
  `id2_type` int(10) unsigned NOT NULL DEFAULT '0',
  `link_type` bigint(20) unsigned NOT NULL DEFAULT '0',
  `visibility` tinyint(4) NOT NULL DEFAULT '0',
  `data` text COLLATE latin1_bin NOT NULL,
  `time` int(10) unsigned NOT NULL DEFAULT '0',
  `version` bigint(20) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`link_type`,`id1`,`id2`) COMMENT 'cf_link',
  KEY `id1_type` (`id1`,`link_type`,`visibility`,`time`,`id2`,`version`)
    COMMENT 'rev:cf_link_id1_type'
) ENGINE=ROCKSDB DEFAULT CHARSET=latin1 COLLATE=latin1_bin
ROW_FORMAT=COMPRESSED KEY_BLOCK_SIZE=4;
}

insert into link_table3 values (1, 1, 2, 2, 1, 1, 'data12', 1, 1);
insert into link_table3 values (1, 1, 3, 2, 1, 2, 'data13', 1, 1);
insert into link_table3 values (1, 1, 4, 2, 1, 2, 'data14', 1, 1);
insert into link_table3 values (1, 1, 5, 2, 1, 1, 'data15', 1, 1);
insert into link_table3 values (2, 1, 1, 2, 1, 1, 'data21', 1, 1);
insert into link_table3 values (2, 1, 2, 2, 1, 1, 'data22', 1, 1);
insert into link_table3 values (2, 1, 3, 2, 1, 1, 'data32', 1, 1);

if ($use_partial_index) {
CREATE TABLE `link_table6` (
  `id1` bigint(20) unsigned NOT NULL DEFAULT '0',
  `id1_type` int(10) unsigned NOT NULL DEFAULT '0',
  `id2` bigint(20) unsigned NOT NULL DEFAULT '0',
  `id2_type` int(10) unsigned NOT NULL DEFAULT '0',
  `link_type` bigint(20) unsigned NOT NULL DEFAULT '0',
  `visibility` tinyint(4) NOT NULL DEFAULT '0',
  `data` text COLLATE latin1_bin NOT NULL,
  `time` int(10) unsigned NOT NULL DEFAULT '0',
  `version` bigint(20) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`link_type`,`id1`,`id2`) COMMENT 'cf_link',
  KEY `id1_type` (`link_type`,`id1`,`visibility`,`time`,`id2`,`version`,
                  `data`(255)) COMMENT 'cfname=rev:cf_link_id1_type;partial_group_keyparts=2;partial_group_threshold=3'
) ENGINE=ROCKSDB DEFAULT CHARSET=latin1 COLLATE=latin1_bin
ROW_FORMAT=COMPRESSED KEY_BLOCK_SIZE=4;
}
if (!$use_partial_index) {
CREATE TABLE `link_table6` (
  `id1` bigint(20) unsigned NOT NULL DEFAULT '0',
  `id1_type` int(10) unsigned NOT NULL DEFAULT '0',
  `id2` bigint(20) unsigned NOT NULL DEFAULT '0',
  `id2_type` int(10) unsigned NOT NULL DEFAULT '0',
  `link_type` bigint(20) unsigned NOT NULL DEFAULT '0',
  `visibility` tinyint(4) NOT NULL DEFAULT '0',
  `data` text COLLATE latin1_bin NOT NULL,
  `time` int(10) unsigned NOT NULL DEFAULT '0',
  `version` bigint(20) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`link_type`,`id1`,`id2`) COMMENT 'cf_link',
  KEY `id1_type` (`id1`,`link_type`,`visibility`,`time`,`id2`,`version`,
                  `data`(255)) COMMENT 'rev:cf_link_id1_type'
) ENGINE=ROCKSDB DEFAULT CHARSET=latin1 COLLATE=latin1_bin
ROW_FORMAT=COMPRESSED KEY_BLOCK_SIZE=4;
}

insert into link_table6 values (1, 1, 2, 2, 1, 1,
  'data12_12345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890', 1, 1);
insert into link_table6 values (1, 1, 3, 2, 1, 2,
  'data13_12345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890', 1, 1);
insert into link_table6 values (1, 1, 4, 2, 1, 2,
  'data14_12345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890', 1, 1);
insert into link_table6 values (1, 1, 5, 2, 1, 1,
  'data15_12345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890', 1, 1);
insert into link_table6 values (2, 1, 1, 2, 1, 1,
  'data21_12345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890', 1, 1);
insert into link_table6 values (2, 1, 2, 2, 1, 1,
  'data22_12345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890', 1, 1);
insert into link_table6 values (2, 1, 3, 2, 1, 1,
  'data32_12345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890', 1, 1);

if ($use_partial_index) {
CREATE TABLE `link_table4` (
  `id1` binary(16) NOT NULL DEFAULT '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0',
  `raw_key` text COLLATE latin1_bin,
  `id2` bigint(20) unsigned NOT NULL DEFAULT '0',
  `id2_type` int(10) unsigned NOT NULL DEFAULT '0',
  `link_type` bigint(20) unsigned NOT NULL DEFAULT '0',
  `visibility` tinyint(3) NOT NULL DEFAULT '0',
  `data` varchar(255) COLLATE latin1_bin NOT NULL DEFAULT '',
  `time` int(10) unsigned NOT NULL DEFAULT '0',
  `version` bigint(20) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`link_type`,`id1`,`id2`) COMMENT 'cf_link',
  KEY `id1_type` (`link_type`,`id1`,`visibility`,`time`,`id2`,`version`,`data`)
      COMMENT 'cfname=rev:cf_link_id1_type;partial_group_keyparts=2;partial_group_threshold=3'
) ENGINE=ROCKSDB DEFAULT CHARSET=latin1 COLLATE=latin1_bin
ROW_FORMAT=COMPRESSED KEY_BLOCK_SIZE=8;
}
if (!$use_partial_index) {
CREATE TABLE `link_table4` (
  `id1` binary(16) NOT NULL DEFAULT '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0',
  `raw_key` text COLLATE latin1_bin,
  `id2` bigint(20) unsigned NOT NULL DEFAULT '0',
  `id2_type` int(10) unsigned NOT NULL DEFAULT '0',
  `link_type` bigint(20) unsigned NOT NULL DEFAULT '0',
  `visibility` tinyint(3) NOT NULL DEFAULT '0',
  `data` varchar(255) COLLATE latin1_bin NOT NULL DEFAULT '',
  `time` int(10) unsigned NOT NULL DEFAULT '0',
  `version` bigint(20) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`link_type`,`id1`,`id2`) COMMENT 'cf_link',
  KEY `id1_type` (`id1`,`link_type`,`visibility`,`time`,`id2`,`version`,`data`)
      COMMENT 'rev:cf_link_id1_type'
) ENGINE=ROCKSDB DEFAULT CHARSET=latin1 COLLATE=latin1_bin
ROW_FORMAT=COMPRESSED KEY_BLOCK_SIZE=8;
}

insert into link_table4 values ('a1', "rk1", 2, 2, 1, 1, 'data12', 1, 1);
insert into link_table4 values ('a1', "rk2", 3, 2, 1, 2, 'data13', 1, 1);
insert into link_table4 values ('a1', "rk3", 4, 2, 1, 2, 'data14', 1, 1);
insert into link_table4 values ('a1', "rk4", 5, 2, 1, 1, 'data15', 1, 1);
insert into link_table4 values ('b1', "rk5", 1, 2, 1, 1, 'data21', 1, 1);
insert into link_table4 values ('b1', "rk6", 2, 2, 1, 1, 'data22', 1, 1);
insert into link_table4 values ('b1', "rk7", 3, 2, 1, 1, 'data32', 1, 1);

if ($use_partial_index) {
CREATE TABLE `link_text` (
  `id1` bigint(20) unsigned NOT NULL DEFAULT '0',
  `id1_type` int(10) unsigned NOT NULL DEFAULT '0',
  `id2` bigint(20) unsigned NOT NULL DEFAULT '0',
  `id2_type` int(10) unsigned NOT NULL DEFAULT '0',
  `link_type` bigint(20) unsigned NOT NULL DEFAULT '0',
  `visibility` tinyint(4) NOT NULL DEFAULT '0',
  `data` text COLLATE latin1_bin NOT NULL,
  `time` int(10) unsigned NOT NULL DEFAULT '0',
  `version` bigint(20) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`link_type`,`id1`,`id2`, `data`(255)) COMMENT 'cf_link',
  KEY `id1_type` (`link_type`,`id1`,`visibility`,`time`,`id2`,`version`,
    `data`(255))COMMENT 'cfname=rev:cf_link_id1_type;partial_group_keyparts=2;partial_group_threshold=3'
) ENGINE=RocksDB DEFAULT CHARSET=latin1 COLLATE=latin1_bin
ROW_FORMAT=COMPRESSED KEY_BLOCK_SIZE=4;
}
if (!$use_partial_index) {
CREATE TABLE `link_text` (
  `id1` bigint(20) unsigned NOT NULL DEFAULT '0',
  `id1_type` int(10) unsigned NOT NULL DEFAULT '0',
  `id2` bigint(20) unsigned NOT NULL DEFAULT '0',
  `id2_type` int(10) unsigned NOT NULL DEFAULT '0',
  `link_type` bigint(20) unsigned NOT NULL DEFAULT '0',
  `visibility` tinyint(4) NOT NULL DEFAULT '0',
  `data` text COLLATE latin1_bin NOT NULL,
  `time` int(10) unsigned NOT NULL DEFAULT '0',
  `version` bigint(20) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`link_type`,`id1`,`id2`, `data`(255)) COMMENT 'cf_link',
  KEY `id1_type` (`id1`,`link_type`,`visibility`,`time`,`id2`,`version`,
    `data`(255))COMMENT 'rev:cf_link_id1_type'
) ENGINE=RocksDB DEFAULT CHARSET=latin1 COLLATE=latin1_bin
ROW_FORMAT=COMPRESSED KEY_BLOCK_SIZE=4;
}

insert into link_text values (1, 1, 1, 2, 3, 4, 'a', 10, 125);
insert into link_text values (1, 1, 2, 2, 3, 4, 'aaaaaa', 10, 125);
insert into link_text values (1, 1, 3, 2, 3, 4, repeat('a', 255), 10, 125);
insert into link_text values (1, 1, 4, 2, 3, 4, repeat('a', 500), 10, 125);

if ($use_partial_index) {
CREATE TABLE `link_varchar` (
  `id1` bigint(20) unsigned NOT NULL DEFAULT '0',
  `id1_type` int(10) unsigned NOT NULL DEFAULT '0',
  `id2` bigint(20) unsigned NOT NULL DEFAULT '0',
  `id2_type` int(10) unsigned NOT NULL DEFAULT '0',
  `link_type` bigint(20) unsigned NOT NULL DEFAULT '0',
  `visibility` tinyint(4) NOT NULL DEFAULT '0',
  `data` varchar(1000) COLLATE latin1_bin NOT NULL,
  `time` int(10) unsigned NOT NULL DEFAULT '0',
  `version` bigint(20) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`link_type`,`id1`,`id2`, `data`(255)) COMMENT 'cf_link',
  KEY `id1_type` (`link_type`,`id1`,`visibility`,`time`,`id2`,`version`,
    `data`(255)) COMMENT 'cfname=rev:cf_link_id1_type;partial_group_keyparts=2;partial_group_threshold=3'
) ENGINE=RocksDB DEFAULT CHARSET=latin1 COLLATE=latin1_bin
ROW_FORMAT=COMPRESSED KEY_BLOCK_SIZE=4;
}
if (!$use_partial_index) {
CREATE TABLE `link_varchar` (
  `id1` bigint(20) unsigned NOT NULL DEFAULT '0',
  `id1_type` int(10) unsigned NOT NULL DEFAULT '0',
  `id2` bigint(20) unsigned NOT NULL DEFAULT '0',
  `id2_type` int(10) unsigned NOT NULL DEFAULT '0',
  `link_type` bigint(20) unsigned NOT NULL DEFAULT '0',
  `visibility` tinyint(4) NOT NULL DEFAULT '0',
  `data` varchar(1000) COLLATE latin1_bin NOT NULL,
  `time` int(10) unsigned NOT NULL DEFAULT '0',
  `version` bigint(20) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`link_type`,`id1`,`id2`, `data`(255)) COMMENT 'cf_link',
  KEY `id1_type` (`id1`,`link_type`,`visibility`,`time`,`id2`,`version`,
    `data`(255)) COMMENT 'rev:cf_link_id1_type'
) ENGINE=RocksDB DEFAULT CHARSET=latin1 COLLATE=latin1_bin
ROW_FORMAT=COMPRESSED KEY_BLOCK_SIZE=4;
}

insert into link_varchar select * from link_text;

--enable_query_log
