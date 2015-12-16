# Copyright (c) 2010-2011 Testtools authors. See LICENSE for details.

"""Tests for the distutils test command logic."""

from distutils.dist import Distribution

from testtools.helpers import try_import, try_imports
fixtures = try_import('fixtures')
StringIO = try_imports(['StringIO.StringIO', 'io.StringIO'])

import testtools
from testtools import TestCase
from testtools.distutilscmd import TestCommand


if fixtures:
    class SampleTestFixture(fixtures.Fixture):
        """Creates testtools.runexample temporarily."""

        def __init__(self):
            self.package = fixtures.PythonPackage(
            'runexample', [('__init__.py', """
from testtools import TestCase

class TestFoo(TestCase):
    def test_bar(self):
        pass
    def test_quux(self):
        pass
def test_suite():
    from unittest import TestLoader
    return TestLoader().loadTestsFromName(__name__)
""")])

        def setUp(self):
            super(SampleTestFixture, self).setUp()
            self.useFixture(self.package)
            testtools.__path__.append(self.package.base)
            self.addCleanup(testtools.__path__.remove, self.package.base)


class TestCommandTest(TestCase):

    def setUp(self):
        super(TestCommandTest, self).setUp()
        if fixtures is None:
            self.skipTest("Need fixtures")

    def test_test_module(self):
        self.useFixture(SampleTestFixture())
        stream = StringIO()
        dist = Distribution()
        dist.script_name = 'setup.py'
        dist.script_args = ['test']
        dist.cmdclass = {'test': TestCommand}
        dist.command_options = {
            'test': {'test_module': ('command line', 'testtools.runexample')}}
        cmd = dist.reinitialize_command('test')
        cmd.runner.stdout = stream
        dist.run_command('test')
        self.assertEqual("""Tests running...
Ran 2 tests in 0.000s

OK
""", stream.getvalue())

    def test_test_suite(self):
        self.useFixture(SampleTestFixture())
        stream = StringIO()
        dist = Distribution()
        dist.script_name = 'setup.py'
        dist.script_args = ['test']
        dist.cmdclass = {'test': TestCommand}
        dist.command_options = {
            'test': {
                'test_suite': (
                    'command line', 'testtools.runexample.test_suite')}}
        cmd = dist.reinitialize_command('test')
        cmd.runner.stdout = stream
        dist.run_command('test')
        self.assertEqual("""Tests running...
Ran 2 tests in 0.000s

OK
""", stream.getvalue())


def test_suite():
    from unittest import TestLoader
    return TestLoader().loadTestsFromName(__name__)
