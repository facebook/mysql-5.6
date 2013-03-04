use Cwd;

$combinations = [
	['--validator=None --queries=100M --duration=7200 --mysqld=--log-output=file --seed=time --mysqld=--max_heap_table_size=3Gb --reporter=MemoryUsage'],
	['--threads=1','--threads=2','--threads=4','--threads=8','--threads=16','--threads=32'],
	['--no-mask','--mask-level=1','--mask-level=2','--mask-level=3'],
	['--basedir=/home/philips/bzr/mysql-55-eb','--basedir=/home/philips/bzr/mysql-55-eb-release'],
	[
		'--grammar=conf/engines/heap/heap_ddl_multi.yy',
		'--mysqld=--init_file='.cwd().'/conf/engines/heap/heap_dml_single.init --grammar=conf/engines/heap/heap_dml_single.yy'
	]
];
