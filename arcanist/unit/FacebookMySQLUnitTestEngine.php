<?php
// Copyright 2004-present Facebook. All Rights Reserved.

final class FacebookMySQLUnitTestEngine extends ArcanistBaseUnitTestEngine {

  // This test engine only supports async tests in our CI framework, so
  // complain if --everything specified as it's meaningless.
  protected function supportsRunAllTests() {
    return false;
  }

  public function run() {
    if ($this->getEnableAsyncTests()) {
      // 'arc diff' workflow.
      //
      // Mark as postponed and we'll trigger the tests in the diff created
      // event.
      $results = array();
      $result = new ArcanistUnitTestResult();
      $result->setName("mysql_build");
      $result->setResult(ArcanistUnitTestResult::RESULT_POSTPONED);
      $results[] = $result;
      return $results;
    } else {
      // 'arc unit' workflow - not (yet) supported.
      $console = PhutilConsole::getConsole();
      $console->writeOut("No 'arc unit' integration implemented.\n");

      return array();
    }
  }

}
