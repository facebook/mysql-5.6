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
  return (getenv('BUILD_TYPE') ?: 'mysql')
          . '_' . (getenv('TEST_SET') ?: 'build')
          . '_' . (getenv('PAGE_SIZE') ?: '16');
}

// When the build fails to compile, just send a 'build failed' message
function update_buildstatus(&$conduit, $diff_id, $status, $message) {
  $build_set = get_build_label();

  if (is_numeric($diff_id)) { // Update diff page ('arc diff')
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
        echo "Caught " . $e->getMessage() . " in conduit.\n";
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
  } else { // Parse output for console ('arc unit')
    echo "JENKINS|" . $status . "|" . $build_set . "\n";
  }
}

/**
 * Send results to phabricator.
 */
function update_unitresults(&$conduit, $diff_id, $results) {
  // possible results: 'pass', 'fail', 'skip', 'broken', 'skip', 'unsound'

  if (is_numeric($diff_id)) { // Update diff page ('arc diff')
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
        echo "Caught " . $e->getMessage() . " in conduit.\n";
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
  } else { // Parse output for console ('arc unit')
    foreach ($results as $result) {
      echo "JENKINS|" . $result['status'] . "|" . $result['name'] . "\n";
    }
  }
}

function read_piped_log($conduit, $diff_id, $all) {

  // The output we are looking for looks like the following:
  //   perfschema.hostcache_ipv6_addrinfo_bad_allow w11 [ fail ]

  $failure_count =  0;
  $build_set = get_build_label();

  // If "--all" is used, get both fails and passes, otherwise just fails
  $res = $all ? 'fail|pass' : 'fail';

  $test_reg = "/^([\S]+(\s\'[\S]+\')?)\s+(w[0-9]+\s)?+\[\s({$res})\s\].*/";

  $fp = fopen("php://stdin","r");
  while($line = fgets($fp)) {
    // This program is just a filter so keep sending output to stdout
    if (preg_match($test_reg, $line, $matches)) {
      $failure_count++;
      $result = array(
                       'file'   => "{$matches[1]}",
                       'name'   => "{$build_set}: {$matches[1]}",
                       'status' => "{$matches[4]}",
                       'msg'    => '', // What should we capture here?
                     );
      update_unitresults($conduit, $diff_id, array($result));
    }
    $line = preg_replace("/\AFinished:/", "SubFinished:", $line);
    echo "$line";
  }
  fclose($fp);
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

// If we were given a status and message, update the diff and exit
if ($status = $args->getArg('status')) {
  $message = $args->getArg('message') ?: "";
  update_buildstatus($conduit, $diff_id, $status, $message);
  exit(0);
}

// Otherwise, assume we are filtering the output and updating the diff.
read_piped_log($conduit, $diff_id, $args->getArg('all'));
