<?php
// Copyright 2004-present Facebook. All Rights Reserved.

// When running arc diff, the user's commit is pushed to the master git
// repository into a branch named refs/autobuilds/<diffid>. Then wget
// is used to initiate a Jenkins poll of the source repo. When the new
// commit is found, it is pulled down to a build slave and built. The
// Jenkins job is configured so that the arcanist/bin/update_build_info.php
// program reads the stdout and updates the phabricator diff page with
// the test results, for 'arc diff'.

final class FacebookBuildServer {

  // This is a terrible terrible function to try to find the build which
  // currently running the requested git rev, using web scraping.
  private function findJenkinsBuild($server, $project, $rev) {
    // Get currently "last" build number as a starting point
    $start = exec("wget -q -O - "
      . "http://$server/job/$project/lastBuild"
      . " | awk '/<title>/ {gsub(/#/,\"\", $2); print $2}'") + 0;

    $top = $start + 100; // Sanity limit of possible builds
    $pass = -1; // -1 (down), then 1 (up)
    $buildnum = $start; // Start with current build
    while ($buildnum < $top && $buildnum >= 1) {
      $atrev = substr(exec("wget -q -O - "
        . "http://$server/job/$project/$buildnum/"
        . " | grep '>Revision<'", $discard, $exval), -40);

      // If that page doesn't exist, or has no git rev listed.
      if ($exval != 0 || !preg_match('/\A[a-f0-9]{40}\Z/', $atrev)) {
        if ($pass == -1) { // Going Down - skip it and keep going
          $buildnum += $pass;
          continue;
        } else { // Going Up - At end of active builds, failed to find mine.
          return 0;
        }

      // Found mine, success.
      } elseif ($rev === $atrev) {
        return $buildnum;

      // Got all the way to the beginning, try counting up
      } elseif ($pass == -1) {
        $pass = 1; // Switch to counting up
        $buildnum = $start; // Try current again, in case it's now available.

      // Continue down or up (depending on $pass mode).
      } else {
        $buildnum += $pass;
      }
    }
    // Exceeded search range, failed to find my rev.
    return 0;
  }

  public function startProjectBuilds($async, $big=false, $diff_id=null) {
    $console = PhutilConsole::getConsole();

    $server = "ci-builds.fb.com";
    $project = "mysql-precommit";
    if ($big === true) {
      $console->writeOut("Forcing build of the full 'big' test set.\n");
      $project = "mysql-precommit-big";
    }

    $branch = $diff_id ?: "unit-" . getenv('USER') . "-" . microtime(TRUE);
    $rev = exec('git log -1 --pretty=format:%H');

    $buildnum = 0;
    if ($async !== true) {
      $buildnum = $this->findJenkinsBuild($server, $project, $rev);
    }

    if ($async === true || $buildnum === 0) {
      $console->writeOut(
        '%s',
        "Launching a build for $branch on the Jenkins server...\n");

      // Push the source up to the master repo so that Jenkins
      // can pull it down and build it
      $gitcmd = "git push origin HEAD:refs/autobuilds/{$branch}";
      $git_future = new ExecFuture($gitcmd);
      $git_future->resolvex();

      // Initiate a Jenkins build for exactly this revision
      exec("wget -q -O - "
        . "'http://$server/job/$project"
        . "/buildWithParameters?token=ARC&REV={$branch}'");
    } else {
      $console->writeOut(
        "Connecting to pre-existing build for this git revision...\n");
    }

    if ($async !== true) {
      while($buildnum == 0) {
        $console->writeOut("Waiting for build to start...\n");
        sleep(60);
        $buildnum = $this->findJenkinsBuild($server, $project, $rev);
      }
      $console->writeOut('%s', "Rev = $rev (Jenkins Build #$buildnum)\n");
      $console->writeOut('%s',
        "http://$server/view/MySQL/job/$project/$buildnum/\n");
    }
  }
}
