<?php
// Copyright 2004-present Facebook. All Rights Reserved.

// When running arc diff, the user's commit is pushed to the master git
// repository into a branch named refs/autobuilds/<diffid>. Then wget
// is used to initiate a Jenkins poll of the source repo. When the new
// commit is found, it is pulled down to a build slave and built. The
// Jenkins job is configured so that the arcanist/bin/update_build_info.php
// program reads the stdout and updates the phabricator diff page with
// the test results, for 'arc diff'.

final class FacebookFbcodeUnitTestEngine extends ArcanistBaseUnitTestEngine {
  protected $big=false;

  public function setRunAllTests() {
    // When --everything is passed to 'arc unit', run the big tests.
    $this->big = true;
  }

  public function run() {
    // If we are running asynchronously, mark all tests as postponed
    // and return those results.  Otherwise, run the tests and collect
    // the actual results.
    if ($this->getEnableAsyncTests()) {
      $results = array();
      $result = new ArcanistUnitTestResult();
      $result->setName("mysql_build");
      $result->setResult(ArcanistUnitTestResult::RESULT_POSTPONED);
      $results[] = $result;
      return $results;
    } else {
      $server = new FacebookBuildServer();
      $server->startProjectBuilds(false, $this->big);
      return array();
    }
  }

}
