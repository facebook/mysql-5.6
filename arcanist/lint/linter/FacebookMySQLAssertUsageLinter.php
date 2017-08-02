<?php
// Copyright 2015, Facebook, Inc.  All rights reserved.
// @author: Gunnar Kudrjavets (gunnarku@fb.com)

class FacebookMySQLAssertUsageLinter extends ArcanistLinter {
  const LINT_ASSERT = 1;

  public function willLintPaths(array $paths) {
    return;
  }

  public function getLinterName() {
    return 'MySQLAssert';
  }

  public function getLintSeverityMap() {
    return array(
      self::LINT_ASSERT
        => ArcanistLintSeverity::SEVERITY_ADVICE,
    );
  }

  public function getLintNameMap() {
    return array(
      self::LINT_ASSERT
        => 'Assertion macro usage',
    );
  }

  public function lintPath($path) {
    if ($this->didStopAllLinters()) {
      return;
    }

    $lines = explode("\n", $this->getData($path));

    foreach ($lines as $line_idx => $line) {
      $trimmedLine = trim($line);

      // We want to make sure that macros or type definitions like
      // mysql_mutex_assert_owner or compile_time_assert won't be flagged
      // by this rule and we'll have an opportunity to turn the warning off
      // as well when needed.

      // NO_LINT_DEBUG comment can be used to turn off this advice.
      if (($line_idx > 0 &&
          strpos($lines[$line_idx - 1], 'NO_LINT_DEBUG') === false) &&
          !(preg_match('/[\t ]*static_assert[\t ]*\(/', $trimmedLine)) &&
          (preg_match('/[\t ]*assert[\t ]*\(/', $trimmedLine) === 1)) {
        $this->raiseLintAtLine(
          $line_idx + 1,
          1,
          self::LINT_ASSERT,
          'Please use DBUG_ASSERT macro instead of assert() macro. '.
          'DBUG_ASSERT is a standard MySQL debugging macro and this way '.
          'the code will look more unified. Once you have made the changes '.
          'then please verify them with a release build. '.
          'You can suppress this advice by adding a comment NO_LINT_DEBUG '.
          'above this line.',
          $line);
      }
    }
  }
}

