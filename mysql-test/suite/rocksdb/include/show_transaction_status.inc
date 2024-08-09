# The output from SHOW ENGINE ROCKSDB TRANSACTION STATUS has some
# non-deterministic results, replace them with deterministic placeholders.

--replace_regex /[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}/TIMESTAMP/ /(ACTIVE) [0-9]+ /\1 NUM / /(thread id) [0-9]+/\1 TID/ /(thread handle) [0-9]+/\1 PTR/ /(query id) [0-9]+/\1 QID/ /(root) [a-z ]+/\1 ACTION/ /WAITING KEY: [0-9a-f]{16}/KEY/ /TRANSACTION ID: [0-9]*/TXN_ID/ /INDEX_ID: [0-9a-f]*/IDX_ID/ /TIMESTAMP: [0-9]*/TSTAMP/
SHOW ENGINE ROCKSDB TRANSACTION STATUS;
