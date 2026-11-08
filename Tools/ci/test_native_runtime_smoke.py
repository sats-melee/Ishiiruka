#!/usr/bin/env python3
"""Package smoke checks with small format fixtures and a harmless checker subprocess."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('smoke', Path(__file__).with_name('smoke-native-runtime.py'))
smoke = importlib.util.module_from_spec(spec)
spec.loader.exec_module(smoke)


class SmokeChecks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def file(self, name, content):
        path = self.root/name
        path.write_bytes(content)
        return path

    def checker(self, statement):
        path = self.root/'checker.py'
        path.write_text('import json, sys\nrequest=json.loads(sys.stdin.readline())\n'
                        'assert request=={"version":1,"backend":"nwc","nwc_uri":"not-a-valid-nwc-uri"}\n'
                        'assert len(sys.argv)==1\n' + statement + '\n')
        return [sys.executable, str(path)]

    def test_elf_pe_and_macho_x64_headers(self):
        elf = bytearray(64); elf[:6] = b'\x7fELF\x02\x01'; elf[18:20] = b'\x3e\x00'
        pe = bytearray(134); pe[:2] = b'MZ'; pe[60:64] = b'\x80\0\0\0'; pe[128:134] = b'PE\0\0\x64\x86'
        macho = b'\xcf\xfa\xed\xfe\x07\0\0\x01' + bytes(24)
        for name, header in [('linux', elf), ('windows.exe', pe), ('mac', macho)]:
            with self.subTest(name=name):
                self.assertEqual(smoke.architecture(self.file(name, header)), 'x86_64')

    def test_arm_library_is_rejected_before_checker_execution(self):
        arm = bytearray(64); arm[:6] = b'\x7fELF\x02\x01'; arm[18:20] = b'\xb7\x00'
        path = self.file('arm64', arm)
        self.assertEqual(smoke.architecture(path), 'arm64')
        args = argparse.Namespace(binary=path, checker=path, library=path, slippi_library=None, gui_load=False)
        with patch.object(smoke, 'check_wallet') as check:
            with self.assertRaisesRegex(ValueError, 'expected x86_64'):
                smoke.smoke(args)
            check.assert_not_called()

    def test_truncated_or_nonbinary_files_are_rejected(self):
        short_pe = bytearray(132); short_pe[:2] = b'MZ'; short_pe[60:64] = b'\x80\0\0\0'; short_pe[128:] = b'PE\0\0'
        bad_elf = bytearray(64); bad_elf[:6] = b'\x7fELF\x02\0'; bad_elf[18:20] = b'\0\x3e'
        for number, content in enumerate((b'\x7fELF', bad_elf, short_pe, b'\xcf\xfa\xed\xfe', b'not an executable')):
            with self.subTest(number=number), self.assertRaisesRegex(ValueError, 'Unsupported or incomplete'):
                smoke.architecture(self.file(str(number), content))

    def test_missing_file_is_reported_before_any_process(self):
        missing = self.root/'missing'
        args = argparse.Namespace(binary=missing, checker=missing, library=missing, slippi_library=None, gui_load=False)
        with patch.object(smoke, 'check_wallet') as check:
            with self.assertRaisesRegex(ValueError, 'Missing packaged file'):
                smoke.smoke(args)
            check.assert_not_called()

    def test_windows_requires_every_app_local_crt_before_running_checker(self):
        pe = bytearray(134); pe[:2] = b'MZ'; pe[60:64] = b'\x80\0\0\0'; pe[128:134] = b'PE\0\0\x64\x86'
        binary = self.file('game.exe', pe)
        names = ('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')
        args = argparse.Namespace(binary=binary, checker=binary, library=binary, slippi_library=None, gui_load=False)
        for missing in names:
            for name in names: self.file(name, pe)
            (self.root/missing).unlink()
            with self.subTest(missing=missing), patch.object(smoke.sys, 'platform', 'win32'), \
                 patch.object(smoke, 'check_wallet') as check:
                with self.assertRaisesRegex(ValueError, 'Missing packaged file.*' + missing):
                    smoke.smoke(args)
                check.assert_not_called()

    def test_windows_crt_is_architecture_checked_and_recorded(self):
        pe = bytearray(134); pe[:2] = b'MZ'; pe[60:64] = b'\x80\0\0\0'; pe[128:134] = b'PE\0\0\x64\x86'
        binary = self.file('game.exe', pe)
        names = ('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')
        for name in names: self.file(name, pe)
        args = argparse.Namespace(binary=binary, checker=binary, library=binary, slippi_library=None, gui_load=False)
        with patch.object(smoke.sys, 'platform', 'win32'), patch.object(smoke, 'check_wallet', return_value={'ok': False}):
            result = smoke.smoke(args)
        self.assertTrue(all(result['files'][name]['architecture'] == 'x86_64' for name in names))
        arm = bytearray(pe); arm[132:134] = b'\x64\xaa'
        self.file('vcruntime140_1.dll', arm)
        with patch.object(smoke.sys, 'platform', 'win32'), patch.object(smoke, 'check_wallet') as check:
            with self.assertRaisesRegex(ValueError, 'architecture is arm64'):
                smoke.smoke(args)
            check.assert_not_called()

    def test_environment_uses_fresh_profile_and_removes_wallet_and_loader_overrides(self):
        inherited = {'SATS_ARENA_NWC_URI': 'synthetic', 'SATS_ARENA_WALLET_CONFIG': 'stdin',
                     'LD_LIBRARY_PATH': '/build/lib', 'LD_PRELOAD': '/build/shim.so',
                     'DYLD_LIBRARY_PATH': '/build/maclib', 'DYLD_FALLBACK_LIBRARY_PATH': '/build/fallback',
                     'GH_TOKEN': 'synthetic', 'PATH': '/system/bin'}
        with patch.dict(os.environ, inherited), patch.object(smoke.sys, 'platform', 'linux'):
            env = smoke.environment(self.root/'profile', [self.root/'package/lib/engine.so'])
        self.assertFalse(any(key.startswith(('SATS_ARENA_', 'DYLD_')) for key in env))
        for key in ('LD_LIBRARY_PATH', 'LD_PRELOAD', 'GH_TOKEN'):
            self.assertNotIn(key, env)
        self.assertEqual(env['PATH'], '/system/bin')
        self.assertTrue(Path(env['XDG_CONFIG_HOME']).is_dir())
        self.assertEqual(env['HOME'], str(self.root/'profile'))
        with patch.object(smoke.sys, 'platform', 'win32'):
            windows = smoke.environment(self.root/'winprofile', [self.root/'package/engine.dll'])
        self.assertTrue(windows['PATH'].startswith(str(self.root/'package') + os.pathsep))

    def test_offline_checker_protocol_runs_in_a_real_subprocess(self):
        command = self.checker('print(json.dumps({"ok":False,"message":"invalid NWC connection string"}))')
        result = smoke.check_wallet(command, smoke.environment(self.root/'profile', []))
        self.assertEqual(result['exit_code'], 0)
        self.assertFalse(result['ok'])
        self.assertFalse(result['network_connection_attempted'])
        self.assertFalse(result['invoice_requested'])

    def test_malformed_unexpected_or_success_response_is_rejected(self):
        for statement in ('print("bad JSON")', 'print(json.dumps({"ok":True,"message":"connected"}))',
                          'print(json.dumps({"ok":False,"message":"network timed out"}))'):
            with self.subTest(statement=statement), self.assertRaises(RuntimeError):
                smoke.check_wallet(self.checker(statement), smoke.environment(self.root/'profile', []))

    def test_loader_failure_and_timeout_are_failures(self):
        command = self.checker('sys.exit(17)')
        with self.assertRaisesRegex(RuntimeError, 'exited 17'):
            smoke.check_wallet(command, smoke.environment(self.root/'profile', []))
        with patch.object(smoke.subprocess, 'run', side_effect=subprocess.TimeoutExpired(command, 15)):
            with self.assertRaisesRegex(RuntimeError, 'within 15 seconds'):
                smoke.check_wallet(command, {})

    def test_gui_load_has_no_iso_and_stops_only_its_own_process(self):
        with patch.object(smoke.subprocess, 'Popen') as popen, \
             patch.object(smoke.time, 'monotonic', side_effect=[0, 8]):
            process = popen.return_value; process.poll.return_value = None
            result = smoke.gui_load(self.root/'game', self.root, {'HOME': str(self.root)})
            params = popen.call_args.args[0]
            self.assertEqual(params[0], str(self.root/'game'))
            self.assertEqual(params[1], '-u')
            self.assertNotIn('-e', params)
            self.assertEqual(popen.call_args.kwargs['env']['SATS_ARENA_FAKE_LOCAL'], '0')
            process.terminate.assert_called_once()
            process.wait.assert_called_once_with(timeout=5)
            process.kill.assert_not_called()
            self.assertTrue(result['started_without_iso'])

    def test_early_gui_exit_fails_without_killing_an_unrelated_process(self):
        with patch.object(smoke.subprocess, 'Popen') as popen, \
             patch.object(smoke.time, 'monotonic', return_value=0):
            process = popen.return_value; process.poll.return_value = 9; process.returncode = 9
            with self.assertRaisesRegex(RuntimeError, 'exited 9 during GUI load'):
                smoke.gui_load(self.root/'game', self.root, {})
            process.terminate.assert_not_called()
            process.kill.assert_not_called()


if __name__ == '__main__':
    unittest.main()
