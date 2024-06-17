# Copyright (c) 2010, 2012, Oracle and/or its affiliates. All rights reserved. 
# Copyright (c) 2013, Monty Program Ab.
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
use POSIX ":sys_wait_h";

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
use constant MYSQLD_WINDOWS_PROCESS_EXITCODE => 22;
use constant MYSQLD_DEBUG_SERVER => 22;
use constant MYSQLD_SERVER_TYPE => 23;
use constant MYSQLD_VALGRIND_SUPPRESSION_FILE => 24;
use constant MYSQLD_TMPDIR => 25;
use constant MYSQLD_CONFIG_CONTENTS => 26;
use constant MYSQLD_CONFIG_FILE => 27;
use constant MYSQLD_USER => 28;

use constant MYSQLD_PID_FILE => "mysql.pid";
use constant MYSQLD_ERRORLOG_FILE => "mysql.err";
use constant MYSQLD_LOG_FILE => "mysql.log";
use constant MYSQLD_DEFAULT_PORT =>  19300;
use constant MYSQLD_DEFAULT_DATABASE => "test";
use constant MYSQLD_WINDOWS_PROCESS_STILLALIVE => 259;


sub new {
    my $class = shift;
    
    my $self = $class->SUPER::new({'basedir' => MYSQLD_BASEDIR,
                                   'sourcedir' => MYSQLD_SOURCEDIR,
                                   'vardir' => MYSQLD_VARDIR,
                                   'debug_server' => MYSQLD_DEBUG_SERVER,
                                   'port' => MYSQLD_PORT,
                                   'server_options' => MYSQLD_SERVER_OPTIONS,
                                   'start_dirty' => MYSQLD_START_DIRTY,
                                   'general_log' => MYSQLD_GENERAL_LOG,
                                   'valgrind' => MYSQLD_VALGRIND,
                                   'valgrind_options' => MYSQLD_VALGRIND_OPTIONS,
                                   'config' => MYSQLD_CONFIG_CONTENTS,
                                   'user' => MYSQLD_USER},@_);
    
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
    
    # Default tmpdir for server.
    $self->[MYSQLD_TMPDIR] = $self->vardir."/tmp";

    $self->[MYSQLD_DATADIR] = $self->[MYSQLD_VARDIR]."/data";
    
    # Use mysqld-debug server if --debug-server option used.
    if ($self->[MYSQLD_DEBUG_SERVER]) {
        # Catch excpetion, dont exit contine search for other mysqld if debug.
        eval{
            $self->[MYSQLD_MYSQLD] = $self->_find([$self->basedir],
                                                  osWindows()?["sql/Debug","sql/RelWithDebInfo","sql/Release","bin"]:["sql","libexec","bin","sbin"],
                                                  osWindows()?"mysqld-debug.exe":"mysqld-debug");
        };
        # If mysqld-debug server is not found, use mysqld server if built as debug.        
        if (!$self->[MYSQLD_MYSQLD]) {
            $self->[MYSQLD_MYSQLD] = $self->_find([$self->basedir],
                                                  osWindows()?["sql/Debug","sql/RelWithDebInfo","sql/Release","bin"]:["sql","libexec","bin","sbin"],
                                                  osWindows()?"mysqld.exe":"mysqld");     
            if ($self->[MYSQLD_MYSQLD] && $self->serverType($self->[MYSQLD_MYSQLD]) !~ /Debug/) {
                croak "--debug-server needs a mysqld debug server, the server found is $self->[MYSQLD_SERVER_TYPE]"; 
            }
        }
    }else {
        # If mysqld server is found use it.
        eval {
            $self->[MYSQLD_MYSQLD] = $self->_find([$self->basedir],
                                                  osWindows()?["sql/Debug","sql/RelWithDebInfo","sql/Release","bin"]:["sql","libexec","bin","sbin"],
                                                  osWindows()?"mysqld.exe":"mysqld");
        };
        # If mysqld server is not found, use mysqld-debug server.
        if (!$self->[MYSQLD_MYSQLD]) {
            $self->[MYSQLD_MYSQLD] = $self->_find([$self->basedir],
                                                  osWindows()?["sql/Debug","sql/RelWithDebInfo","sql/Release","bin"]:["sql","libexec","bin","sbin"],
                                                  osWindows()?"mysqld-debug.exe":"mysqld-debug");
        }
        
        $self->serverType($self->[MYSQLD_MYSQLD]);
    }

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
   
    ## Use valgrind suppression file available in mysql-test path. 
    if ($self->[MYSQLD_VALGRIND]) {
        $self->[MYSQLD_VALGRIND_SUPPRESSION_FILE] = $self->_find(defined $self->sourcedir?[$self->basedir,$self->sourcedir]:[$self->basedir],
                                                             osWindows()?["share/mysql-test","mysql-test"]:["share/mysql-test","mysql-test"],
                                                             "valgrind.supp")
    };
    
    foreach my $file ("mysql_system_tables.sql", 
                      "mysql_performance_tables.sql",
                      "mysql_system_tables_data.sql", 
                      "mysql_test_data_timezone.sql",
                      "fill_help_tables.sql") {
        my $script = 
             eval { $self->_find(defined $self->sourcedir?[$self->basedir,$self->sourcedir]:[$self->basedir],
                          ["scripts","share/mysql","share"], $file) };
        push(@{$self->[MYSQLD_BOOT_SQL]},$script) if $script;
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
                               "--tmpdir=".$self->tmpdir];    

    if ($self->[MYSQLD_START_DIRTY]) {
        say("Using existing data for MySQL " .$self->version ." at ".$self->datadir);
    } else {
        say("Creating MySQL " . $self->version . " database at ".$self->datadir);
        if ($self->createMysqlBase != DBSTATUS_OK) {
            croak("FATAL ERROR: Bootstrap failed, cannot proceed!");
        }
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

sub tmpdir {
    return $_[0]->[MYSQLD_TMPDIR];
}

sub port {
    my ($self) = @_;
    
    if (defined $self->[MYSQLD_PORT]) {
        return $self->[MYSQLD_PORT];
    } else {
        return MYSQLD_DEFAULT_PORT;
    }
}

sub user {
    return $_[0]->[MYSQLD_USER];
}

sub serverpid {
    return $_[0]->[MYSQLD_SERVERPID];
}

sub forkpid {
    return $_[0]->[MYSQLD_AUXPID];
}

sub socketfile {
    my ($self) = @_;
    my $socketFileName = $_[0]->vardir."/mysql.sock";
    if (length($socketFileName) >= 100) {
	$socketFileName = "/tmp/RQGmysql.".$self->port.".sock";
    }
    return $socketFileName;
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

sub setStartDirty {
    $_[0]->[MYSQLD_START_DIRTY] = $_[1];
}

sub valgrind_suppressionfile {
    return $_[0]->[MYSQLD_VALGRIND_SUPPRESSION_FILE] ;
}

#sub libmysqldir {
#    return $_[0]->[MYSQLD_LIBMYSQL];
#}

# Check the type of mysqld server.
sub serverType {
    my ($self, $mysqld) = @_;
    $self->[MYSQLD_SERVER_TYPE] = "Release";
    
    my $command="$mysqld --version";
    my $result=`$command 2>&1`;
    
    $self->[MYSQLD_SERVER_TYPE] = "Debug" if ($result =~ /debug/sig);
    return $self->[MYSQLD_SERVER_TYPE];
}

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
    
    ## Clean old db if any
    if (-d $self->vardir) {
        rmtree($self->vardir);
    }

    ## Create database directory structure
    mkpath($self->vardir);
    mkpath($self->tmpdir);
    mkpath($self->datadir);

    ## Prepare config file if needed
    if ($self->[MYSQLD_CONFIG_CONTENTS] and ref $self->[MYSQLD_CONFIG_CONTENTS] eq 'ARRAY' and scalar(@{$self->[MYSQLD_CONFIG_CONTENTS]})) {
        $self->[MYSQLD_CONFIG_FILE] = $self->vardir."/my.cnf";
        open(CONFIG,">$self->[MYSQLD_CONFIG_FILE]") || die "Could not open $self->[MYSQLD_CONFIG_FILE] for writing: $!\n";
        print CONFIG @{$self->[MYSQLD_CONFIG_CONTENTS]};
        close CONFIG;
    }

    my $defaults = ($self->[MYSQLD_CONFIG_FILE] ? "--defaults-file=$self->[MYSQLD_CONFIG_FILE]" : "--no-defaults");

    ## Create boot file

    my $boot = $self->vardir."/boot.sql";
    open BOOT,">$boot";
    print BOOT "CREATE DATABASE test;\n";

    ## Boot database

    my $boot_options = [$defaults];
    push @$boot_options, @{$self->[MYSQLD_STDOPTS]};

    if ($self->_olderThan(5,6,3)) {
        push(@$boot_options,"--loose-skip-innodb --default-storage-engine=MyISAM") ;
    } else {
        push(@$boot_options, @{$self->[MYSQLD_SERVER_OPTIONS]});
    }
    push @$boot_options, "--skip-log-bin";
    push @$boot_options, "--loose-innodb-encrypt-tables=OFF";
    push @$boot_options, "--loose-innodb-encrypt-log=OFF";

    my $command;

    if ($self->_olderThan(5,7,5)) {

       # Add the whole init db logic to the bootstrap script
       print BOOT "CREATE DATABASE mysql;\n";
       print BOOT "USE mysql;\n";
       foreach my $b (@{$self->[MYSQLD_BOOT_SQL]}) {
            open B,$b;
            while (<B>) { print BOOT $_;}
            close B;
        }

        push(@$boot_options,"--bootstrap") ;
        $command = $self->generateCommand($boot_options);
        if (osWindows()) {
            $command = "$command < \"$boot\"";
        } else {
            $command = "cat \"$boot\" | $command";
        }
    } else {
        push @$boot_options, "--initialize-insecure", "--init-file=$boot";
        $command = $self->generateCommand($boot_options);
    }

    ## Add last strokes to the boot/init file: don't want empty users, but want the test user instead
    print BOOT "USE mysql;\n";
    print BOOT "DELETE FROM user WHERE `User` = '';\n";
    if ($self->user ne 'root') {
        print BOOT "CREATE TABLE tmp_user AS SELECT * FROM user WHERE `User`='root' AND `Host`='localhost';\n";
        print BOOT "UPDATE tmp_user SET `User` = '". $self->user ."';\n";
        print BOOT "INSERT INTO user SELECT * FROM tmp_user;\n";
        print BOOT "DROP TABLE tmp_user;\n";
        print BOOT "CREATE TABLE tmp_proxies AS SELECT * FROM proxies_priv WHERE `User`='root' AND `Host`='localhost';\n";
        print BOOT "UPDATE tmp_proxies SET `User` = '". $self->user . "';\n";
        print BOOT "INSERT INTO proxies_priv SELECT * FROM tmp_proxies;\n";
        print BOOT "DROP TABLE tmp_proxies;\n";
    }
    close BOOT;

    say("Running bootstrap: $command (and feeding $boot to it)");
    system("$command > \"".$self->vardir."/boot.log\" 2>&1");
    return $?;
}

sub _reportError {
    say(Win32::FormatMessage(Win32::GetLastError()));
}

sub startServer {
    my ($self) = @_;

	my @defaults = ($self->[MYSQLD_CONFIG_FILE] ? ("--defaults-group-suffix=.runtime", "--defaults-file=$self->[MYSQLD_CONFIG_FILE]") : ("--no-defaults"));

    my ($v1,$v2,@rest) = $self->versionNumbers;
    my $v = $v1*1000+$v2;
    my $command = $self->generateCommand([@defaults],
                                         $self->[MYSQLD_STDOPTS],
                                         ["--core-file",
                                          "--max-allowed-packet=128Mb",	# Allow loading bigger blobs
                                          "--port=".$self->port,
                                          "--socket=".$self->socketfile,
                                          "--pid-file=".$self->pidfile],
                                         $self->_logOptions);
    if (defined $self->[MYSQLD_SERVER_OPTIONS]) {
        $command = $command." ".join(' ',@{$self->[MYSQLD_SERVER_OPTIONS]});
    }
    # If we don't remove the existing pidfile, 
    # the server will be considered started too early, and further flow can fail
    unlink($self->pidfile);
    
    my $errorlog = $self->vardir."/".MYSQLD_ERRORLOG_FILE;
    
    if (osWindows) {
        my $proc;
        my $exe = $self->binary;
        my $vardir = $self->[MYSQLD_VARDIR];
        $exe =~ s/\//\\/g;
        $vardir =~ s/\//\\/g;
        $self->printInfo();
        say("Starting MySQL ".$self->version.": $exe as $command on $vardir");
        Win32::Process::Create($proc,
                               $exe,
                               $command,
                               0,
                               NORMAL_PRIORITY_CLASS(),
                               ".") || croak _reportError();
        $self->[MYSQLD_WINDOWS_PROCESS]=$proc;
        $self->[MYSQLD_SERVERPID]=$proc->GetProcessID();
        # Gather the exit code and check if server is running.
        $proc->GetExitCode($self->[MYSQLD_WINDOWS_PROCESS_EXITCODE]);
        if ($self->[MYSQLD_WINDOWS_PROCESS_EXITCODE] == MYSQLD_WINDOWS_PROCESS_STILLALIVE) {
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
        }
    } else {
        if ($self->[MYSQLD_VALGRIND]) {
            my $val_opt ="";
            if (defined $self->[MYSQLD_VALGRIND_OPTIONS]) {
                $val_opt = join(' ',@{$self->[MYSQLD_VALGRIND_OPTIONS]});
            }
            $command = "valgrind --time-stamp=yes --leak-check=yes --suppressions=".$self->valgrind_suppressionfile." ".$val_opt." ".$command;
        }
        $self->printInfo;
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
            exec("$command >> \"$errorlog\"  2>&1") || croak("Could not start mysql server");
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

    # clean up when the server is not alive.
    unlink $self->socketfile if -e $self->socketfile;
    unlink $self->pidfile if -e $self->pidfile;
}

sub term {
    my ($self) = @_;
    
    if (osWindows()) {
        ### Not for windows
        say("Don't know how to do SIGTERM on Windows");
        $self->kill;
    } else {
        if (defined $self->serverpid) {
            kill TERM => $self->serverpid;
            my $waits = 0;
            while ($self->running && $waits < 100) {
                Time::HiRes::sleep(0.2);
                $waits++;
            }
            if ($waits >= 100) {
                say("Unable to terminate process ".$self->serverpid." Trying kill");
                $self->kill;
            } else {
                say("Terminated process ".$self->serverpid);
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

    # clean up when the server is not alive.
    unlink $self->socketfile if -e $self->socketfile;
    unlink $self->pidfile if -e $self->pidfile;
 
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
                             "--port=".$self->port.
                             " -uroot $database";
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
        ## Use dbh routine to ensure reconnect in case connection is
        ## stale (happens i.e. with mdl_stability/valgrind runs)
        my $dbh = $self->dbh();
        my $res;
        my $waits = 0;
        # Need to check if $dbh is defined, in case the server has crashed
        if (defined $dbh) {
            $res = $dbh->func('shutdown','127.0.0.1','root','admin');
            if ($res) {
                while ($self->running && $waits < 100) {
                    Time::HiRes::sleep(0.2);
                    $waits++;
                }
            } else {
                ## If shutdown fails, we want to know why:
                say("Shutdown failed due to ".$dbh->err.":".$dbh->errstr);
            }
        }
        if (!$res or $waits >= 100) {
            # Terminate process
            say("Server would not shut down properly. Terminate it");
            $self->term;
        } else {
            # clean up when server is not alive.
            unlink $self->socketfile if -e $self->socketfile;
            unlink $self->pidfile if -e $self->pidfile;
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
        ## Check if the child process is active.
        my $child_status = waitpid($self->serverpid,WNOHANG);
        return $child_status != -1;
    }
}

sub _find {
    my($self, $bases, $subdir, @names) = @_;
    
    foreach my $base (@$bases) {
        foreach my $s (@$subdir) {
        	foreach my $n (@names) {
                my $path  = $base."/".$s."/".$n;
                return $path if -f $path;
        	}
        }
    }
    my $paths = "";
    foreach my $base (@$bases) {
        $paths .= join(",",map {"'".$base."/".$_."'"} @$subdir).",";
    }
    my $names = join(" or ", @names );
    croak "Cannot find '$names' in $paths"; 
}

sub dsn {
    my ($self,$database) = @_;
    $database = "test" if not defined MYSQLD_DEFAULT_DATABASE;
    return "dbi:mysql:host=127.0.0.1:port=".
        $self->[MYSQLD_PORT].
        ":user=".
        $self->[MYSQLD_USER].
        ":database=".$database;
}

sub dbh {
    my ($self) = @_;
    if (defined $self->[MYSQLD_DBH]) {
        if (!$self->[MYSQLD_DBH]->ping) {
            say("Stale connection to ".$self->[MYSQLD_PORT].". Reconnecting");
            $self->[MYSQLD_DBH] = DBI->connect($self->dsn("mysql"),
                                               undef,
                                               undef,
                                               {PrintError => 0,
                                                RaiseError => 0,
                                                AutoCommit => 1});
        }
    } else {
        say("Connecting to ".$self->[MYSQLD_PORT]);
        $self->[MYSQLD_DBH] = DBI->connect($self->dsn("mysql"),
                                           undef,
                                           undef,
                                           {PrintError => 0,
                                            RaiseError => 0,
                                            AutoCommit => 1});
    }
    if(!defined $self->[MYSQLD_DBH]) {
        say("ERROR: (Re)connect to ".$self->[MYSQLD_PORT]." failed due to ".$DBI::err.": ".$DBI::errstr);
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
        my $conf = $self->_find([$self->basedir], 
                                ['scripts',
                                 'bin',
                                 'sbin'], 
                                'mysql_config.pl', 'mysql_config');
        ## This will not work if there is no perl installation,
        ## but without perl, RQG won't work either :-)
        my $ver = `perl $conf --version`;
        chop($ver);
        $self->[MYSQLD_VERSION] = $ver;
    }
    return $self->[MYSQLD_VERSION];
}

sub printInfo {
    my($self) = @_;

    say("MySQL Version: ". $self->version);
    say("Binary: ". $self->binary);
    say("Type: ". $self->serverType($self->binary));
    say("Datadir: ". $self->datadir);
    say("Tmpdir: ". $self->tmpdir);
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

# For _olderThan and _newerThan we will match according to InnoDB versions
# 10.0 to 5.6
# 10.1 to 5.6
# 10.2 to ? 

sub _olderThan {
    my ($self,$b1,$b2,$b3) = @_;
    
    my ($v1, $v2, $v3) = $self->versionNumbers;

    if ($v1 == 10 and $b1 == 5 and ($v2 == 0 or $v2 == 1)) { $v1 = 5; $v2 = 6 }
    elsif ($v1 == 5 and $b1 == 10 and ($b2 == 0 or $b2 == 1)) { $b1 = 5; $b2 = 6 }

    my $b = $b1*1000 + $b2 * 100 + $b3;
    my $v = $v1*1000 + $v2 * 100 + $v3;

    return $v < $b;
}

sub _newerThan {
    my ($self,$b1,$b2,$b3) = @_;

    my ($v1, $v2, $v3) = $self->versionNumbers;

    if ($v1 == 10 and $b1 == 5 and ($v2 == 0 or $v2 == 1)) { $v1 = 5; $v2 = 6 }
    elsif ($v1 == 5 and $b1 == 10 and ($b2 == 0 or $b2 == 1)) { $b1 = 5; $b2 = 6 }

    my $b = $b1*1000 + $b2 * 100 + $b3;
    my $v = $v1*1000 + $v2 * 100 + $v3;

    return $v > $b;
}


