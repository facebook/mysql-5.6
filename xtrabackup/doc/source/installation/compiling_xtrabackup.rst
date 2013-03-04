===========================================
 Compiling and Installing from Source Code
===========================================

The source code is available from the *Launchpad* project `here <https://launchpad.net/percona-xtrabackup>`_. The easiest way to get the code is with :command:`bzr branch` of the desired release, such as the following: ::

  bzr branch lp:percona-xtrabackup/2.0

You should then have a directory named after the release you branched, such as ``percona-xtrabackup``.


Compiling on Linux
==================

Prerequisites
-------------

The following packages and tools must be installed to compile *Percona XtraBackup* from source. These might vary from system to system.

In Debian-based distributions, you need to: ::

  $ apt-get install debhelper autotools-dev libaio-dev wget automake \
    libtool bison libncurses-dev libz-dev cmake bzr

In ``RPM``-based distributions, you need to: ::

  $ yum install cmake gcc gcc-c++ libaio libaio-devel automake autoconf bzr \
    bison libtool ncurses-devel zlib-devel

Compiling with :command:`build.sh`
----------------------------------

Once you have all dependencies met, the compilation is straight-forward with the bundled :command:`build.sh` script in the :file:`utils/` directory of the distribution.

The script needs the codebase for which the building is targeted, you must provide it with one of the following values or aliases:

  ================== =========  ============================================
  Value              Alias      Server
  ================== =========  ============================================
  innodb51_builtin   5.1		build against built-in InnoDB in MySQL 5.1
  innodb51           plugin		build against InnoDB plugin in MySQL 5.1
  innodb55           5.5		build against InnoDB in MySQL 5.5
  xtradb51           xtradb     build against Percona Server with XtraDB 5.1
  xtradb55           xtradb55   build against Percona Server with XtraDB 5.5
  ================== =========  ============================================

Note that the script must be executed from the base directory of |Xtrabackup| sources, and that directory must contain the packages with the source code of the codebase selected. This may appear cumbersome, but if the variable ``AUTO_LOAD="yes"`` is set, the :command:`build.sh` script will download all the source code needed for the build.

At the base directory of the downloaded source code, if you execute ::

  $ AUTO_DOWNLOAD="yes" ./utils/build.sh xtradb

and you go for a coffee, at your return |XtraBackup| will be ready to be used. The |xtrabackup| binary will be located in the ``percona-xtrabackup/src`` subdirectory.

After this you'll need to copy |innobackupex| (in the root folder used to retrieve |XtraBackup|) and the corresponding xtrabackup binary (in the src folder) to some directory listed in the PATH environment variable, e.g. ``/usr/bin``.
