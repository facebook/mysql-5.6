<?php
// Copyright 2015, Facebook, Inc.  All rights reserved.
// @author: Jay Edgar (jkedgar@fb.com)

class FacebookInnoDBTabLinter extends ArcanistLinter {

  const LINT_INCORRECT_TAB = 1;
  const LINT_TABSTOP = 8;

  public function getLinterName() {
    return 'InnoDB_tab';
  }

  public function getLintSeverityMap() {
    return array(
      self::LINT_INCORRECT_TAB => ArcanistLintSeverity::SEVERITY_AUTOFIX,
    );
  }

  public function getLintNameMap() {
    return array(
      self::LINT_INCORRECT_TAB => 'Incorrect tab usage',
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

    if (strpos($this->fileData, "\t")) {
      // There are a few files in storage/innobase that don't use tabs.
      // Only check for correct usage of tabs if there is at least one
      // tab in the file.
      $this->lintInnoDBTabs();
    }
  }

  private function move_to_tabstop($pos) {
    $past_tabstop = $pos % self::LINT_TABSTOP;
    if ($past_tabstop > 0) {
      $pos += self::LINT_TABSTOP - $past_tabstop;
    }

    return $pos;
  }

  private function tabify($line) {
    $adjusted = "";    // Adjust result
    $whitespace = "";  // Buffer holding recently seen whitespace chars
    $pos = 0;          // Current position (when tabs are considered)

    $len = strlen($line);
    for ($ii = 0; $ii < $len; $ii++) {
      $char = $line[$ii];
      // Whenever the current position is at a tabstop we need to see if we
      // need to make any adjustments.
      if ($pos % self::LINT_TABSTOP == 0) {
        $wslen = strlen($whitespace);
        if ($wslen > 1 || ($wslen > 0 && ($char == "\t" || $char == " "))) {
          // If we have seen 2 or more whitespace chars or a single whitespace
          // char and the next char is also a whitespace char then replace the
          // seen whitespace chars with a tab.
          $adjusted .= "\t";
        } else {
          // Otherwise just append whatever whitespace char we have seen.
          $adjusted .= $whitespace;
        }

        // Clear out the seen whitespace.
        $whitespace = "";
      }

      $pos++;
      if ($char == "\t") {
        // The current char is a tab - append it to the seen whitespace chars
        // and adjust the position to the next tab stop.
        $whitespace .= "\t";
        $pos = $this->move_to_tabstop($pos);
      } else if ($char == " ") {
        // The current char is a space - append it to the seen whitespace and
        // increment the position.
        $whitespace .= " ";
      } else {
        // The current char is not a tab or space - append any seen
        // whitespace chars and the current char.  Clear out the seen
        // whitespace and increment the position.
        $adjusted .= $whitespace . $char;
        $whitespace = "";
      }
    }

    // Add any seen whitespace to the end.  This linter is not attempting to
    // notify about extra whitespace on the end of a line.  Other linters can
    // handle that job.
    $adjusted .= $whitespace;

    return $adjusted;
  }

  private function lintInnoDBTabs() {
    $lines = explode("\n", $this->fileData);
    foreach ($lines as $line_idx => $line) {
      // Replace spaces with tabs where it is expected.  Compare the result
      // with the original.  If there are differences then the line does not
      // follow the convention and is flagged.
      $tabified = $this->tabify($line);
      if ($line !== $tabified) {
        $this->raiseLintAtLine($line_idx + 1, 1, self::LINT_INCORRECT_TAB,
          'This line does not use tabs in the correct convention',
          $line, $tabified);
      }
    }
  }
}
