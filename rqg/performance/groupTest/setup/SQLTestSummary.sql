create table SQLTestSummary (
TestID			int,
TestRunID		char(14),	
TestRunDesc		varchar(255),
ExecServer		varchar(15),
StackName		varchar(15),
numDM			tinyint,
numUM			tinyint,
numPM			tinyint,
CalpontDB		varchar(15),
ScriptFileName	varchar(255),
NumIterations		tinyint,
NumSessions		tinyint,
NumSQLStmts		tinyint,
DataVolume		char(1),
IOType			char(1),
NumStmts		int,
NumStmtsProcessed	int,	
RunCompleted		char(1)
);

create table SQLTestTime (
TestRunID		char(14),
IterNum		tinyint,
SessNum		tinyint,
SQLSeqNum		tinyint,
SQLIdxNum		tinyint,
StartTime		datetime,
EndTime		datetime
);

create table SQLTestStats (
TestRunID		char(14),	
IterNum			tinyint,
SessNum			tinyint,
SQLSeqNum		tinyint,
SQLIdxNum		tinyint,
MaxMemPct		int,
NumTempFiles		int,
TempFileSpace		varchar(10),
PhyIO			int,
CacheIO			int,
BlocksTouched		int,
PartBlocksEliminated	int,
MsgBytesIn		varchar(10),
MsgBytesOut		varchar(10),
ModeDistributed		varchar(10)
);

create table SQLTestTime (
TestRunID		char(14),
IterNum		tinyint,
SessNum		tinyint,
SQLSeqNum		tinyint,
SQLIdxNum	tinyint,
StartTime		datetime,
EndTime		datetime
);






create table BulkTestSummary (
TestID			int,
TestRunID		char(14),
TestRunDesc		varchar(255),
ExecServer		varchar(15),
StackName		varchar(15),
numDM			tinyint,
numUM			tinyint,
numPM			tinyint,
CalpontDB		varchar(15),
ScriptFileName	varchar(255),
NumTables		tinyint,
NumTablesLoaded	tinyint,
RunCompleted		char(1)
);

create table BulkTestStats (
TestRunID		char(14),
TableName		varchar(25),
SourceFile		varchar(25),
LoadTime		int,
RowsProcessed		bigint,
RowsInserted		bigint
);

