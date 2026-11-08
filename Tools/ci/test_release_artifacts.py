#!/usr/bin/env python3
"""Release selection/staging checks using temporary Git repositories and mocked GitHub data."""
import contextlib
import copy
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('release_artifacts', Path(__file__).with_name('release-artifacts.py'))
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)

REPO = 'sats-melee/Ishiiruka'
SHA = '1' * 40
VERSION = '1.2.3'



def run(run_id=100, **changes):
    result = {'id': run_id, 'head_sha': SHA, 'workflow_id': 77, 'event': 'push',
              'status': 'completed', 'conclusion': 'success'}
    result.update(changes)
    return result


def artifacts():
    return [{'id': 201 + i, 'name': name, 'expired': False} for i, name in enumerate(release.ARTIFACTS)]


class SelectionTests(unittest.TestCase):
    def resolve(self, runs, available=None, run_id=None, sha=SHA):
        calls = []

        def api(path):
            calls.append(path)
            if path.endswith('/actions/workflows/pr-build.yml'):
                return {'id': 77}
            if '/artifacts?' in path:
                return {'artifacts': copy.deepcopy(artifacts() if available is None else available)}
            if '/runs?head_sha=' in path:
                return {'workflow_runs': copy.deepcopy(runs)}
            if run_id is not None and path.endswith('/actions/runs/' + str(run_id)):
                return copy.deepcopy(runs[0])
            self.fail('Unexpected API request: ' + path)

        with patch.object(release, 'api', side_effect=api), patch.object(release.time, 'sleep') as sleep:
            result = release.resolve(REPO, sha, run_id=run_id, wait=0)
            sleep.assert_not_called()
        return result, calls

    def test_selects_newest_success_for_exact_sha_workflow_and_trusted_event(self):
        result, calls = self.resolve([run(9), run(12, conclusion='failure'), run(11, event='workflow_dispatch'),
                                      run(90, head_sha='2'*40), run(91, workflow_id=88),
                                      run(92, event='pull_request')])
        self.assertEqual(result, {'run_id': '11', 'sha': SHA, 'artifact_ids': '201,202,203'})
        self.assertIn(f'repos/{REPO}/actions/runs/11/artifacts?per_page=100', calls)

    def test_explicit_successful_run_is_used(self):
        result, _ = self.resolve([run(8, event='workflow_dispatch')], run_id=8)
        self.assertEqual(result['run_id'], '8')

    def test_explicit_wrong_sha_workflow_or_untrusted_event_is_rejected(self):
        for changes in ({'head_sha': '2'*40}, {'workflow_id': 88}, {'event': 'pull_request'},
                        {'event': 'pull_request_target'}):
            with self.subTest(changes=changes), self.assertRaisesRegex(ValueError, 'not a trusted build'):
                self.resolve([run(8, **changes)], run_id=8)

    def test_failed_cancelled_or_skipped_build_is_rejected(self):
        for conclusion in ('failure', 'cancelled', 'skipped', 'timed_out'):
            with self.subTest(conclusion=conclusion), self.assertRaisesRegex(ValueError, 'failed or was cancelled'):
                self.resolve([run(conclusion=conclusion)])

    def test_missing_or_untrusted_only_builds_time_out_without_real_wait(self):
        for runs in ([], [run(event='pull_request')], [run(head_sha='2'*40)], [run(workflow_id=88)]):
            with self.subTest(runs=runs), self.assertRaisesRegex(ValueError, 'No successful build'):
                self.resolve(runs)

    def test_missing_expired_and_duplicate_platform_artifacts_are_rejected(self):
        missing = artifacts()[:-1]
        expired = artifacts(); expired[1]['expired'] = True
        duplicate = artifacts() + [dict(artifacts()[0], id=900)]
        for available in (missing, expired, duplicate):
            with self.subTest(available=available), self.assertRaisesRegex(ValueError, 'one unexpired'):
                self.resolve([run()], available)

    def test_expired_old_duplicate_and_unrelated_artifact_are_not_selected(self):
        available = artifacts() + [dict(artifacts()[0], id=900, expired=True),
                                   {'id': 999, 'name': 'debug-output', 'expired': False}]
        result, _ = self.resolve([run()], available)
        self.assertEqual(result['artifact_ids'], '201,202,203')

    def test_default_wait_allows_build_beyond_thirty_minutes_then_success(self):
        replies = iter([{'workflow_runs': [run(status='in_progress', conclusion=None)]},
                        {'workflow_runs': [run()]}])

        def api(path):
            if path.endswith('/actions/workflows/pr-build.yml'): return {'id': 77}
            if '/artifacts?' in path: return {'artifacts': artifacts()}
            return next(replies)

        with patch.object(release, 'api', side_effect=api), \
             patch.object(release.time, 'sleep') as sleep, \
             patch.object(release.time, 'monotonic', side_effect=[0, 1866]):
            self.assertEqual(release.resolve(REPO, SHA)['run_id'], '100')
            sleep.assert_called_once_with(10)

    def test_pending_build_wait_is_bounded_by_the_deadline(self):
        def api(path):
            if path.endswith('/actions/workflows/pr-build.yml'): return {'id': 77}
            return {'workflow_runs': [run(status='in_progress', conclusion=None)]}

        with patch.object(release, 'api', side_effect=api), \
             patch.object(release.time, 'sleep') as sleep, \
             patch.object(release.time, 'monotonic', side_effect=[100, 104, 105]):
            with self.assertRaisesRegex(ValueError, 'No successful build'):
                release.resolve(REPO, SHA, wait=5)
            sleep.assert_called_once_with(10)


