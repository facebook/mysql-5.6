# Copyright (c) 2010 Oracle and/or its affiliates. All rights reserved. 
# Use is subject to license terms.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301
# USA

package DBServer::MySQL::MySQLd;

@ISA = qw(DBServer::DBServer);

use DBI;
use DBServer::DBServer;
use if osWindows(), Win32::Process;
use Time::HiRes;

use strict;

use Carp;
use Data::Dumper;
use File::Path qw(mkpath rmtree);

use constant MYSQLD_BASEDIR => 0;
use constant MYSQLD_VARDIR => 1;
use constant MYSQLD_DATADIR => 2;
use constant MYSQLD_PORT => 3;
use constant MYSQLD_MYSQLD => 4;
use constant MYSQLD_LIBMYSQL => 5;
use constant MYSQLD_BOOT_SQL => 6;
use constant MYSQLD_STDOPTS => 7;
use constant MYSQLD_MESSAGES => 8;
use constant MYSQLD_CHARSETS => 9;
use constant MYSQLD_SERVER_OPTIONS => 10;
use constant MYSQLD_AUXPID => 11;
use constant MYSQLD_SERVERPID => 12;
use constant MYSQLD_WINDOWS_PROCESS => 13;
use constant MYSQLD_DBH => 14;
use constant MYSQLD_START_DIRTY => 15;
use constant MYSQLD_VALGRIND => 16;
use constant MYSQLD_VALGRIND_OPTIONS => 17;
use constant MYSQLD_VERSION => 18;
use constant MYSQLD_DUMPER => 19;
use constant MYSQLD_SOURCEDIR => 20;
use constant MYSQLD_GENERAL_LOG => 21;

use constant MYSQLD_PID_FILE => "mysql.pid";
use constant MYSQLD_ERRORLOG_FILE => "mysql.err";
use constant MYSQLD_LOG_FILE => "mysql.log";
use constant MYSQLD_DEFAULT_PORT =>  19300;
use constant MYSQLD_DEFAULT_DATABASE => "test";



