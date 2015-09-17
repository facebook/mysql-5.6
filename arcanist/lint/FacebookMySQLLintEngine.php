<?php
// Copyright 2013-present Facebook.  All rights reserved.
// @author: rahulgulati (rahulgulati@fb.com)

class FacebookMySQLLintEngine extends ArcanistLintEngine {

  public function buildLinters() {
    $linters = array();
    $paths = $this->getPaths();

    foreach ($paths as $key => $path) {
      if (!Filesystem::pathExists($this->getFilePathOnDisk($path))) {
        unset($paths[$key]);
      }
    }

    // ArcanistGeneratedLinter stops other linters from
    // running on generated code.
    $generated_linter = new ArcanistGeneratedLinter();
    $linters[] = $generated_linter;

    // ArcanistNoLintLinter stops other linters from
    // running on code marked with a nolint annotation.
    $nolint_linter = new ArcanistNoLintLinter();
    $linters[] = $nolint_linter;

    // FacebookMySqlLinter enforces the following lint
    // checks: max line length is 80 characters, use Unix
    // newlines instead of DOS newlines, Files should end
    // in a new line, and, lines containing trailing whitespace.
    $mysql_linter = new FacebookMySQLLinter();
    $linters[] = $mysql_linter;

    // Standard test linter with line length of 100 characters
    // for java files.
    $java_text_linter = new ArcanistTextLinter();
    $java_text_linter->setMaxLineLength(100);
    $java_text_linter->setCustomSeverityMap(array(
      ArcanistTextLinter::LINT_LINE_WRAP
        => ARcanistLintSeverity::SEVERITY_ADVICE,
    ));
    $linters[] = $java_text_linter;

    if (!$this->getCommitHookMode()) {
      $cpp_linters = array();
      // For C++ code use both the standard c++ linter and the
      // fbcode C++ linter
      $cpp_linters[] = $linters[] = new ArcanistCpplintLinter();
      $cpp_linters[] = $linters[] = new FbcodeCppLinter();

      $mysql_whitespace_linter = new FacebookMySQLWhitespaceLinter();
      $linters[] = $mysql_whitespace_linter;

      $innodb_tab_linter = new FacebookInnoDBTabLinter();
      $linters[] = $innodb_tab_linter;
    }

    // Enforces basic spelling. A blacklisted set of words that
    // are commonly spelled incorrectly are used.
    $spelling_linter = new ArcanistSpellingLinter();

    $spelling_linter->setCustomSeverityMap(
      array(
        ArcanistSpellingLinter::LINT_SPELLING_PICKY
          => ArcanistLintSeverity::SEVERITY_WARNING,
        ArcanistSpellingLinter::LINT_SPELLING_IMPORTANT
          => ArcanistLintSeverity::SEVERITY_WARNING,
      )
    );

    $linters[] = $spelling_linter;

    $text_extensions = (
      '/\.('.
      'cpp|cxx|c|cc|h|hpp|hxx|tcc'.
      'txt'.
      'test'.
      'py'.
      'sh'.
      'cmake'.
      'css'.
      'sql'.
      'inc'.
      'pl'.
      'php'.
      'json'.
      'java'.
      'html'.
      'ic'.
      'yy'.
      ')$/'
    );

    $cpp_extensions = (
      '/\.('.
      'cpp|cxx|c|cc|h|hpp|hxx|tcc'.
      ')$/'
    );

    foreach ($paths as $path) {
      $is_text = false;

      if (preg_match($text_extensions, $path)) {
        $is_text = true;
      }

      if ($is_text) {
        $is_myrocks = false;
        $is_innodb = false;

        if (preg_match('/storage\/rocksdb/', $path)) {
          $is_myrocks = true;
        }

        if (preg_match('/storage\/innobase/', $path)) {
          $is_innodb = true;
        }

        $nolint_linter->addPath($path);

        $generated_linter->addPath($path);
        $generated_linter->addData($path, $this->loadData($path));

        if ($is_myrocks && preg_match('/\.java$/', $path)) {
          $java_text_linter->addPath($path);
          $java_text_linter->addData($path, $this->loadData($path));
        } else {
          $mysql_linter->addPath($path);
          $mysql_linter->addData($path, $this->loadData($path));
        }

        $spelling_linter->addPath($path);
        $spelling_linter->addData($path, $this->loadData($path));

        if (preg_match($cpp_extensions, $path)) {
          if ($is_myrocks) {
            // For MyRocks code (in storage/engine) use strict linters
            foreach ($cpp_linters as &$linter) {
              $linter->addPath($path);
              $linter->addData($path, $this->loadData($path));
            }
          } else {
            // For all other MySQL code check to make sure we aren't
            // changing just whitespace on a line
            $mysql_whitespace_linter->addPath($path);
            $mysql_whitespace_linter->addData($path, $this->loadData($path));

            if ($is_innodb) {
              // If in InnoDB source code (storage/innobase) check to make
              // sure are using tabs (not spaces)
              $innodb_tab_linter->addPath($path);
              $innodb_tab_linter->addData($path, $this->loadData($path));
            }
          }
        }
      }
    }

    // ArcanistFilenameLinter stifles creativity in choosing
    // imaginative file names.
    $name_linter = new ArcanistFilenameLinter();
    $linters[] = $name_linter;

    foreach ($paths as $path) {
        $name_linter->addPath($path);
    }
    return $linters;
  }
}