@contextlib.contextmanager
def temporary_git():
    with tempfile.TemporaryDirectory() as folder:
        previous = Path.cwd()
        os.chdir(folder)
        try:
            def git(*args):
                return subprocess.check_output(['git', '-c', 'user.name=Release Test', '-c',
                    'user.email=release@example.invalid', *args], text=True, stderr=subprocess.DEVNULL).strip()
            git('init', '-b', 'main')
            package = Path('release/app/package.json')
            package.parent.mkdir(parents=True)
            package.write_text(json.dumps({'version': VERSION}))
            git('add', '.'); git('commit', '-m', 'Tagged release source')
            tagged = git('rev-parse', 'HEAD')
            git('tag', 'v1.2.3')
            git('tag', '-a', 'v1.2.3-annotated', '-m', 'Annotated release')
            package.write_text(json.dumps({'version': '9.9.9'}))
            git('add', '.'); git('commit', '-m', 'Main moved past the tag')
            yield git, tagged
        finally:
            os.chdir(previous)


class TagTests(unittest.TestCase):
    def test_annotated_and_lightweight_tags_resolve_to_original_commit_after_main_moves(self):
        with temporary_git() as (git, tagged):
            self.assertNotEqual(git('rev-parse', 'HEAD'), tagged)
            self.assertEqual(release.tag_commit('v1.2.3'), tagged)
            self.assertEqual(release.tag_commit('v1.2.3-annotated'), tagged)

    def test_invalid_tag_ref_is_rejected(self):
        with temporary_git(), self.assertRaises(subprocess.CalledProcessError):
            release.tag_commit('../main')

    def test_cli_reads_version_and_build_sha_from_tag_not_moving_main(self):
        with temporary_git() as (_, tagged):
            output = Path('github-output')
            selected = {'run_id': '100', 'sha': tagged, 'artifact_ids': '201,202,203'}
            with patch.object(sys, 'argv', ['release-artifacts.py', 'resolve', '--tag', 'v1.2.3']), \
                 patch.object(release, 'resolve', return_value=selected) as resolve, \
                 patch.dict(os.environ, {'GITHUB_OUTPUT': str(output), 'GITHUB_REPOSITORY': REPO}), \
                 contextlib.redirect_stdout(io.StringIO()) as console:
                release.main()
            resolve.assert_called_once_with(REPO, tagged, None, 3600)
            self.assertEqual(json.loads(console.getvalue())['version'], VERSION)
            self.assertIn('version=1.2.3\n', output.read_text())
            self.assertIn('sha=' + tagged + '\n', output.read_text())


class StagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.downloads = self.root/'downloads'
        self.output = self.root/'release-assets'
        self.files = {
            'Linux.zip': '2026-11-02-abcdef123-v3.6.4-linux-netplay.zip',
            'Win.zip': '2026-11-02-abcdef123-v3.6.4-windows-netplay.zip',
            'Mac.dmg': '2026-11-02-abcdef123-v3.6.4-macOS-netplay.dmg',
        }
        for platform, name in self.files.items():
            path = self.downloads/platform/name
            path.parent.mkdir(parents=True)
            path.write_bytes(platform.encode())

    def stage(self):
        return release.stage(self.downloads, self.output, VERSION)

    def test_exact_native_archives_renamed_without_rewriting_bytes(self):
        for name in ('builder-debug.yml', 'stray.zip', 'notes.txt', 'old-linux-playback.zip',
                     'old-windows-playback.zip', 'old-macOS-playback.dmg', 'stray.exe'):
            (self.downloads/name).write_bytes(b'not a release asset')
        expected = sorted('FM-Slippi-1.2.3-' + platform for platform in self.files)
        self.assertEqual(self.stage(), expected)
        self.assertEqual(sorted(p.name for p in self.output.iterdir()), expected)
        for platform, original in self.files.items():
            self.assertEqual((self.output/('FM-Slippi-1.2.3-' + platform)).read_bytes(), platform.encode())
            self.assertTrue((self.downloads/platform/original).exists())

    def test_missing_platform_is_rejected_before_output_creation(self):
        for platform, name in self.files.items():
            with self.subTest(platform=platform):
                source = self.downloads/platform/name; source.unlink()
                with self.assertRaisesRegex(ValueError, 'Missing native release archives'):
                    self.stage()
                self.assertFalse(self.output.exists())
                source.write_bytes(platform.encode())

    def test_duplicate_platform_archive_is_rejected(self):
        (self.downloads/'second-linux-netplay.zip').write_bytes(b'duplicate')
        with self.assertRaisesRegex(ValueError, 'Duplicate native release archive'):
            self.stage()

    def test_empty_platform_archive_is_rejected(self):
        (self.downloads/'Mac.dmg'/self.files['Mac.dmg']).write_bytes(b'')
        with self.assertRaisesRegex(ValueError, 'Empty native release archive'):
            self.stage()

    def test_symlinks_rejected_even_when_filename_is_unrelated(self):
        link = self.downloads/'unrelated-link'
        link.symlink_to(self.root)
        with self.assertRaisesRegex(ValueError, 'must not contain symlinks'):
            self.stage()

    def test_nonempty_output_is_preserved_and_rejected(self):
        self.output.mkdir()
        sentinel = self.output/'keep'; sentinel.write_bytes(b'preserve')
        with self.assertRaisesRegex(ValueError, 'must be empty'):
            self.stage()
        self.assertEqual(list(self.output.iterdir()), [sentinel])
        self.assertEqual(sentinel.read_bytes(), b'preserve')

    def test_empty_existing_output_is_allowed(self):
        self.output.mkdir()
        self.assertEqual(len(self.stage()), 3)

    def test_version_path_injection_rejected(self):
        for version in ('../1.2.3', '1.2.3/evil', '', 'v1.2.3', '1.2.3\nother'):
            with self.subTest(version=version), self.assertRaisesRegex(ValueError, 'Invalid native release version'):
                release.stage(self.downloads, self.output, version)
        self.assertFalse(self.output.exists())


if __name__ == '__main__':
    unittest.main()