sub new {
    my $class = shift;
    
    my $self = $class->SUPER::new({'basedir' => MYSQLD_BASEDIR,
                                   'sourcedir' => MYSQLD_SOURCEDIR,
                                   'vardir' => MYSQLD_VARDIR,
                                   'port' => MYSQLD_PORT,
                                   'server_options' => MYSQLD_SERVER_OPTIONS,
                                   'start_dirty' => MYSQLD_START_DIRTY,
                                   'general_log' => MYSQLD_GENERAL_LOG,
                                   'valgrind' => MYSQLD_VALGRIND,
                                   'valgrind_options' => MYSQLD_VALGRIND_OPTIONS},@_);
    
    croak "No valgrind support on windows" if osWindows() and $self->[MYSQLD_VALGRIND];
    
    if (not defined $self->[MYSQLD_VARDIR]) {
        $self->[MYSQLD_VARDIR] = "mysql-test/var";
    }
    
    if (osWindows()) {
        ## Use unix-style path's since that's what Perl expects...
        $self->[MYSQLD_BASEDIR] =~ s/\\/\//g;
        $self->[MYSQLD_VARDIR] =~ s/\\/\//g;
        $self->[MYSQLD_DATADIR] =~ s/\\/\//g;
    }
    
    if (not $self->_absPath($self->vardir)) {
        $self->[MYSQLD_VARDIR] = $self->basedir."/".$self->vardir;
    }
    
    $self->[MYSQLD_DATADIR] = $self->[MYSQLD_VARDIR]."/data";
    
    $self->[MYSQLD_MYSQLD] = $self->_find([$self->basedir],
                                          osWindows()?["sql/Debug","sql/RelWithDebInfo","sql/Release","bin"]:["sql","libexec","bin","sbin"],
                                          osWindows()?"mysqld.exe":"mysqld");
    $self->[MYSQLD_BOOT_SQL] = [];

    $self->[MYSQLD_DUMPER] = $self->_find([$self->basedir],
                                          osWindows()?["client/Debug","client/RelWithDebInfo","client/Release","bin"]:["client","bin"],
                                          osWindows()?"mysqldump.exe":"mysqldump");


    ## Check for CMakestuff to get hold of source dir:

    if (not defined $self->sourcedir) {
        if (-e $self->basedir."/CMakeCache.txt") {
            open CACHE, $self->basedir."/CMakeCache.txt";
            while (<CACHE>){
                if (m/^MySQL_SOURCE_DIR:STATIC=(.*)$/) {
                    $self->[MYSQLD_SOURCEDIR] = $1;
                    say("Found source directory at ".$self->[MYSQLD_SOURCEDIR]);
                    last;
                }
            }
        }
    }
    
    foreach my $file ("mysql_system_tables.sql", 
                      "mysql_system_tables_data.sql", 
                      "mysql_test_data_timezone.sql",
                      "fill_help_tables.sql") {
        push(@{$self->[MYSQLD_BOOT_SQL]}, 
             $self->_find(defined $self->sourcedir?[$self->basedir,$self->sourcedir]:[$self->basedir],
                          ["scripts","share/mysql","share"], $file));
    }
    
    $self->[MYSQLD_MESSAGES] = 
       $self->_findDir(defined $self->sourcedir?[$self->basedir,$self->sourcedir]:[$self->basedir], 
                       ["sql/share","share/mysql","share"], "english/errmsg.sys");

    $self->[MYSQLD_CHARSETS] =
        $self->_findDir(defined $self->sourcedir?[$self->basedir,$self->sourcedir]:[$self->basedir], 
                        ["sql/share/charsets","share/mysql/charsets","share/charsets"], "Index.xml");
                         
    
    #$self->[MYSQLD_LIBMYSQL] = 
    #   $self->_findDir([$self->basedir], 
    #                   osWindows()?["libmysql/Debug","libmysql/RelWithDebInfo","libmysql/Release","lib","lib/debug","lib/opt","bin"]:["libmysql","libmysql/.libs","lib/mysql","lib"], 
    #                   osWindows()?"libmysql.dll":osMac()?"libmysqlclient.dylib":"libmysqlclient.so");
    
    $self->[MYSQLD_STDOPTS] = ["--basedir=".$self->basedir,
                               "--datadir=".$self->datadir,
                               $self->_messages,
                               "--character-sets-dir=".$self->[MYSQLD_CHARSETS],
                               "--default-storage-engine=myisam",
                               "--log-warnings=0"];    

    if ($self->[MYSQLD_START_DIRTY]) {
        say("Using existing data for MySQL " .$self->version ." at ".$self->datadir)
    } else {
        say("Creating MySQL " . $self->version . " database at ".$self->datadir);
        $self->createMysqlBase;
    }

    return $self;
}

sub basedir {
    return $_[0]->[MYSQLD_BASEDIR];
}

sub sourcedir {
    return $_[0]->[MYSQLD_SOURCEDIR];
}

sub datadir {
    return $_[0]->[MYSQLD_DATADIR];
}

sub vardir {
    return $_[0]->[MYSQLD_VARDIR];
}

sub port {
    my ($self) = @_;
    
    if (defined $self->[MYSQLD_PORT]) {
        return $self->[MYSQLD_PORT];
    } else {
        return MYSQLD_DEFAULT_PORT;
    }
}

sub serverpid {
    return $_[0]->[MYSQLD_SERVERPID];
}

sub forkpid {
    return $_[0]->[MYSQLD_AUXPID];
}

sub socketfile {
    my ($self) = @_;

    return "/tmp/RQGmysql.".$self->port.".sock";
}

sub pidfile {
    return $_[0]->vardir."/".MYSQLD_PID_FILE;
}

sub logfile {
    return $_[0]->vardir."/".MYSQLD_LOG_FILE;
}

sub errorlog {
    return $_[0]->vardir."/".MYSQLD_ERRORLOG_FILE;
}

#sub libmysqldir {
#    return $_[0]->[MYSQLD_LIBMYSQL];
#}


sub generateCommand {
    my ($self, @opts) = @_;

    my $command = '"'.$self->binary.'"';
    foreach my $opt (@opts) {
        $command .= ' '.join(' ',map{'"'.$_.'"'} @$opt);
    }
    $command =~ s/\//\\/g if osWindows();
    return $command;
}

