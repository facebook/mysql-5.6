use strict;
use Data::Dumper;
use DBI;

my @php_files = @ARGV;
my %queries;

foreach my $php_file (@php_files) {
	open (FILE, $php_file) or die $!;
	read (FILE, my $php_code, -s $php_file) or die $!;
	close FILE;

	my @file_selects = $php_code =~ m{(SELECT.*?)'}sog;
	foreach my $file_select (@file_selects) {
		$file_select =~ s{\{db_prefix\}}{smf_}sgio;
		my ($trial_select , $grammar_select) = ($file_select , $file_select);

		$trial_select =~ s{(\{.*?\})}{1}sgio;

		$grammar_select =~ s{\{([a-z_]*?):[a-z_]*?\}}{_\1}sgio;
		$grammar_select =~ s{\{STEP_LOW\}}{_step_low}sgio;
		$grammar_select =~ s{\{STEP_HIGH\}}{_step_high}sgio;

		push @{$queries{'select'}} , [ $trial_select , $grammar_select ];
	}

	my @file_updates = $php_code =~  m{(UPDATE.*?)'}sog;
	foreach my $file_update (@file_updates) {
		$file_update =~ s{\{db_prefix\}}{smf_}sgio;
		my ($trial_update , $grammar_update) = ($file_update, $file_update);

		$trial_update =~ s{(\{.*?\})}{1}sgio;
		$grammar_update =~ s{\{([a-z_]*?):[a-z_]*?\}}{_\1}sgio;
		push @{$queries{'update'}} , [ $trial_update , $grammar_update ];
	}

	my @file_deletes = $php_code =~  m{(DELETE.*?)'}sog;
	foreach my $file_delete (@file_deletes) {
		$file_delete =~ s{\{db_prefix\}}{smf_}sgio;
		my ($trial_delete , $grammar_delete) = ($file_delete, $file_delete);

		$trial_delete =~ s{(\{.*?\})}{1}sgio;
		$grammar_delete =~ s{\{([a-z_]*?):[a-z_]*?\}}{_\1}sgio;
		push @{$queries{'delete'}} , [ $trial_delete , $grammar_delete ];
	}

	my @file_insertsphp = $php_code =~ m{'db_insert']\((.*?)\);}sgio;
	my @file_inserts;
	
	foreach my $file_insertphp (@file_insertsphp) {
		$file_insertphp =~ s{[\r\n]}{}sgio;
		my ($insert_replace, $table) = $file_insertphp =~ m{'(.*?)'.*?'(.*?)'}sio;

		$insert_replace = 'insert '.$insert_replace if $insert_replace !~ m{^(insert|replace)}sgio;

		$table =~ s{\{db_prefix\}}{smf_}sgio;

		die $file_insertphp if $table eq '';
		my ($array) = $file_insertphp =~ m{array\((.*?)\)}sio;
		my @array_elements = split (',', $array);

		my @col_names;
		my @col_values;
		my @col_types;
		foreach my $array_element (@array_elements) {
			next if $array_element =~ m{^\s+$}sgio;
			my ($col_name, $col_type) = $array_element =~ m{'(.*?)'\s+=>\s+'(.*?)'}sio;
			my $col_value;
			$col_type =~ s{\-}{_}sgio;
			$col_type = "_".$col_type;
			if (defined $col_name) {
				$col_type eq 'unknown_type' if $col_type eq '';
				push @col_names, $col_name;
				push @col_values, 1;
				push @col_types, $col_type;	
			}
		}

		if ($#col_names > -1) {
			push @{$queries{'insert_replace'}} , [
				 uc($insert_replace)." INTO $table (".join(',', @col_names).") VALUES (".join(',', @col_values).")",
				 uc($insert_replace)." INTO $table (".join(',', map {'`'.$_.'`' }@col_names).") VALUES (".join(',', @col_types).")"
			]
		}
	}
}

my $dbh = DBI->connect('dbi:mysql:database=smf2:user=root', undef , undef , { PrintError => 1});

my %errors;
print '
	query:
		insert_replace | insert_replace | insert_replace | insert_replace | insert_replace | insert_replace |
		update | update | update | update | update |
		delete |
		select | select | select ;

	_int: _mediumint_unsigned | _tinyint_unsigned | _digit ;
	_string: _english | _varchar(1) | _varchar(2) | _varchar(255) | REPEAT ( _varchar(2) , _tinyint_unsigned );
	_string_16: _english | _varchar(1) | _varchar(2) | _varchar(16) |  REPEAT ( _varchar(2) , _digit );
	_string_40: _english | _varchar(1) | _varchar(2) | _varchar(40) |  REPEAT ( _varchar(2) , _digit );
	_string_255: _english | _varchar(1) | _varchar(2) | _varchar(255) | REPEAT ( _varchar(2) , _tinyint_unsigned );
	_string_65534: _english | _varchar(1) | _varchar(2) | _varchar(255) | REPEAT ( _varchar(2) , _smallint_unsigned );
	_array_int: _int | _array_int , _int ;
	_array_string: _string | _string , _string ;
	_raw: _digit ;
	_step_low: _digit | _tinyint_unsigned ;
	_step_high: _tinyint_unsigned | _smallint_unsigned ;
';

foreach my $query_type ('select', 'insert_replace','update','delete') {
	my $type_queries = $queries{$query_type};
	my @type_rules;
	foreach my $type_query (@$type_queries) {
		my ($trial_query, $grammar_rule) = ($type_query->[0], $type_query->[1]);
		my $sth = $dbh->prepare($trial_query);
		$sth->execute();
		$errors{$sth->err()}++;

		if (($sth->err() == 0) || ($sth->err() == 1062)) {
			$grammar_rule =~ s{(data|next_time|time_offset|time_regularity|time_unit|time_updated|time_removed)}{`$1`}sgio;
			$grammar_rule =~ s{``}{`}sgio;
			push @type_rules, $grammar_rule;
		}
	}
	print "\n\n$query_type:\n".join("|\n\t", @type_rules).";\n\n";
}

print STDERR Dumper \%errors;
exit;
