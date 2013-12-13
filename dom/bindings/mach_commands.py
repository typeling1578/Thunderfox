# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, # You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import unicode_literals

import os
import sys

from mach.decorators import (
    CommandArgument,
    CommandProvider,
    Command,
)

from mozbuild.base import MachCommandBase


@CommandProvider
class WebIDLProvider(MachCommandBase):
    @Command('webidl-example', category='misc',
        description='Generate example files for a WebIDL interface.')
    @CommandArgument('interface', nargs='+',
        help='Interface(s) whose examples to generate.')
    def webidl_example(self, interface):
        from mozwebidlcodegen import BuildSystemWebIDL

        manager = self._spawn(BuildSystemWebIDL).manager
        for i in interface:
            manager.generate_example_files(i)

    @Command('webidl-parser-test', category='testing',
        description='Run WebIDL tests.')
    @CommandArgument('--verbose', '-v', action='store_true',
        help='Run tests in verbose mode.')
    def webidl_test(self, verbose=False):
        sys.path.insert(0, os.path.join(self.topsrcdir, 'other-licenses',
            'ply'))

        from runtests import run_tests
        return run_tests(None, verbose=verbose)