sub addServerOptions {
    my ($self,$opts) = @_;

    push(@{$self->[MYSQLD_SERVER_OPTIONS]}, @$opts);
}

sub createMysqlBase  {
    my ($self) = @_;
    
    ## 1. Clean old db if any
    if (-d $self->vardir) {
        rmtree($self->vardir);
    }

    ## 2. Create database directory structure
    mkpath($self->vardir);
    mkpath($self->datadir);
    mkpath($self->datadir."/mysql");
    mkpath($self->datadir."/test");
    
    ## 3. Create boot file
    my $boot = $self->vardir."/boot.sql";
    open BOOT,">$boot";
    
    ## Set curren database
    print BOOT  "use mysql;\n";
    foreach my $b (@{$self->[MYSQLD_BOOT_SQL]}) {
        open B,$b;
        while (<B>) { print BOOT $_;}
        close B;
    }
    ## Don't want empty users
    print BOOT "DELETE FROM user WHERE `User` = '';\n";
    close BOOT;
    
    ## 4. Boot database
    if (osWindows()) {
        my $command = $self->generateCommand(["--no-defaults","--bootstrap"],
                                             $self->[MYSQLD_STDOPTS]);
    
        my $bootlog = $self->vardir."/boot.log";
        system("$command < \"$boot\" > \"$bootlog\"");
    } else {
        my $boot_options = ["--no-defaults","--bootstrap"];
        push(@$boot_options,"--loose-skip-innodb") if $self->_olderThan(5,6,3);
        my $command = $self->generateCommand($boot_options,
                                             $self->[MYSQLD_STDOPTS]);
        my $bootlog = $self->vardir."/boot.log";
        system("cat \"$boot\" | $command > \"$bootlog\"  2>&1 ");
    }
}

sub _reportError {
    say(Win32::FormatMessage(Win32::GetLastError()));
}

sub startServer {
    my ($self) = @_;

    my ($v1,$v2,@rest) = $self->versionNumbers;
    my $v = $v1*1000+$v2;
    my $command = $self->generateCommand(["--no-defaults"],
                                         $self->[MYSQLD_STDOPTS],
                                         ["--core-file",
                                          #"--skip-ndbcluster",
                                          #"--skip-grant",
                                          "--loose-new",
                                          "--relay-log=slave-relay-bin",
                                          "--loose-innodb",
                                          "--max-allowed-packet=16Mb",	# Allow loading bigger blobs
                                          "--loose-innodb-status-file=1",
                                          "--master-retry-count=65535",
                                          "--port=".$self->port,
                                          "--socket=".$self->socketfile,
                                          "--pid-file=".$self->pidfile],
                                         $self->_logOptions);
    if (defined $self->[MYSQLD_SERVER_OPTIONS]) {
        $command = $command." ".join(' ',@{$self->[MYSQLD_SERVER_OPTIONS]});
    }
    
    my $errorlog = $self->vardir."/".MYSQLD_ERRORLOG_FILE;
    
    if (osWindows) {
        my $proc;
        my $exe = $self->binary;
        my $vardir = $self->[MYSQLD_VARDIR];
        $exe =~ s/\//\\/g;
        $vardir =~ s/\//\\/g;
        say("Starting MySQL ".$self->version.": $exe as $command on $vardir");
        Win32::Process::Create($proc,
                               $exe,
                               $command,
                               0,
                               NORMAL_PRIORITY_CLASS(),
                               ".") || croak _reportError();	
        $self->[MYSQLD_WINDOWS_PROCESS]=$proc;
        $self->[MYSQLD_SERVERPID]=$proc->GetProcessID();
    } else {
        if ($self->[MYSQLD_VALGRIND]) {
            my $val_opt ="";
            if (defined $self->[MYSQLD_VALGRIND_OPTIONS]) {
                $val_opt = join(' ',@{$self->[MYSQLD_VALGRIND_OPTIONS]});
            }
            $command = "valgrind --time-stamp=yes ".$val_opt." ".$command;
        }
        say("Starting MySQL ".$self->version.": $command");
        $self->[MYSQLD_AUXPID] = fork();
        if ($self->[MYSQLD_AUXPID]) {
            ## Wait for the pid file to have been created
            my $wait_time = 0.2;
            my $waits = 0;
            while (!-f $self->pidfile && $waits < 600) {
                Time::HiRes::sleep($wait_time);
                $waits++;
            }
            if (!-f $self->pidfile) {
                sayFile($self->errorlog);
                croak("Could not start mysql server, waited ".($waits*$wait_time)." seconds for pid file");
            }
            my $pidfile = $self->pidfile;
            my $pid = `cat \"$pidfile\"`;
            $pid =~ m/([0-9]+)/;
            $self->[MYSQLD_SERVERPID] = int($1);
        } else {
            exec("$command > \"$errorlog\"  2>&1") || croak("Could not start mysql server");
        }
    }
    
    return $self->dbh ? DBSTATUS_OK : DBSTATUS_FAILURE;
}

