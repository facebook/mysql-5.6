if (`SELECT count(*) FROM information_schema.engines WHERE
      (support = 'YES' OR support = 'DEFAULT') AND
      engine = 'PERFORMANCE_SCHEMA'`){
  skip Need performance schema off;
}
