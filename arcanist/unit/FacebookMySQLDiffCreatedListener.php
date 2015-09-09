<?php
// Copyright 2004-present Facebook. All Rights Reserved.

final class FacebookMySQLDiffCreatedListener extends PhutilEventListener {

  public function register() {
    $this->listen(ArcanistEventType::TYPE_DIFF_WASCREATED);
  }

  public function handleEvent(PhutilEvent $event) {
    if ($event->getValue('unitResult') == ArcanistUnitWorkflow::RESULT_SKIP) {
      return;
    }

    $workflow = $event->getValue('workflow');
    $diffID = $event->getValue('diffID');

    $big = $workflow->getArgument('big-test-queue');

    $this->startJenkinsBuilds($big, $diffID);
  }

  // Enqueue Jenkins build & test
  function startJenkinsBuilds($big, $diffID) {
    $console = PhutilConsole::getConsole();

    $server = "ci-builds.fb.com";
    $project = "github-mysql-precommit";
    if ($big === true) {
      $console->writeOut("Tests will use the 'big' queue.\n");
      $project = "github-mysql-precommit-big";
    }

    // Push the source up to the master repo so that Jenkins
    // can pull it down and build it
    $repository = "git@github.com:facebook/mysql-5.6.git";
    $gitcmd = "git push {$repository} HEAD:refs/autobuilds/{$diffID}";
    $git_future = new ExecFuture($gitcmd);
    $git_future->resolvex();

    $console->writeOut(
      '%s',
      "Launching a build for $diffID on the Jenkins server...\n");

    // Initiate a Jenkins build for this diff_id
    $last_line = system("wget -q -O - "
      . "'http://$server/job/$project"
      . "/buildWithParameters?token=ARC&DIFF_ID={$diffID}'",
      $retval);
    if ($retval) {
      $console->writeOut("Attempt to launch Jenkins build failed with:\n%s\n",
                         $last_line);
      throw new Exception("Launch of Jenkins build failed.");
    }
  }
}
