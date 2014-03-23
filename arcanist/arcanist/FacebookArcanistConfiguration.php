<?php
// Copyright 2004-present Facebook. All Rights Reserved.

class FacebookArcanistConfiguration extends ArcanistConfiguration {

  public function getCustomArgumentsForCommand($command) {
    switch ($command) {
      case 'unit':
      case 'diff':
        return array(
          'run-tests' => array(
            'help' =>
              'Run unittests for all projects.',
            'passthru' => array(
              'unit' => true,
              'diff' => true,
            ),
          ),
        );
    }
    return array();
  }

}
