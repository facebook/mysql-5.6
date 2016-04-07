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
        // If you're brave enough and want to start dogfooding Sandcastle
        // integration then set this environment variable and instead of
        // Jenkins build the code will take you to the unexplored territory.
        if (getenv('USE_SANDCASTLE')) {
          $this->startSandcastleBuilds($workflow->getArgument('big-test-queue'),
                                       $diffID,
                                       $workflow->getUserName());
        } else {
          $this->startJenkinsBuilds($workflow->getArgument('big-test-queue'),
                                    $diffID);
        }
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
  function startSandcastleBuilds($useBigTestQueue, $diffID, $username) {
    assert(is_numeric($diffID));
    assert(strlen($username) > 0);

    // All the real work will be done by the code in the file referenced
    // below. File will be there because shouldStartBuilds() guards the
    // execution of this section of code.
    require(MYSQL_INTERNAL_DIFF_DETERMINATOR);

    StartSandCastleBuild($useBigTestQueue, $diffID, $username);
  }

  // Enqueue Jenkins build & test.
  function startJenkinsBuilds($big, $diffID) {
    $console = PhutilConsole::getConsole();

    $server = "ci-builds.fb.com";
    $project = "github-mysql-precommit";
    if ($big) {
      $console->writeOut("Tests will use the 'big' queue.\n");
      $project = "github-mysql-precommit-big";
    }

    // Push the source up to the master repo so that Jenkins
    // can pull it down and build it
    $repository = "git@github.com:facebook/mysql-5.6.git";
    $gitcmd = "git push {$repository} HEAD:refs/autobuilds/{$diffID}";
    $git_future = new ExecFuture($gitcmd);
    $git_future->resolvex();

    $console->writeOut("Launching async tests for %d.\n",
                       $diffID);

    // Initiate a Jenkins build for this diff_id
    $jenkins_cmd = "wget -q -O - --no-proxy "
      . "'http://$server/job/$project"
      . "/buildWithParameters?token=ARC&DIFF_ID={$diffID}'";
    $last_line = system($jenkins_cmd, $retval);

    if ($retval) {
      $console->writeOut("Attempt to launch Jenkins build returned %d.\n"
                         ."Command run: %s\n"
                         ."Last line of output:\n%s\n",
                         $retval, $jenkins_cmd, $last_line);
      throw new Exception("Launch of Jenkins build failed.");
    }
  }
}
