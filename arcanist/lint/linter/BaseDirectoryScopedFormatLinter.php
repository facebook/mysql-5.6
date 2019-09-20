<?php
// Copyright 2004-present Facebook. All Rights Reserved.

abstract class BaseDirectoryScopedFormatLinter extends ArcanistLinter {

  const LINT_FORMATTING = 1;

  private $changedLines = array();
  private $rawLintOutput = array();
  private $pathsThatRaisedLint = array();

  abstract protected function getPathsToLint();

  /**
   * Override and add paths to exclude subdirectories of paths returned by
   * `getPathsToLint`. For example, if you want to include `a/*` except for
   * `a/b`, you can add `a/` to `getPathsToLint` and `a/b` to
   * `getPathsToExcludeFromLint`.
   */
  protected function getPathsToExcludeFromLint() {
    return array();
  }

  protected function shouldLintPath($path) {
    return self::matchPath($path, $this->getPathsToLint()) &&
      !self::matchPath($path, $this->getPathsToExcludeFromLint());
  }

  protected static function matchPath($needle, $haystack) {
    foreach ($haystack as $p) {
      // check if $path starts with $p
      if (strncmp($needle, $p, strlen($p)) === 0) {
        return true;
      }
    }
    return false;
  }

  // API to tell this linter which lines were changed
  final public function setPathChangedLines($path, $changed) {
    $this->changedLines[$path] = $changed;
  }

  final public function willLintPaths(array $paths) {
    $futures = array();

    $this->prepareFormat();

    foreach ($paths as $path) {
      if (!$this->shouldLintPath($path)) {
        continue;
      }

      $changed = $this->changedLines[$path];
      if (!isset($changed)) {
        // do not run linter if there are no changes
        continue;
      }

      $futures[$path] = $this->getFormatFuture($path, $changed);
    }

    foreach (Futures($futures)->limit(8) as $p => $f) {
      $this->rawLintOutput[$p] = $f->resolvex();
    }
  }

  protected function prepareFormat() {}
  abstract protected function getFormatFuture($path, array $changed);
  abstract protected function getLintMessage($diff);

  final public function lintPath($path) {
    if (!isset($this->rawLintOutput[$path])) {
      return;
    }

    list($new_content) = $this->rawLintOutput[$path];
    $old_content = $this->getData($path);

    if ($new_content != $old_content) {
      $diff = ArcanistDiffUtils::renderDifferences($old_content, $new_content);
      $this->raiseLintAtOffset(
        0,
        self::LINT_FORMATTING,
        $this->getLintMessage($diff),
        $old_content,
        $new_content);
      $this->setDidModifySource();
      $this->pathsThatRaisedLint[$path] = true;
    }
  }

  final public function hasRaisedLint($path) {
    return array_key_exists($path, $this->pathsThatRaisedLint);
  }
}
