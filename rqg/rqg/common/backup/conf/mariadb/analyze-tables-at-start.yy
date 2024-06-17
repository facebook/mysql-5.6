thread1_init:
	ANALYZE TABLE { join ',', @{$executors->[0]->baseTables()} };

