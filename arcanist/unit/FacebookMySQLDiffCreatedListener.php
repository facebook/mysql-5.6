<?hh
// Copyright 2004-present Facebook. All Rights Reserved.

final class FacebookMySQLDiffCreatedListener extends PhutilEventListener {

  // File containing the implementation of the event handler that is shared
  // across our multiple repositories.
  const string
    MYSQL_DIFF_HANDLER_FILE = "../tools/sandcastle/MysqlDiffHandler.php";

  public function register(): void {
    $this->listen(ArcanistEventType::TYPE_DIFF_WASCREATED);
  }

  public function handleEvent(PhutilEvent $event): void {
    $workflow = $event->getValue('workflow');
    $working_copy = $workflow->getWorkingCopy();

    $full_diff_handler_path = $working_copy->getProjectPath(
      FacebookMySQLDiffCreatedListener::MYSQL_DIFF_HANDLER_FILE,
    );

    // If we have acccess to the shared handler, call it to start tests.
    if (Filesystem::pathExists($full_diff_handler_path)) {
      // Do the magic that allows us to call the handler shared by
      // multiple repositories.
      require_once ($full_diff_handler_path);
      $handler = new MysqlDiffHandler(
        $event,
        MysqlLegoConstants::MYSQL_GITHUB,
        './tools/',
      );
      $handler->handleEvent();
    } else {
      $console = PhutilConsole::getConsole();
      $console->writeOut(
        "Failed to detect Facebook-internal tools. Skipping launch of tests.\n",
      );
    }
  }
}
