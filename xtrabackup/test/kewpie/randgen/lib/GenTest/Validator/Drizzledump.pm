# Copyright (C) 2010-2011 Patrick Crews. All rights reserved.
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

package GenTest::Validator::Drizzledump;

require Exporter;
@ISA = qw(GenTest GenTest::Validator);

use strict;

use Data::Dumper;
use GenTest;
use GenTest::Constants;
use GenTest::Result;
use GenTest::Validator;

sub validate {
	my ($validator, $executors, $results) = @_;
        my $fail_count ;
        my $total_count ;
        my $query_value = $results->[0]->[0] ;
        if ($query_value eq ' SELECT 1')
        {
          # do some setup and whatnot
          my @files;
	  my $port = '9306';

	  my $database = 'drizzledump_db' ;
          my $restored_database = $database."_restore" ;
          my @basedir = $executors->[0]->dbh()->selectrow_array('SELECT @@basedir') ;
          # little kludge to get the proper basedir if drizzle was started via test-run.pl
          # such a situation sets basedir to the drizzle/tests directory and can
          # muck up efforts to get to the client directory
          my @basedir_split = split(/\//, @basedir->[0]) ;
          if (@basedir_split[-1] eq 'tests')
          {
            pop(@basedir_split); 
            @basedir = join('/',@basedir_split);
          }
       
          my $drizzledump = @basedir->[0].'/client/drizzledump' ;
          my $drizzle_client = @basedir->[0].'/client/drizzle' ;

        
          # dump our database under test
	  my $drizzledump_file = tmpdir()."/dump_".$$."_".$port.".sql";
          
          if (rqg_debug()) 
          {
            say("drizzledump file is:  $drizzledump_file");
          }

	  my $drizzledump_result = system("$drizzledump --compact --order-by-primary --host=127.0.0.1 --port=$port --user=root $database  > $drizzledump_file") ;
	  return STATUS_UNKNOWN_ERROR if $drizzledump_result > 0 ;

          # restore test database from dumpfile
          if (rqg_debug()) 
          {
            say("Restoring from dumpfile...");
          }
          my $drizzle_restore_result = system("$drizzle_client --host=127.0.0.1 --port=$port --user=root $restored_database <  $drizzledump_file") ;
          return STATUS_UNKNOWN_ERROR if $drizzle_restore_result > 0 ;

          # compare original + restored databases
          # 1) We get the list of columns for the original table (use original as it is the standard)
          # 2) Use said column list in the comparison query (will report on any rows / values not in both tables)  
          # 3) Check the rows returned

          my $get_table_names =  " SELECT DISTINCT(table_name) ".
                                 " FROM data_dictionary.tables ". 
                                 " WHERE table_schema = '".$database."'" ;

          # Here, we get the name of the single table in the test db
          # Need to change the perl to better deal with a list of tables (more than 1)
          my $table_names = $executors->[0]->dbh()->selectcol_arrayref($get_table_names) ;

          foreach(@$table_names)
          { 
            $total_count += 1 ;
            my $get_table_columns = " SELECT column_name FROM data_dictionary.tables INNER JOIN ".
                                    " DATA_DICTIONARY.columns USING (table_schema, table_name) ".
                                    " WHERE table_schema = '".$database."'". 
                                    " AND table_name = '".$_."'"   ;


            my $table_columns = $executors->[0]->dbh()->selectcol_arrayref($get_table_columns) ; 
            my $table_column_list = join(' , ' , @$table_columns) ;
            
            if (rqg_debug()) 
            {
              say("Comparing original and restored tables for table : $database . $_" );
            }
            my $compare_orig_and_restored =     "SELECT MIN(TableName) as TableName, $table_column_list ".
                                 " FROM ".
                                 " ( ".
                                 "   SELECT 'Table A' as TableName, $table_column_list ".
                                 " FROM $database . $_ ".
                                 " UNION ALL ".
                                 " SELECT 'Table B' as TableName, $table_column_list ".
                                 " FROM $restored_database . $_ ".
                                 " ) tmp ".
                                 " GROUP BY $table_column_list ".
                                 " HAVING COUNT(*) = 1 ORDER BY `pk` " ;

            # say("$compare_orig_and_restored");

            my $diff_result = $executors->[0]->dbh()->selectall_arrayref($compare_orig_and_restored) ;
            if ($diff_result->[0] != undef)
            {
               say("Differences between the two databases were found after dump/restore from file ".$drizzledump_file );
               say("Comparison query: ".$compare_orig_and_restored ) ;
               say("Returned:  ".$diff_result) ; 
               $fail_count += 1;
            } 

          }

	    if ($fail_count ) {
                say("$fail_count /  $total_count test tables failed validation") ;
		return STATUS_DATABASE_CORRUPTION ;
	    } else {
                # cleanup our dumpfile - we leave it only if we found a mismatch
		unlink($drizzledump_file);
                say("All tests passed - tested $total_count tables");
		return STATUS_OK;
	   }


  }

return STATUS_OK ;
}


1;
