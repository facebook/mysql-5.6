<?php

class FacebookMySQLWhitespaceLinter extends ArcanistLinter {

  const LINT_WHITESPACE_ONLY_CHANGE = 1;

  public function willLintPaths(array $paths) {
    return;
  }

  public function getLinterName() {
    return 'MYSQLWhitespace';
  }

  public function getLintSeverityMap() {
    return array(
      self::LINT_WHITESPACE_ONLY_CHANGE
        => ArcanistLintSeverity::SEVERITY_AUTOFIX,
    );
  }

  public function getLintNameMap() {
    return array(
      self::LINT_WHITESPACE_ONLY_CHANGE
        => 'Only whitespace change',
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
      /* If the file is empty, then we don't need to do any work */
      return;
    }

    exec("git diff -U0 HEAD^ -- " . $path, $diff_output);

    $this->lookForWhitespaceDifferences($diff_output);
  }

  private function lookForWhitespaceDifferences($diff_output) {
    $prev_lines = array();
    $in_header = true;
    foreach ($diff_output as $line) {
      if (preg_match('/^@@ -\d+(,\d+)? \+(\d+)(,\d+)? @@/', $line, $matches)) {
        // line starts a new diff section
        $prev_lines = array();
        $in_header = false;
        $line_number = $matches[2];
      } else if (!$in_header) {
        if ($line[0] == '-') {
          $prev_lines[] = substr($line, 1);
        } else if ($line[0] == '+') {
          $real_line = substr($line, 1);
          foreach ($prev_lines as $prev) {
            if ($this->differsByWhitespaceOnly($real_line, $prev)) {
              $this->raiseLintAtLine($line_number, 1,
                self::LINT_WHITESPACE_ONLY_CHANGE,
                'Changed line only differs from original by whitespace',
                $real_line, $prev);
              break;
            }
          }

          $line_number++;
        }
      }
    }
  }

  private function stripWhitespace($str) {
    return str_replace(array(" ", "\t", "\r", "\n", "\f", "\v"), "", $str);
  }

  private function differsByWhitespaceOnly($line1, $line2) {
    return $this->stripWhitespace($line1) === $this->stripWhitespace($line2);
  }
}
