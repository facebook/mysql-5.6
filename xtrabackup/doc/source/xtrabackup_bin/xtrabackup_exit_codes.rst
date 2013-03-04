=========================
 |xtrabackup| Exit Codes
=========================

The |xtrabackup| binary exits with the traditional success value of 0 after a backup when no error occurs. If an error occurs during the backup, the exit value is 1.

In certain cases, the exit value can be something other than 0 or 1, due to the command-line option code included from the |MySQL| libraries. An unknown command-line option, for example, will cause an exit code of 255.

When an error happens in the ``main()`` function of ``xtrabackup.c``, when |xtrabackup| is preparing to perform the backup, the exit code is -1. This is usually because of a missing or wrong command-line option, failure to open a file or directory that the user specified as a command-line option, or similar. This behavior is changed in |xtrabackup| 1.4 and later, so it always returns 0 or 1. (However, the |MySQL| libraries might still exit with a code of 255.)