sub kill {
    my ($self) = @_;
    
    if (osWindows()) {
        if (defined $self->[MYSQLD_WINDOWS_PROCESS]) {
            $self->[MYSQLD_WINDOWS_PROCESS]->Kill(0);
            say("Killed process ".$self->[MYSQLD_WINDOWS_PROCESS]->GetProcessID());
        }
    } else {
        if (defined $self->serverpid) {
            kill KILL => $self->serverpid;
            my $waits = 0;
            while ($self->running && $waits < 100) {
                Time::HiRes::sleep(0.2);
                $waits++;
            }
            if ($waits >= 100) {
                croak("Unable to kill process ".$self->serverpid);
            } else {
                say("Killed process ".$self->serverpid);
            }
        }
    }
    if (-e $self->socketfile) {
        unlink $self->socketfile;
    }
}

sub crash {
    my ($self) = @_;
    
    if (osWindows()) {
        ## How do i do this?????
        $self->kill; ## Temporary
    } else {
        if (defined $self->serverpid) {
            kill SEGV => $self->serverpid;
            say("Crashed process ".$self->serverpid);
        }
    }
    if (-e $self->socketfile) {
        unlink $self->socketfile;
    }
}

sub corefile {
    my ($self) = @_;

    ## Unix variant
    return $self->datadir."/core.".$self->serverpid;
}

sub dumper {
    return $_[0]->[MYSQLD_DUMPER];
}

sub dumpdb {
    my ($self,$database, $file) = @_;
    say("Dumping MySQL server ".$self->version." on port ".$self->port);
    my $dump_command = '"'.$self->dumper.
                             "\" --hex-blob --skip-triggers --compact ".
                             "--order-by-primary --skip-extended-insert ".
                             "--no-create-info --host=127.0.0.1 ".
                             "--port=".$self->port;
    # --no-tablespaces option was introduced in version 5.1.14.
    if ($self->_newerThan(5,1,13)) {
        $dump_command = $dump_command . " --no-tablespaces";
    }
    my $dump_result = system("$dump_command | sort > $file");
    return $dump_result;
}

sub binary {
    return $_[0]->[MYSQLD_MYSQLD];
}

sub stopServer {
    my ($self) = @_;
    
    if (defined $self->[MYSQLD_DBH]) {
        say("Stopping server on port ".$self->port);
        my $r = $self->[MYSQLD_DBH]->func('shutdown','127.0.0.1','root','admin');
        my $waits = 0;
        if ($r) {
            while ($self->running && $waits < 100) {
                Time::HiRes::sleep(0.2);
                $waits++;
            }
        }
        if (!$r or $waits >= 100) {
            say("Server would not shut down properly");
            $self->kill;
        } else {
            unlink $self->socketfile;
        }
    } else {
        $self->kill;
    }
}

sub running {
    my($self) = @_;
    if (osWindows()) {
        ## Need better solution fir windows. This is actually the old
        ## non-working solution for unix....
        return -f $self->pidfile;
    } else {
        return kill 0, $self->serverpid;
    }
}

sub _find {
    my($self, $bases, $subdir, $name) = @_;
    
    foreach my $base (@$bases) {
        foreach my $s (@$subdir) {
            my $path  = $base."/".$s."/".$name;
            return $path if -f $path;
        }
    }
    my $paths = "";
    foreach my $base (@$bases) {
        $paths .= join(",",map {"'".$base."/".$_."'"} @$subdir).",";
    }
    croak "Cannot find '$name' in $paths"; 
}

