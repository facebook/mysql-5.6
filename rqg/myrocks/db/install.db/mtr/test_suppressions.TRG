TYPE=TRIGGERS
triggers='CREATE DEFINER=`root`@`localhost` TRIGGER ts_insert BEFORE INSERT ON test_suppressions FOR EACH ROW BEGIN   DECLARE dummy INT;   SELECT "" REGEXP NEW.pattern INTO dummy; END'
sql_modes=0
definers='root@localhost'
client_cs_names='latin1'
connection_cl_names='latin1_swedish_ci'
db_cl_names='latin1_swedish_ci'
