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

    foreach ($paths as $path) {
      $is_text = false;

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

      if (preg_match($text_extensions, $path)) {
        $is_text = true;
      }

      if ($is_text) {
        $nolint_linter->addPath($path);
        $generated_linter->addPath($path);
        $generated_linter->addData($path, $this->loadData($path));
        $mysql_linter->addPath($path);
        $mysql_linter->addData($path, $this->loadData($path));
        $spelling_linter->addPath($path);
        $spelling_linter->addData($path, $this->loadData($path));
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
