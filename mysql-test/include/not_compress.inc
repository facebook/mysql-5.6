-- require r/not_compress.require
disable_query_log;
eval SHOW SESSION STATUS LIKE 'Compression';
enable_query_log;
