<?php
// Copyright 2018-present Facebook.  All rights reserved.

final class FacebookMySQLPort80Linter extends ArcanistLinter {

  const LINT_DEFINE_REMOVED = 1;

  private $maxLineLength = 80;
  private $fileData;

  public function setMaxLineLength($new_length) {
    $this->maxLineLength = $new_length;
    return $this;
  }

  public function willLintPaths(array $paths) {
    return;
  }

  public function getLinterName() {
    return 'MYSQLPort80';
  }

  public function getLintSeverityMap() {
    return array(
      self::LINT_DEFINE_REMOVED
        => ArcanistLintSeverity::SEVERITY_WARNING,
      );
  }

  public function getLintNameMap() {
    return array(
      self::LINT_DEFINE_REMOVED
        => 'Removed #define from 8.0',
    );
  }

  public function lintPath($path) {
    if ($this->didStopAllLinters()) {
      return;
    }

    if (!$this->shouldLintBinaryFiles() &&
        $this->GetEngine()->isBinaryFile($path)) {
      return;
    }

    $this->fileData = $this->getData($path);

    if (!strlen($this->fileData)) {
      /* If the file is empty, then we don't require to
      add a new line */
      return;
    }

    $this->lintDefineRemoved($path);
  }

  protected function lintDefineRemoved($path) {

    $lines = explode("\n", $this->getData($path));
    foreach ($lines as $line_idx => $line) {
      $trimmedLine = trim($line);

      if (preg_match(
            '/#.*if.*(EMBEDDED_LIBRARY|MYSQL_CLIENT|HAVE_REPLICATION)/',
            $trimmedLine) === 1) {
        $this->raiseLintAtLine(
          $line_idx + 1,
          1,
          self::LINT_DEFINE_REMOVED,
          'The #defines for EMBEDDED_LIBRARY, MYSQL_CLIENT, and ' .
          'HAVE_REPLICATION have been removed in 8.0. These #defines ' .
          'should be removed from this diff.',
          $line);
      }
    }
  }
}
