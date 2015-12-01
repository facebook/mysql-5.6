<?php
// Copyright 2015, Facebook, Inc.  All rights reserved.
// @author: Jay Edgar (jkedgar@fb.com)

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

  private function didPrevLineChangeIndent($prev) {
    if (isset($prev)) {
      $last_char = substr(trim($prev), -1);
      if ($last_char == '{' or $last_char == '}' or $last_char == ':') {
        return true;
      }
    }

    return false;
  }

  private function lookForWhitespaceDifferences($diff_output) {
    $old_lines = array();
    $in_header = true;
    $prev = NULL;
    $last_match_didnt_warn = false;
    foreach ($diff_output as $new_key => $line) {
      if (preg_match('/^@@ -\d+(,\d+)? \+(\d+)(,\d+)? @@/', $line, $matches)) {
        // line starts a new diff section
        $old_lines = array();
        $in_header = false;
        $line_number = $matches[2];
      } else if (!$in_header) {
        if ($line[0] == '-') {
          $old_lines[] = [substr($line, 1), false];
        } else if ($line[0] == '+') {
          $prev_new_diff_indent = $this->didPrevLineChangeIndent($prev);

          $prev_old = NULL;
          $real_line = substr($line, 1);
          foreach ($old_lines as $old_key => $old) {
            $prev_old_diff_indent = $this->didPrevLineChangeIndent($prev_old);

            if (!$old[1]) {
              if ($this->differsByWhitespaceOnly($real_line, $old[0])) {
                if (($prev_new_diff_indent xor $prev_old_diff_indent) or
                     $last_match_didnt_warn) {
                  // Don't warn - the code indicates we are in a block that
                  // changed indentation intentionally.
                  $last_match_didnt_warn = true;
                } else {
                  $this->raiseLintAtLine($line_number, 1,
                    self::LINT_WHITESPACE_ONLY_CHANGE,
                    'Changed line only differs from original by whitespace',
                    $real_line, $old[0]);
                  // Mark this entry in the old_lines array so that it isn't
                  // used twice
                  $old_lines[$old_key] = [$old[0], true];
                  $last_match_didnt_warn = false;
                  break;
                }
              }
            } else {
              $last_match_didnt_warn = false;
            }

            $prev_old = $old[0];
          }

          $line_number++;
          $prev = $line;
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
