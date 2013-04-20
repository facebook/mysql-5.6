#!/usr/bin/env php
<?php

// Copyright 2004-present Facebook. All Rights Reserved.

/**
 * This program is used to Update the build and test results on the
 * diff page and is meant to be run from within a Jenkins Job.
 *
 * Some of the information this program needs is read from the
 * environment instead of command line options. Specifically,
 * the BUILD_TYPE and GIT_BRANCH are expected to be set.
 *
 * This program can be run in two modes:
 *
 *  * As a pipe to read the test output.
 *    Use this mode when running the tests so that unit failures
 *    are posted as they are found. From the jenkins job, do something
 *    like:
 *    new-mysqldev.sh 56test --skip-test-list=test.skip | update_build_info.php
 *
 *  * As a single invocation to set the build status.
 *    Use this mode when the build portion fails. Use the
 *    --status= and --message= options to update the diff page.
 *
 *    From the jenkins job, do:
 *    new-mysqldev.sh clean && \
 *         update_build_info.php --status=pass --message="Good build"
 */

require_once '/home/engshare/devtools/arcanist/scripts/__init_script__.php';

const JENKINS_PROPERTY = 'facebook:jenkins_build_log';

/**
 * Configure the conduit call to phabricator
 * The 'svcscm' user is the user that runs the Jenkins builds as well
 * as has access to the git/svn repositories.
 */
function get_conduit() {

  $endpoint = 'https://phabricator.fb.com/api/';
  $conduit = new ConduitClient($endpoint);

  $user = 'svcscm';
  $cert = 'nqvznivfyrbi56s65vur26zucxfgto6wfqsqqp2ihwtybzurcrv53cxt7ikpthu26'.
          'dcrdgeg55nxaqu6ebagch5e2el752mygos6wtjcsf5b6mwvcq4vzjao4hyiknakmg'.
          'vf2evfazhivbnce6qih3wgn7r2rwg64i2ki5wayl6mvlmuaktu4meaqayerzdzifr'.
          'mejcabzfdjb6lq6d2mfzontnn2xndatmhfctyg5fick3cugiebmasvknnvrt';


  $response = $conduit->callMethodSynchronous(
    'conduit.connect',
    array(
      'client'            => 'Continuous Builder Client',
      'clientVersion'     => '1.0',
      'clientDescription' => 'Test updater for Jenkins',
      'user'              => $user,
      'certificate'       => $cert,
    ));

  return $conduit;
}

function get_diffid() {
  $git_branch = getenv('GIT_BRANCH');

  // Exit here if GIT_BRANCH isn't set.
  if (! $git_branch) {
    echo "Error: GIT_BRANCH was not set. Are you running this outside\n".
         "of a Jenkins build?\n";
    exit(1);
  }

  // The branch should look like refs/autobuilds/<digits>
  $diff_id = array_pop(explode("/", $git_branch));

  if (!preg_match('/\d+/', $diff_id)) {
    echo "Error: the diff_id ($diff_id) was not a valid diff.\n";
    exit(1);
  }
  return $diff_id;
}


// BUILD_TYPE, TEST_SET and PAGE_SIZE are set by the Jenkins environment
// only for individual builds, otherwise the label is "mysql_build".
function get_build_label() {
  $label = (getenv('BUILD_TYPE') ?: 'mysql')
            . '_' . (getenv('TEST_SET') ?: 'build');
  if((getenv('PAGE_SIZE') ?: '16') != '16') {
    $label .= ('_' . getenv('PAGE_SIZE'));
  }
  return $label;
}

function update_buildstate(&$conduit, $diff_id, $buildstate) {
  $job_name = get_build_label();
  $build_url = getenv('BUILD_URL');
  if (!$job_name || !$build_url) {
    exit(0);
  }
  if (!is_numeric($diff_id)) { // Skip for 'arc unit'
    return;
  }

  $tries = 360;
  $retry = TRUE;
  while($retry) {
    $retry = FALSE;
    try {
      $result = $conduit->callMethodSynchronous('differential.getdiff',
                                                array('diff_id' => $diff_id));
      $links = idx($result['properties'], JENKINS_PROPERTY, array());
      $new_link = array('label' => $job_name,
                        'url'   => $build_url,
                        'state' => $buildstate);
      $found = false;
      foreach ($links as &$link) {
        if (idx($link, 'label', '') == $job_name) {
          $found = true;
          $link = $new_link;
          break;
        }
      }
      if (!$found) {
        $links[] = $new_link;
      }
      $conduit->callMethodSynchronous('differential.setdiffproperty',
                                      array('diff_id' => $diff_id,
                                            'name'    => JENKINS_PROPERTY,
                                            'data'    => json_encode($links)));
    } catch(Exception $e) {
      echo "Caught (in conduit): " . $e->getMessage() . "\n";
      echo "Retrying in 10 seconds.\n";
      sleep(10);
      $tries--;
      if($tries <= 0) {
        echo "Error: Retries exhausted.\n";
        exit(1);
      }
      $conduit = get_conduit();
      $retry = TRUE;
    }
  }
}

