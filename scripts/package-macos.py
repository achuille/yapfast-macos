#!/usr/bin/env python3
"""Package the release binary; keep all output in target/macos."""
import argparse
import os
import pathlib
import plistlib
import shutil
import subprocess
import tomllib
import tempfile

root = pathlib.Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--profile', choices=['release', 'fast', 'measure'], default='release')
profile = parser.parse_args().profile
binary = pathlib.Path(os.environ.get('CARGO_TARGET_DIR', root / 'target')) / profile / 'yapfast'
if not binary.is_file():
    raise SystemExit('Run scripts/build-macos first.')
version = tomllib.loads((root / 'Cargo.toml').read_text())['package']['version']
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
    CFBundleVersion=version, LSMinimumSystemVersion='13.0',
    NSHighResolutionCapable=True,
    NSMicrophoneUsageDescription='Yapfast utilise le micro pour les appels Discord.',
    NSCameraUsageDescription='Yapfast utilise la caméra pendant les appels vidéo.')
with (contents / 'Info.plist').open('wb') as f:
    plistlib.dump(info, f)
for name in ['LICENSE', 'README.md']:
    shutil.copy2(root / name, contents / 'Resources' / name)
# Ad-hoc signing allows local execution; distribution needs Developer ID + notarization.
subprocess.run(['codesign', '--force', '--sign', '-', str(app)], check=True)
subprocess.run(['codesign', '--verify', '--strict', str(app)], check=True)
archive = out / f'Yapfast-{version}-macOS-{subprocess.check_output(["uname", "-m"], text=True).strip()}.zip'
subprocess.run(['ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', str(app), str(archive)], check=True)
print(app)
print(archive)
