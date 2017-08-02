<?php
// Copyright 2004-present Facebook. All Rights Reserved.

final class FacebookMySQLArcanistLandcastleWorkflow extends ArcanistBaseWorkflow
{

  // We use GenerateLandcastleController via ArcanistInternGraphClient
  const ENDPOINT = 'sandcastle/land';

  private $revision;
  private $revisionID;

  public function getWorkflowName() {
    return 'landcastle';
  }

  public function getCommandSynopses() {
    return phutil_console_format(<<<EOTEXT
      **landcastle** [__options__] [__revision__]
EOTEXT
    );
  }

  public function getCommandHelp() {
    return phutil_console_format(<<<EOTEXT
          Supports: git
          Land a diff asynchronously using landcastle.
EOTEXT
    );
  }

  // We don't use the Repository API since we rely on arguments and Conduit
  // to get the latest diff information.
  public function requiresRepositoryAPI() {
    return false;
  }

  // We use Conduit to read diff information from Phabricator.
  public function requiresConduit() {
    return true;
  }

  public function requiresAuthentication() {
    return true;
  }

  public function getSupportedRevisionControlSystems() {
    // This command actually is revision control system agnositic. Since we rely
    // entirely on Phabricator to provide the diff, we don't need to access the
    // local repository. If you want to support checking the current branch for
    // changes, then you'll want to adjust this value accordingly for your repo.
    return array('git');
  }

  public function getOwner() {
    // Replace this and getOwnerUri() with your own team information.
    return 'MyRocks';
  }

  public function getOwnerUri() {
    return 'https://fb.facebook.com/groups/dbeng.oncall';
  }

  public function getArguments() {
    // We will consume all arguments as revision information. If you need to add
    // additional flags or arguments, you would do it here.
    return array(
      '*' => 'revision',
    );
  }

  public function run() {
    $console = PhutilConsole::getConsole();
    $console->writeOut(
      "You are using Landcastle.\n\n"
    );

    $this->readArguments();

    $this->checkAccepted();

    $console->writeOut("Landing **{$this->revision}**...\n");

    $this->generateLandcastle();

    $url = 'https://phabricator.intern.facebook.com/'.$this->revision;
    $pref_url = 'http://fburl.com/landpref';
    $console->writeOut(
      "\nYour commit is landing asynchronously, so it has *not* landed yet.\n".
      "You can follow the progress at __{$url}__\n".
      "To adjust your notification preferences visit __${pref_url}__\n".
      "To remove closed feature branches use **arc feature --cleanup**\n".
      "To get cracking on your next feature branch run **arc feature foo**\n"
    );
  }

  private function generateLandcastle() {
    $latest_diff = $this->getLatestDiff();

    $revisions = array(
      array(
        (int)$this->revisionID,
        (int)$latest_diff['id'],
      ),
    );

    $query_params = array(
      'source' => 'arc',
      'revisions' => json_encode($revisions),
    );

    $land_status = ArcanistInternGraphClient::call(
      self::ENDPOINT,
      $query_params,
      null
    );

    if (!idx($land_status, 'url')) {
      throw new Exception(
        'Got an unexpected response from landcastle: '.
        print_r($land_status, true)
      );
    }
  }

  private function readArguments() {
    // If you were needed to support landing from the local repository, you
    // might to want read a branch name from the arguments and use the
    // repository API to determine a revision or branch to land.

    $revisions = $this->getArgument('revision');

    if (empty($revisions)) {
      throw new ArcanistUsageException(
        'No revision provided.'
      );
    }

    if (count($revisions) > 1) {
      throw new ArcanistUsageException(
        'Landcastle workflow only supports a single revision.'
      );
    }

    if ($revisions[0][0] !== 'D') {
      throw new ArcanistUsageException(
        'Revision must start with "D", eg. D123456.'
      );
    }

    $this->revision = $revisions[0];
    $this->revisionID = str_replace('D', '', $this->revision);
  }

  private function getLatestDiff() {
    // This API gets the latest diff for a given revision.
    return $this->getConduit()->callMethodSynchronous(
      'differential.getdiff',
      array(
        'revision_id' => $this->revisionID,
      )
    );
  }

  private function checkAccepted() {
    // We need to use the query Conduit API to get revision status information.
    $revisions = $this->getConduit()->callMethodSynchronous(
      'differential.query',
      array(
        'ids' => array($this->revisionID),
      )
    );
    $status = $revisions[0]['status'];
    if ($status !== ArcanistDifferentialRevisionStatus::ACCEPTED) {
      throw new ArcanistUsageException(
        "Revision D$this->revisionID has not been accepted"
      );
    }
  }
}
