<?php
// Copyright 2004-present Facebook. All Rights Reserved.

// Internal diff determinator for Sandcastle. The file will exist only in the
// FB-specific environment.
define("MYSQL_INTERNAL_DIFF_DETERMINATOR",
       "../tools/sandcastle/mysql_diff_determinator.php");

final class FacebookMySQLDiffCreatedListener extends PhutilEventListener {

  public function register() {
    assert_options(ASSERT_BAIL, true);
    $this->listen(ArcanistEventType::TYPE_DIFF_WASCREATED);
  }

  public function handleEvent(PhutilEvent $event) {
    if ($event->getValue('unitResult') == ArcanistUnitWorkflow::RESULT_SKIP) {
      return;
    }
    $workflow = $event->getValue('workflow');
    assert($workflow);

    $diffID = $event->getValue('diffID');
    assert(is_numeric($diffID));

    if ($this->shouldStartBuilds($workflow)) {
      $this->startSandcastleBuilds($workflow, $diffID);
    } else {
      $console = PhutilConsole::getConsole();
      $console->writeOut("Skipping launch of tests. Ask a Facebook " .
                         "reviewer to launch tests for %d.\n", $diffID);
    }
  }

  // Should we try to launch CI builds / tests?
  function shouldStartBuilds($workflow) {
    // If this sentinel file exists, assume that we have access to
    // the CI servers.
    $sentinel = MYSQL_INTERNAL_DIFF_DETERMINATOR;
    $working_copy = $workflow->getWorkingCopy();
    assert($working_copy);

    return Filesystem::pathExists($working_copy->getProjectPath($sentinel));
  }

  // Start Sandcastle build and test execution.
  function startSandcastleBuilds($workflow, $diffID) {
    assert($workflow);
    assert(strlen($diffID) > 0);
    assert(is_numeric($diffID));

    $username = $workflow->getUserName();
    assert(strlen($username) > 0);

    $working_copy = $workflow->getWorkingCopy();
    assert($working_copy);

    $full_diff_det_path = $working_copy->getProjectPath(
        MYSQL_INTERNAL_DIFF_DETERMINATOR);

    // All the real work will be done by the code in the file referenced
    // below. File will be there because shouldStartBuilds() guards the
    // execution of this section of code.
    require($full_diff_det_path);

    StartSandCastleBuild($diffID, $username);
  }
}