// When the build fails to compile, just send a 'build failed' message
function update_buildstatus(&$conduit, $diff_id, $status, $message) {
  $build_set = get_build_label();

  if (!is_numeric($diff_id)) { // Skip for 'arc unit'
    return;
  }

  $tries = 360;
  $retry = TRUE;
  while($retry) {
    $retry = FALSE;
    try {
      $conduit->callMethodSynchronous(
        'differential.updateunitresults',
        array(
          'diff_id' => $diff_id,
          'file'    => 'mysql_build',
          'name'    => "{$build_set}",
          'result'  => $status,
          'message' => $message,
        ));
    } catch(Exception $e) {
      echo "Caught (in conduit): " . $e->getMessage() . "\n";
      echo "Retrying in 10 seconds.\n";
      sleep(10);
      $tries--;
      if($tries <= 0) {
        echo "Error: Retries exhausted.\n";
        exit(1);
      }
      $conduit = get_conduit();
      $retry = TRUE;
    }
  }
}

/**
 * Send results to phabricator.
 */
function update_unitresults(&$conduit, $diff_id, $results) {
  // possible results: 'pass', 'fail', 'skip', 'broken', 'skip', 'unsound'

  if (!is_numeric($diff_id)) { // Skip for 'arc unit'
    return;
  }

  $tries = 360;
  $retry = TRUE;
  while($retry) {
    $retry = FALSE;
    try {
      foreach ($results as $result) {
        $conduit->callMethodSynchronous(
          'differential.updateunitresults',
          array(
            'diff_id' => $diff_id,
            'file'    => $result['file'],
            'name'    => $result['name'],
            'result'  => $result['status'],
            'message' => $result['msg'],
          ));
        echo "Sent: " . $result['name'] . "\n";
      }
    } catch(Exception $e) {
      echo "Caught (in conduit): " . $e->getMessage() . "\n";
      echo "Retrying in 10 seconds.\n";
      sleep(10);
      $tries--;
      if($tries <= 0) {
        echo "Error: Retries exhausted.\n";
        exit(1);
      }
      $conduit = get_conduit();
      $retry = TRUE;
    }
  }
}

function read_piped_log($conduit, $diff_id, $all) {

  // The output we are looking for looks like the following:
  //   perfschema.hostcache_ipv6_addrinfo_bad_allow w11 [ fail ]

  $completed = 0;
  $failure_count =  0;
  $build_set = get_build_label();

  $pass_reg = "/^([\S]+(\s\'[\S]+\')?)\s+(w[0-9]+\s)?+\[\spass\s\].*/";
  $fail_reg = "/^([\S]+(\s\'[\S]+\')?)\s+(w[0-9]+\s)?+\[\sfail\s\].*/";

  $fp = fopen("php://stdin","r");
  while($line = fgets($fp)) {
    if (preg_match($fail_reg, $line, $matches)) {
      $failure_count++;
      $result = array(
                       'file'   => "{$matches[1]}",
                       'name'   => "{$build_set}: {$matches[1]}",
                       'status' => 'fail',
                       'msg'    => '', // What should we capture here?
                     );
      update_unitresults($conduit, $diff_id, array($result));
    }

    // If "--all" is used, get both fails and passes, otherwise just fails
    elseif ($all && preg_match($pass_reg, $line, $matches)) {
      $result = array(
                       'file'   => "{$matches[1]}",
                       'name'   => "{$build_set}: {$matches[1]}",
                       'status' => 'pass',
                       'msg'    => '', // What should we capture here?
                     );
      update_unitresults($conduit, $diff_id, array($result));
    }

    // Check for the tag at end of test output, to confirm completion.
    elseif (preg_match('/^Completed All Requested MySQL Testing/', $line)) {
      $completed = 1;
    }

    // This program is just a filter so keep sending output to stdout
    echo "$line";
  }
  fclose($fp);

  // Alert if test system didn't fully complete test run.
  if ($completed == 0) { return -1; }

  return $failure_count;
}

// Command line option specification
$specs = array(
  array(
    'name' => 'status',
    'param' => 'value',
  ),
  array(
    'name' => 'message',
    'param' => 'value',
  ),
  array(
    'name' => 'all',
  ),
);

$args = new PhutilArgumentParser($argv);
$args->parseFull($specs);

$diff_id = get_diffid();
$conduit = get_conduit();

if (!$diff_id || !$conduit) {
  exit(0);
}

// If we were given a status and message, update the diff and exit
if ($status = $args->getArg('status')) {
  update_buildstate($conduit, $diff_id, $status);
  $message = $args->getArg('message') ?: "";
  update_buildstatus($conduit, $diff_id, $status, $message);
  exit(0);
}

// Otherwise, assume we are filtering the output and updating the diff.
update_buildstate($conduit, $diff_id, '...');
update_buildstatus($conduit, $diff_id, 'postponed', 'Running Tests');

$test_failures = read_piped_log($conduit, $diff_id, $args->getArg('all'));

if ($test_failures < 0) { // Test system itself failed
  update_buildstatus($conduit, $diff_id, 'fail', 'Test System Failure');
  update_buildstate($conduit, $diff_id, 'fail');
  exit(1); // And fail the Jenkins job too.
} elseif ($test_failures > 0) { // At least one test failed
  update_buildstatus($conduit, $diff_id, 'fail', 'Test Failures');
  update_buildstate($conduit, $diff_id, 'fail');
  exit(1); // And fail the Jenkins job too.
} else { // All is well
  update_buildstatus($conduit, $diff_id, 'pass', 'No Test Failures');
  update_buildstate($conduit, $diff_id, 'pass');
}
