#!/usr/bin/env python3
"""Package the release binary; keep all output in target/macos."""
import argparse
import json
import os
import pathlib
import plistlib
import re
import shutil
import subprocess
import tomllib
import tempfile

from release_package import dependency_notices

root = pathlib.Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--profile', choices=['release', 'fast', 'measure'], default='release')
parser.add_argument('--binary', type=pathlib.Path, help='Reuse a previously verified executable')
args = parser.parse_args()
profile = args.profile
binary = args.binary or pathlib.Path(os.environ.get('CARGO_TARGET_DIR', root / 'target')) / profile / 'yapfast'
if not binary.is_file():
    raise SystemExit('Run scripts/build-macos first.')
version = tomllib.loads((root / 'Cargo.toml').read_text())['package']['version']
release = os.environ.get('YAPFAST_RELEASE_TAG', 'v' + version).removeprefix('v')
if not re.fullmatch(re.escape(version) + r'(?:-macos\.\d+)?', release):
    raise SystemExit('Release tag must match the app version.')
out = root / 'target' / ('macos' if profile == 'release' else f'macos-{profile}')
app = out / 'Yapfast.app'
contents = app / 'Contents'
(contents / 'MacOS').mkdir(parents=True, exist_ok=True)
(contents / 'Resources').mkdir(exist_ok=True)
shutil.copy2(binary, contents / 'MacOS/yapfast')
if profile == 'release':
    subprocess.run(['strip', '-x', str(contents / 'MacOS/yapfast')], check=True)
with tempfile.TemporaryDirectory(prefix='yapfast-icon-') as scratch:
    scratch = pathlib.Path(scratch)
    svg = scratch / 'icon.svg'
    svg.write_text((root / 'packaging/icon.svg').read_text().replace('width="64" height="64"', 'width="1024" height="1024"'))
    png = scratch / 'icon.png'
    subprocess.run(['sips', '-s', 'format', 'png', str(svg), '--out', str(png)], check=True, stdout=subprocess.DEVNULL)
    iconset = scratch / 'Yapfast.iconset'
    iconset.mkdir()
    for size in (16, 32, 128, 256, 512):
        for scale in (1, 2):
            pixels = str(size * scale)
            name = f'icon_{size}x{size}' + ('@2x' if scale == 2 else '') + '.png'
            subprocess.run(['sips', '-z', pixels, pixels, str(png), '--out', str(iconset / name)], check=True, stdout=subprocess.DEVNULL)
    subprocess.run(['iconutil', '-c', 'icns', str(iconset), '-o', str(contents / 'Resources/Yapfast.icns')], check=True)
info = dict(CFBundleIconFile='Yapfast.icns', CFBundleName='Yapfast', CFBundleDisplayName='Yapfast',
    CFBundleIdentifier='io.github.achuille.yapfast', CFBundleExecutable='yapfast',
    CFBundlePackageType='APPL', CFBundleShortVersionString=version,
    CFBundleVersion=version, YapfastReleaseVersion=release, LSMinimumSystemVersion='13.0',
    NSHighResolutionCapable=True,
    NSMicrophoneUsageDescription='Yapfast uses your microphone for Discord calls.',
    NSCameraUsageDescription='Yapfast uses your camera for video calls.')
with (contents / 'Info.plist').open('wb') as f:
    plistlib.dump(info, f)
for name in ['LICENSE', 'README.md']:
    shutil.copy2(root / name, contents / 'Resources' / name)
metadata = json.loads(subprocess.check_output(
    ['cargo', 'metadata', '--locked', '--format-version', '1', '--filter-platform',
     subprocess.check_output(['rustc', '-vV'], text=True).split('host: ')[1].splitlines()[0]],
    cwd=root, text=True))
build = pathlib.Path(os.environ.get('CARGO_TARGET_DIR', metadata['target_directory'])) / profile / 'build'

def native_notice(pattern):
    matches = list(build.glob(pattern))
    if not matches:
        raise SystemExit(f'Missing native license: {pattern}; build the app first.')
    return max(matches, key=lambda path: path.stat().st_mtime).parent

licenses = contents / 'Resources' / 'licenses'
if licenses.exists():
    shutil.rmtree(licenses)
dependencies = dependency_notices(
    metadata, contents / 'Resources', licenses, None,
    native_notice('skia-bindings-*/out/skia/LICENSE_SKIA'),
    native_notice('scratch-*/out/livekit_webrtc/livekit/*/*/LICENSE.md'))
(contents / 'Resources' / 'THIRD-PARTY.json').write_text(
    json.dumps(dependencies, indent=2) + '\n')
# Apple Development / Developer ID signing preserves Keychain's team identity
# across builds. A self-signed certificate does not fix its cdhash partitions.
identity = os.environ.get('YAPFAST_SIGNING_IDENTITY', '-')
if not identity:
    raise SystemExit('Empty signing identity; refusing an accidental ad-hoc fallback.')
# A missing configured identity fails codesign, rather than changing the app's identity.
subprocess.run(['codesign', '--force', '--sign', identity, str(app)], check=True)
subprocess.run(['codesign', '--verify', '--strict', str(app)], check=True)
archive = out / f'Yapfast-{release}-macOS-{subprocess.check_output(["uname", "-m"], text=True).strip()}.zip'
subprocess.run(['ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', str(app), str(archive)], check=True)
print(app)
print(archive)
