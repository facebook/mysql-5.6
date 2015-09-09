<?php
// Copyright 2004-present Facebook. All Rights Reserved.

class FacebookMySQLArcanistConfiguration extends ArcanistConfiguration {

  public function getCustomArgumentsForCommand($command) {
    switch ($command) {
      case 'diff':
        return array(
          'big-test-queue' => array(
            'help' =>
              'Use the big async test queue instead.',
          ),
        );
    }

    return array();
  }

}
