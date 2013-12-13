# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import unicode_literals

import imp
import json
import os
import shutil
import sys
import tempfile
import unittest

import mozpack.path as mozpath

from mozwebidlcodegen import (
    WebIDLCodegenManager,
    WebIDLCodegenManagerState,
)

from mozfile import NamedTemporaryFile

from mozunit import (
    MockedOpen,
    main,
)


OUR_DIR = mozpath.abspath(mozpath.dirname(__file__))
TOPSRCDIR = mozpath.normpath(mozpath.join(OUR_DIR, '..', '..', '..', '..'))


class TestWebIDLCodegenManager(unittest.TestCase):
    TEST_STEMS = {
        'Child',
        'Parent',
        'ExampleBinding',
        'TestEvent',
    }

    @property
    def _static_input_paths(self):
        s = {mozpath.join(OUR_DIR, p) for p in os.listdir(OUR_DIR)
            if p.endswith('.webidl')}

        return s

    @property
    def _config_path(self):
        config = mozpath.join(TOPSRCDIR, 'dom', 'bindings', 'Bindings.conf')
        self.assertTrue(os.path.exists(config))

        return config

    def _get_manager_args(self):
        tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, tmp)

        cache_dir = mozpath.join(tmp, 'cache')
        os.mkdir(cache_dir)

        ip = self._static_input_paths

        inputs = (
            ip,
            {mozpath.splitext(mozpath.basename(p))[0] for p in ip},
            set()
        )

        return dict(
            config_path=self._config_path,
            inputs=inputs,
            exported_header_dir=mozpath.join(tmp, 'exports'),
            codegen_dir=mozpath.join(tmp, 'codegen'),
            state_path=mozpath.join(tmp, 'state.json'),
            make_deps_path=mozpath.join(tmp, 'codegen.pp'),
            make_deps_target='codegen.pp',
            cache_dir=cache_dir,
        )

    def _get_manager(self):
        return WebIDLCodegenManager(**self._get_manager_args())

    def test_unknown_state_version(self):
        """Loading a state file with a too new version resets state."""
        args = self._get_manager_args()

        p = args['state_path']

        with open(p, 'wb') as fh:
            json.dump({
                'version': WebIDLCodegenManagerState.VERSION + 1,
                'foobar': '1',
            }, fh)

        manager = WebIDLCodegenManager(**args)

        self.assertEqual(manager._state['version'],
            WebIDLCodegenManagerState.VERSION)
        self.assertNotIn('foobar', manager._state)

    def test_generate_build_files(self):
        """generate_build_files() does the right thing from empty."""
        manager = self._get_manager()
        result = manager.generate_build_files()
        self.assertEqual(len(result.inputs), 5)

        output = manager.expected_build_output_files()
        self.assertEqual(result.created, output)
        self.assertEqual(len(result.updated), 0)
        self.assertEqual(len(result.unchanged), 0)

        for f in output:
            self.assertTrue(os.path.isfile(f))

        for f in manager.GLOBAL_DECLARE_FILES:
            self.assertIn(mozpath.join(manager._exported_header_dir, f), output)

        for f in manager.GLOBAL_DEFINE_FILES:
            self.assertIn(mozpath.join(manager._codegen_dir, f), output)

        for s in self.TEST_STEMS:
            self.assertTrue(os.path.isfile(mozpath.join(
                manager._exported_header_dir, '%sBinding.h' % s)))
            self.assertTrue(os.path.isfile(mozpath.join(
                manager._codegen_dir, '%sBinding.cpp' % s)))

        self.assertTrue(os.path.isfile(manager._state_path))

        with open(manager._state_path, 'rb') as fh:
            state = json.load(fh)
            self.assertEqual(state['version'], 1)
            self.assertIn('webidls', state)

            child = state['webidls']['Child.webidl']
            self.assertEqual(len(child['inputs']), 2)
            self.assertEqual(len(child['outputs']), 2)
            self.assertEqual(child['sha1'], 'c41527cad3bc161fa6e7909e48fa11f9eca0468b')

    def test_generate_build_files_load_state(self):
        """State should be equivalent when instantiating a new instance."""
        args = self._get_manager_args()
        m1 = WebIDLCodegenManager(**args)
        self.assertEqual(len(m1._state['webidls']), 0)
        m1.generate_build_files()

        m2 = WebIDLCodegenManager(**args)
        self.assertGreater(len(m2._state['webidls']), 2)
        self.assertEqual(m1._state, m2._state)

    def test_no_change_no_writes(self):
        """If nothing changes, no files should be updated."""
        args = self._get_manager_args()
        m1 = WebIDLCodegenManager(**args)
        m1.generate_build_files()

        m2 = WebIDLCodegenManager(**args)
        result = m2.generate_build_files()

        self.assertEqual(len(result.inputs), 0)
        self.assertEqual(len(result.created), 0)
        self.assertEqual(len(result.updated), 0)

    def test_output_file_regenerated(self):
        """If an output file disappears, it is regenerated."""
        args = self._get_manager_args()
        m1 = WebIDLCodegenManager(**args)
        m1.generate_build_files()

        rm_count = 0
        for p in m1._state['webidls']['Child.webidl']['outputs']:
            rm_count += 1
            os.unlink(p)

        for p in m1.GLOBAL_DECLARE_FILES:
            rm_count += 1
            os.unlink(mozpath.join(m1._exported_header_dir, p))

        m2 = WebIDLCodegenManager(**args)
        result = m2.generate_build_files()
        self.assertEqual(len(result.created), rm_count)

    def test_only_rebuild_self(self):
        """If an input file changes, only rebuild that one file."""
        args = self._get_manager_args()
        m1 = WebIDLCodegenManager(**args)
        m1.generate_build_files()

        child_path = None
        for p in m1._input_paths:
            if p.endswith('Child.webidl'):
                child_path = p
                break

        self.assertIsNotNone(child_path)
        child_content = open(child_path, 'rb').read()

        with MockedOpen({child_path: child_content + '\n/* */'}):
            m2 = WebIDLCodegenManager(**args)
            result = m2.generate_build_files()
            self.assertEqual(result.inputs, set([child_path]))
            self.assertEqual(len(result.updated), 0)
            self.assertEqual(len(result.created), 0)

    def test_rebuild_dependencies(self):
        """Ensure an input file used by others results in others rebuilding."""
        args = self._get_manager_args()
        m1 = WebIDLCodegenManager(**args)
        m1.generate_build_files()

        parent_path = None
        child_path = None
        for p in m1._input_paths:
            if p.endswith('Parent.webidl'):
                parent_path = p
            elif p.endswith('Child.webidl'):
                child_path = p

        self.assertIsNotNone(parent_path)
        parent_content = open(parent_path, 'rb').read()

        with MockedOpen({parent_path: parent_content + '\n/* */'}):
            m2 = WebIDLCodegenManager(**args)
            result = m2.generate_build_files()
            self.assertEqual(result.inputs, {child_path, parent_path})
            self.assertEqual(len(result.updated), 0)
            self.assertEqual(len(result.created), 0)

    def test_python_change_regenerate_everything(self):
        """If a Python file changes, we should attempt to rebuild everything."""

        # We don't want to mutate files in the source directory because we want
        # to be able to build from a read-only filesystem. So, we install a
        # dummy module and rewrite the metadata to say it comes from the source
        # directory.
        #
        # Hacking imp to accept a MockedFile doesn't appear possible. So for
        # the first iteration we read from a temp file. The second iteration
        # doesn't need to import, so we are fine with a mocked file.
        fake_path = mozpath.join(OUR_DIR, 'fakemodule.py')
        with NamedTemporaryFile('wt') as fh:
            fh.write('# Original content')
            fh.flush()
            mod = imp.load_source('mozwebidlcodegen.fakemodule', fh.name)
            mod.__file__ = fake_path

            args = self._get_manager_args()
            m1 = WebIDLCodegenManager(**args)
            with MockedOpen({fake_path: '# Original content'}):
                old_exists = os.path.exists
                try:
                    def exists(p):
                        if p == fake_path:
                            return True
                        return old_exists(p)

                    os.path.exists = exists

                    result = m1.generate_build_files()
                    l = len(result.inputs)

                    with open(fake_path, 'wt') as fh:
                        fh.write('# Modified content')

                    m2 = WebIDLCodegenManager(**args)
                    result = m2.generate_build_files()
                    self.assertEqual(len(result.inputs), l)

                    result = m2.generate_build_files()
                    self.assertEqual(len(result.inputs), 0)
                finally:
                    os.path.exists = old_exists
                    del sys.modules['mozwebidlcodegen.fakemodule']


if __name__ == '__main__':
    main()