sub dsn {
    my ($self,$database) = @_;
    $database = "test" if not defined MYSQLD_DEFAULT_DATABASE;
    return "dbi:mysql:host=127.0.0.1:port=".
        $self->[MYSQLD_PORT].
        ":user=root:database=".$database;
}

sub dbh {
    my ($self) = @_;
    if (defined $self->[MYSQLD_DBH]) {
        if (!$self->[MYSQLD_DBH]->ping) {
            $self->[MYSQLD_DBH] = DBI->connect($self->dsn("mysql"),
                                               undef,
                                               undef,
                                               {PrintError => 1,
                                                RaiseError => 0,
                                                AutoCommit => 1});
        }
    } else {
        $self->[MYSQLD_DBH] = DBI->connect($self->dsn("mysql"),
                                           undef,
                                           undef,
                                           {PrintError => 1,
                                            RaiseError => 0,
                                            AutoCommit => 1});
    }
    return $self->[MYSQLD_DBH];
}

sub _findDir {
    my($self, $bases, $subdir, $name) = @_;
    
    foreach my $base (@$bases) {
        foreach my $s (@$subdir) {
            my $path  = $base."/".$s."/".$name;
            return $base."/".$s if -f $path;
        }
    }
    my $paths = "";
    foreach my $base (@$bases) {
        $paths .= join(",",map {"'".$base."/".$_."'"} @$subdir).",";
    }
    croak "Cannot find '$name' in $paths";
}

sub _absPath {
    my ($self, $path) = @_;
    
    if (osWindows()) {
        return 
            $path =~ m/^[A-Z]:[\/\\]/i;
    } else {
        return $path =~ m/^\//;
    }
}

sub version {
    my($self) = @_;

    if (not defined $self->[MYSQLD_VERSION]) {
        my $ver;
        if (osWindows) {
            my $conf = $self->_find([$self->basedir], 
                                    ['scripts',
                                     'bin',
                                     'sbin'], 
                                    'mysql_config.pl');
            ## This will not work if there is no perl installation,
            ## but without perl, RQG won't work either :-)
            $ver = `perl $conf --version`;
        } else {
            my $conf = $self->_find([$self->basedir], 
                                    ['scripts',
                                     'bin',
                                     'sbin'], 
                                    'mysql_config');
            
            $ver = `sh "$conf" --version`;
        }
        chop($ver);
        $self->[MYSQLD_VERSION] = $ver;
    }
    return $self->[MYSQLD_VERSION];
}

sub printInfo {
    my($self) = @_;

    say("MySQL ". $self->version);
    say("Binary: ". $self->binary);
    say("Datadir: ". $self->datadir);
    say("Corefile: " . $self->corefile);
}

sub versionNumbers {
    my($self) = @_;

    $self->version =~ m/([0-9]+)\.([0-9]+)\.([0-9]+)/;

    return (int($1),int($2),int($3));
}

#############  Version specific stuff

sub _messages {
    my ($self) = @_;

    if ($self->_olderThan(5,5,0)) {
        return "--language=".$self->[MYSQLD_MESSAGES]."/english";
    } else {
        return "--lc-messages-dir=".$self->[MYSQLD_MESSAGES];
    }
}

sub _logOptions {
    my ($self) = @_;

    if ($self->_olderThan(5,1,29)) {
        return ["--log=".$self->logfile]; 
    } else {
        if ($self->[MYSQLD_GENERAL_LOG]) {
            return ["--general-log", "--general-log-file=".$self->logfile]; 
        } else {
            return ["--general-log-file=".$self->logfile];
        }
    }
}

sub _olderThan {
    my ($self,$b1,$b2,$b3) = @_;
    
    my ($v1, $v2, $v3) = $self->versionNumbers;

    my $b = $b1*1000 + $b2 * 100 + $b3;
    my $v = $v1*1000 + $v2 * 100 + $v3;

    return $v < $b;
}

sub _newerThan {
    my ($self,$b1,$b2,$b3) = @_;

    my ($v1, $v2, $v3) = $self->versionNumbers;

    my $b = $b1*1000 + $b2 * 100 + $b3;
    my $v = $v1*1000 + $v2 * 100 + $v3;

    return $v > $b;
}


