<?php
// Copyright 2015, Facebook, Inc.  All rights reserved.
// @author: Santosh Banda (santoshb@fb.com)

class FacebookMySQLPrintfLinter extends ArcanistLinter {
  const LINT_PRINTF = 1;

  public function willLintPaths(array $paths) {
    return;
  }

  public function getLinterName() {
    return 'MySQLPrintf';
  }

  public function getLintSeverityMap() {
    return array(
      self::LINT_PRINTF
        => ArcanistLintSeverity::SEVERITY_ADVICE,
    );
  }

  public function getLintNameMap() {
    return array(
      self::LINT_PRINTF
        => 'Error log output',
    );
  }

  public function lintPath($path) {
    if ($this->didStopAllLinters()) {
      return;
    }
    $lines = explode("\n", $this->getData($path));
    foreach ($lines as $line_idx => $line) {
      $trimmedLine = trim($line);
      if (strpos($trimmedLine, 'fprintf(stderr') === 0 ||
          strpos($trimmedLine, 'sql_print_') === 0) {
        $this->raiseLintAtLine(
          $line_idx + 1,
          1,
          self::LINT_PRINTF,
          'Please ensure this debug output is not going to flood error log. '.
          'Ignore this warning if you think this is safe.',
          $line);
      }
    }
  }
}

