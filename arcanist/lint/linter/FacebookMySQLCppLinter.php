<?php
// Copyright 2013-present Facebook.  All rights reserved.
// @author: rahulgulati (rahulgulati@fb.com)

final class FacebookMySQLCppLinter extends ArcanistLinter {

  private $fileData;
  const LINT_INDENTATION = 1;

  public function willLintPaths(array $paths) {
    return;
  }

  public function getLinterName() {
    return 'MYSQL_CPP';
  }

  public function getLintSeverityMap() {
    return array(
      self::LINT_INDENTATION
        => ArcanistLintSeverity::SEVERITY_WARNING,
    );
  }

  public function getLintNameMap() {
    return array(
      self::LINT_INDENTATION
        => 'Indentation',
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
      check indentation */
      return;
    }
    $this->lintIndentation();
  }

  private function prefixHas($line, $tabs, $tabCharacter) {
    $len = strlen($line);
    if ($tabs > $len)
      return false;
    for ($i = 0; $i < $tabs; ++$i)
      if ($line[$i] != $tabCharacter)
        return false;
    return true;
  }

  private function hasSubstring($string, $substring) {
    return substr_count($string, $substring) > 0;
  }

  private function checkIndentLevel($line, $tabs) {
    return ($this->prefixHas($line, 2 * $tabs, " ")
           || $this->prefixHas($line, $tabs, "\t"))
           || ($this->hasSubstring($line, "case")
               && ($this->prefixHas($line, 2 * ($tabs - 1), " ")
           || $this->prefixHas($line, 2 * $tabs, "\t")));
  }
  private function isPreprocessorDirective($line) {
    return $this->hasSubstring($line, "#if")
           || $this->hasSubstring($line, "#else")
           || $this->hasSubstring($line, "#endif");
  }
  private function isWhitespace($ch) {
    return $ch == " " || $ch == "\t";
  }
  private function isLabel($line) {
    if (!$this->hasSubstring($line, ":"))
    return false;
    $i = 0;
    $len = strlen($line);
    while ($i < $len) {
      if ($line[$i] == ":")
        return true;
      else if ($this->isWhitespace($line[$i]))
        return false;
      $i++;
    }
    return false;
  }
  private function inOrder($line, $a, $b, $c) {
    $x1 = strpos($line, $a);
    $x2 = strpos($line, $b);
    $x3 = strpos($line, $c);
    return $x1 < $x2 && $x2 < $x3;
  }
  protected function lintIndentation() {
    $lines = explode("\n", $this->fileData);
    $currIndentLevel = 0;
    $isComment = false;
    $insideSwitch = 0;
    $seenSwitch = false;
    foreach ($lines as $lineIdx => $line) {
      $len = strlen($line);
      if ($seenSwitch) {
        $insideSwitch -= substr_count($line, "}");
        $insideSwitch += substr_count($line, "{");
        if ($insideSwitch == 0)
          $seenSwitch = false;
        continue;
       }

    if (!$insideSwitch && $this->hasSubstring($line, "switch (")) {
      $seenSwitch = true;
      $insideSwitch += substr_count($line, "{") - substr_count($line, "}");
      continue;
    }

    if (!$isComment) {
      $isComment = $this->hasSubstring($line, "/*");
      if ($isComment &&
          $this->hasSubstring($line, "{") &&
          (!$this->hasSubstring($line, "*/") ||
           !$this->inOrder($line, "/*", "{", "*/")))
        $currIndentLevel++;
    }

    if ($isComment) {
      $isComment = !$this->hasSubstring($line, "*/");
    if (!$isComment &&
        $this->hasSubstring($line, "}") &&
        (!$this->hasSubstring($line, "*/") ||
         !$this->inOrder($line, "/*", "}", "*/")))
      $currIndentLevel--;
      continue;
    }
    for ($i = 0; $i < $len; ++$i)

      if ($this->hasSubstring($line, "extern \"C\""))
        continue;

    // If the line is empty, then don't check it's indentation level.
    if (!$len)
      continue;

    // Don't test indentation if it's a preprocessor directive.
    if ($this->isPreprocessorDirective($line))
      continue;

    // Don't test indentation for labels.
    if ($this->isLabel($line))
      continue;

    $oldIndentLevel = $currIndentLevel;
    $extraClosedBraces = 0;
    $didCloseBrace = false;
    for ($i = 0; $i < $len; $i++) {
      if ($line[$i] == "{") {
        $currIndentLevel++;
        $extraClosedBraces--;
      }
      else if ($line[$i] == "}") {
        $currIndentLevel--;
        $extraClosedBraces++;
        $didCloseBrace = true;
      }
    }
    $tabs = 0;
    if ($extraClosedBraces >= 0)
      $tabs = $currIndentLevel - ($didCloseBrace == true? 1 : 0);
    else
      $tabs = $oldIndentLevel;
    if (!$this->checkIndentLevel($line, $tabs))
      $this->raiseLintAtLine(
      $lineIdx + 1,
      1,
      self::LINT_INDENTATION,
      'This line possibly has an indentation error',
      $line);
    }
  }
}

