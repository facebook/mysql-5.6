<?php
// Copyright 2004-present Facebook. All Rights Reserved.

final class FacebookMySQLClangFormatLinter
    extends BaseDirectoryScopedFormatLinter {

  const LINT_FORMATTING = 1;
  const CLANG_FORMAT_BINARY =
    '/usr/local/bin/clang-format';

  protected function getPathsToLint() {
    return array(
      'storage/rocksdb/',
    );
  }

  public function getLinterName() {
    return 'CLANG_FORMAT';
  }

  public function getLinterPriority() {
    // As of today, this needs to be between 0.25 and 0.50.
    // After ArcanistGeneratedLinter, so it doesn't apply to files
    // using generated tag (it breaks the signed source hash).
    // Before FbcodeLineWrapLinter and ArcanistTextLinter, so clang-format
    // has a chance to fix the 80 lines column issues.
    return 0.3;
  }

  public function getLintSeverityMap() {
    return array(
      self::LINT_FORMATTING => ArcanistLintSeverity::SEVERITY_ADVICE,
    );
  }

  public function getLintNameMap() {
    return array(
      self::LINT_FORMATTING => pht('Changes are not clang-formatted'),
    );
  }

  protected function getFormatFuture($path, array $changed) {
    if (!file_exists(self::CLANG_FORMAT_BINARY)) {
      PhutilConsole::getConsole()->writeOut(
        "clang-format not detected. Skipping...\n");
      return;
    }

    $args = "";
    foreach ($changed as $key => $value) {
      $args .= " --lines=$key:$key";
    }

    return new ExecFuture(
      "%s -style=file %s $args",
      self::CLANG_FORMAT_BINARY,
      $this->getEngine()->getFilePathOnDisk($path));
  }

  protected function getLintMessage($diff) {
    return <<<LINT_MSG
Changes in this file were not formatted using clang-format
See proposed changes:

$diff

LINT_MSG;
  }
}
