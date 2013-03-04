# This is a config file for testing combinations.pl's handling of various exit values.
# We avoid supplying invalid server options, as that will cause the server start operation to
# spend too much time before timing out.
#
# See test file TestScripts.pm for run details.

$combinations = [
    ['
        --queries=10 --duration=60 --threads=1 --seed=1
    '],
    [
        '--grammar=conf/examples/example.yy',                          # STATUS_OK (0).
        '--grammar=conf/examples/example.yy --sqltrace=InvalidValue',  # Invalid RQG option, should cause STATUS_ENVIRONMENT_FAILURE (110) with runall-new.pl.
        '--grammar=conf/examples/example.yy --reporter=CustomAlarm ',  # Custom reporter that returns STATUS_ALARM.
        '--grammar=conf/optimizer/optimizer_no_subquery.yy'            # Another STATUS_OK (0).
    ]
];
