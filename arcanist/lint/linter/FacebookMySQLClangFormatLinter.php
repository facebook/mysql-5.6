<?php
// Copyright 2004-present Facebook. All Rights Reserved.

final class FacebookMySQLClangFormatLinter
    extends BaseDirectoryScopedFormatLinter {

  const LINT_FORMATTING = 1;
  const CLANG_FORMAT_BINARY =
    '/opt/rh/llvm-toolset-7/root/usr/bin/clang-format';
  const CLANG_FORMAT_FALLBACK_BINARY =
    '/usr/local/bin/clang-format';

  private $clang_format_binary = "";

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

  protected function prepareFormat() {
    if (!file_exists(self::CLANG_FORMAT_BINARY)) {
      PhutilConsole::getConsole()->writeOut(
        "clang-format 5 not detected. Proceed to installation...\n");

      $working_copy = $this->getEngine()->getWorkingCopy();

      $using_sandcastle =
        FacebookMySQLArcanistConfiguration::isUsingSandcastle();

      // For security reasons install-clang-5.sh lives in tools
      $clang_install_cmd =
        FacebookMySQLArcanistConfiguration::pathInTools(
          $using_sandcastle, $working_copy, "install-clang-5.sh");

      $this->clang_format_binary = self::CLANG_FORMAT_FALLBACK_BINARY;

      // Install clang-5
      PhutilConsole::getConsole()->writeOut(
        "Running $clang_install_cmd...\n");
      list($err, $stdout, $stderr) = exec_manual($clang_install_cmd);

      if ($err != 0) {
        PhutilConsole::getConsole()->writeErr(
          "$clang_install_cmd failed with %d. Falling back to %s...\n" .
          "\nStdout:\n%s\nStderr:\n%s\n",
          $err, self::CLANG_FORMAT_FALLBACK_BINARY, $stdout, $stderr);

        return;
      }

      PhutilConsole::getConsole()->writeOut("Installation complete.\n");
    }

    // Validate version is 5.0.1
    list($err, $stdout, $stderr) =
      exec_manual(self::CLANG_FORMAT_BINARY . " --version");

    if ($err) {
      PhutilConsole::getConsole()->writeErr(
        "clang-format --version failed with %d. " .
        "Falling back to %s...\n",
        $err, self::CLANG_FORMAT_FALLBACK_BINARY);

      return;
    }

    if (!strpos($stdout, "5.0.1")) {
      PhutilConsole::getConsole()->writeErr(
        "Unexpected clang-format version: %s" .
        "Expecting 5.0.1. Falling back to %s...\n",
        $stdout, self::CLANG_FORMAT_FALLBACK_BINARY);

      return;
    }

    $this->clang_format_binary = self::CLANG_FORMAT_BINARY;
  }

  protected function getFormatFuture($path, array $changed) {
    $args = "";
    foreach ($changed as $key => $value) {
      $args .= " --lines=$key:$key";
    }

    return new ExecFuture(
      "%s %s $args",
      $this->clang_format_binary,
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
