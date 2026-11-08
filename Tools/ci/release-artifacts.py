#!/usr/bin/env python3
"""Select a successful build of the release tag and stage only its release assets."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

ARTIFACTS = ('linux-netplay', 'windows-netplay', 'macOS-netplay')


def api(path):
    return json.loads(subprocess.check_output(['gh', 'api', path], text=True))


def tag_commit(tag):
    subprocess.run(['git', 'check-ref-format', 'refs/tags/' + tag], check=True, capture_output=True)
    sha = subprocess.check_output(['git', 'rev-parse', '--verify', 'refs/tags/' + tag + '^{commit}'], text=True).strip()
    if not re.fullmatch('[0-9a-f]{40}', sha):
        raise ValueError('The release tag did not resolve to a commit')
    return sha


def artifact_ids(repo, run):
    artifacts = api(f'repos/{repo}/actions/runs/{run["id"]}/artifacts?per_page=100')['artifacts']
    chosen = []
    for name in ARTIFACTS:
        matches = [a for a in artifacts if a['name'] == name and not a['expired']]
        if len(matches) != 1:
            raise ValueError(f'Build {run["id"]} needs one unexpired {name} artifact; rerun the build')
        chosen.append(str(matches[0]['id']))
    return ','.join(chosen)


def resolve(repo, sha, run_id=None, wait=1800):
    workflow_id = api(f'repos/{repo}/actions/workflows/pr-build.yml')['id']
    deadline = time.monotonic() + wait
    while True:
        if run_id:
            runs = [api(f'repos/{repo}/actions/runs/{run_id}')]
        else:
            runs = api(f'repos/{repo}/actions/workflows/pr-build.yml/runs?head_sha={sha}&per_page=100')['workflow_runs']
        runs = [r for r in runs if r['head_sha'] == sha and r['workflow_id'] == workflow_id and
                r['event'] in ('push', 'workflow_dispatch')]
        if run_id and not runs:
            raise ValueError('The selected run is not a trusted build of the release tag commit')
        successful = sorted((r for r in runs if r['conclusion'] == 'success'), key=lambda r: r['id'], reverse=True)
        if successful:
            run = successful[0]
            return {'run_id': str(run['id']), 'sha': sha, 'artifact_ids': artifact_ids(repo, run)}
        if runs and all(r['status'] == 'completed' for r in runs):
            raise ValueError('The build of the release tag failed or was cancelled; rerun pr-build.yml for that tag')
        if time.monotonic() >= deadline:
            raise ValueError('No successful build of the release tag; run pr-build.yml for that tag, then retry')
        time.sleep(10)


def stage(downloads, output, version):
    if not re.fullmatch(r'[0-9][A-Za-z0-9.+-]*', version):
        raise ValueError('Invalid native release version')
    patterns = {'Linux.zip': r'.+-linux-netplay\.zip', 'Win.zip': r'.+-windows-netplay\.zip',
                'Mac.dmg': r'.+-macOS-netplay\.dmg'}
    found = {}
    for path in downloads.rglob('*'):
        if path.is_symlink():
            raise ValueError('Release artifacts must not contain symlinks')
        if not path.is_file():
            continue
        for target, pattern in patterns.items():
            if not re.fullmatch(pattern, path.name):
                continue
            if target in found:
                raise ValueError('Duplicate native release archive: ' + target)
            if not path.stat().st_size:
                raise ValueError('Empty native release archive: ' + path.name)
            found[target] = path
    missing = patterns.keys() - found.keys()
    if missing:
        raise ValueError('Missing native release archives: ' + ', '.join(sorted(missing)))
    if output.exists() and any(output.iterdir()):
        raise ValueError('Release output directory must be empty')
    output.mkdir(parents=True, exist_ok=True)
    assets = []
    for target, path in sorted(found.items()):
        name = f'FM-Slippi-{version}-{target}'
        shutil.copy2(path, output / name)
        assets.append(name)
    return assets


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    source = sub.add_parser('resolve')
    source.add_argument('--tag', required=True)
    source.add_argument('--repo', default=os.environ.get('GITHUB_REPOSITORY', 'sats-melee/Ishiiruka'))
    source.add_argument('--run-id', type=int)
    source.add_argument('--wait', type=int, default=1800)
    collect = sub.add_parser('stage')
    collect.add_argument('--downloads', type=Path, required=True)
    collect.add_argument('--output', type=Path, required=True)
    collect.add_argument('--version', required=True)
    args = parser.parse_args()
    if args.command == 'resolve':
        sha = tag_commit(args.tag)
        result = resolve(args.repo, sha, args.run_id, args.wait)
        result['version'] = args.tag.removeprefix('v')
        if not re.fullmatch(r'[0-9][A-Za-z0-9.+-]*', result['version']):
            raise ValueError('Invalid tagged application version')
        if os.environ.get('GITHUB_OUTPUT'):
            with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
                for key, value in result.items():
                    output.write(f'{key}={value}\n')
        print(json.dumps(result))
    else:
        print(json.dumps({'assets': stage(args.downloads, args.output, args.version)}))


if __name__ == '__main__':
    main()
