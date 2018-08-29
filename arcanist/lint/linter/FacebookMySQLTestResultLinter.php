<?php
// Copyright 2016, Facebook, Inc.  All rights reserved.
// @author: Jay Edgar (jkedgar@fb.com)

class FacebookMySQLTestResultLinter extends ArcanistLinter {

  const MISSING_RESULT = 1;
  const MISSING_TEST = 2;

  const TEST_FILE_REGEX = '/(.*mysql-test.*\/)t(\/.*)\.test$/';
  const RESULT_FILE_REGEX = '/(.*mysql-test.*\/)r(\/.*)\.result/';

  public function getLinterName() {
    return 'MySQLTestResult';
  }

  public function willLintPaths(array $paths) {
    return;
  }

  public function getLintSeverityMap() {
    return array(
      self::MISSING_RESULT => ArcanistLintSeverity::SEVERITY_ERROR,
      self::MISSING_TEST =>   ArcanistLintSeverity::SEVERITY_ERROR,
    );
  }

  public function getLintNameMap() {
    return array(
      self::MISSING_RESULT => 'Missing result file',
      self::MISSING_TEST =>   'Missing test file',
    );
  }

  public function lintPath($path) {
    $full_path = $this->getEngine()->getFilePathOnDisk($path);
    if (preg_match_all(self::TEST_FILE_REGEX, $full_path, $matches)) {
      $result_path = $matches[1][0] . 'r' . $matches[2][0] . '.result';
      if (!file_exists($result_path)) {
        $this->raiseLintAtPath(self::MISSING_RESULT,
          "The result file ($result_path) for $path is missing");
      }
    }
    elseif (preg_match_all(self::RESULT_FILE_REGEX, $full_path, $matches)) {
      $test_path = $matches[1][0] . 't' . $matches[2][0] . '.test';
      if (!file_exists($test_path)) {
        $this->raiseLintAtPath(self::MISSING_TEST,
          "The test file ($test_path) for $path is missing");
      }
    }
  }

}
