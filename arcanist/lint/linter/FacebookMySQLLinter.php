<?php
// Copyright 2013-present Facebook.  All rights reserved.
// @author: rahulgulati (rahulgulati@fb.com)

final class FacebookMySQLLinter extends ArcanistLinter {

  const LINT_DOS_NEWLINE         = 1;
  const LINT_LINE_WRAP           = 2;
  const LINT_EOF_NEWLINE         = 3;
  const LINT_TRAILING_WHITESPACE = 4;

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
    return 'MYSQL';
  }

  public function getLintSeverityMap() {
    return array(
      self::LINT_DOS_NEWLINE
        => ArcanistLintSeverity::SEVERITY_WARNING,
      self::LINT_LINE_WRAP
        => ArcanistLintSeverity::SEVERITY_WARNING,
      self::LINT_EOF_NEWLINE
        => ArcanistLintSeverity::SEVERITY_WARNING,
      self::LINT_TRAILING_WHITESPACE
        => ArcanistLintSeverity::SEVERITY_AUTOFIX,
      );
  }

  public function getLintNameMap() {
    return array(
      self::LINT_DOS_NEWLINE
        => 'DOS Newlines',
      self::LINT_LINE_WRAP
        => 'Line Too Long',
      self::LINT_EOF_NEWLINE
        => 'File Does Not End in Newline',
      self::LINT_TRAILING_WHITESPACE
        => 'Trailing Whitespace',
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

    $this->lintNewLines();
    $this->lintLineLength();
    $this->lintEOFNewline();
    $this->lintTrailingWhitespace();
  }

  protected function lintNewlines() {
    $pos = strpos($this->fileData, "\r");
    if ($pos !== false) {
      $this->raiseLintAtOffset(
      $pos,
      self::LINT_DOS_NEWLINE,
      'You must only use UNIX linebreaks ("\n") in source code',
      "\r");
    }
  }

  protected function lintLineLength() {
    $lines = explode("\n", $this->fileData);
    $width = $this->maxLineLength;
    $tabLength = 8;
    foreach ($lines as $line_idx => $line) {
      $lineLength = 0;
      $stringLength = strlen($line);
      for ($i = 0; $i < $stringLength; $i++) {
        $curr = $line[$i];
        if ($curr === "\t") {
          $lineLength += $tabLength;
          $lineLength -= ($lineLength % $tabLength);
        }
        else
          $lineLength++;
      }
      if ($lineLength > $width) {
        $this->raiseLintAtLine(
        $line_idx + 1,
        1,
        self::LINT_LINE_WRAP,
        'This line is '.$lineLength.' characters long, '.
        'but the convention is '.$width.' characters.',
        $line);
      }
    }
  }

  protected function lintEOFNewline() {
    $len = strlen($this->fileData);
    if (!$len || $this->fileData[$len - 1] != "\n") {
      $this->raiseLintAtOffset(
        $len,
        self::LINT_EOF_NEWLINE,
        "Files must end in a newline.",
        '',
        "\n");
    }
  }

  protected function lintTrailingWhitespace() {
    $matches = null;
    $preg = preg_match_all(
      '/[ \t]+$/m',
      $this->fileData,
      $matches,
      PREG_OFFSET_CAPTURE);

    if (!$preg) {
      return;
    }

    foreach($matches[0] as $match) {
      list($string, $offset) = $match;
      $this->raiseLintAtOffset(
        $offset,
        self::LINT_TRAILING_WHITESPACE,
        'This line contains trailing whitespace.',
        $string,
        '');

    }
  }
}
