#!/usr/bin/env python3
"""Create a read-only drag-to-Applications disk image."""
import argparse
import pathlib
import plistlib
import re
import shutil
import subprocess
import tempfile
from ds_store import DSStore

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('app', type=pathlib.Path)
parser.add_argument('--version', required=True)
args = parser.parse_args()
app = args.app.resolve()
if not re.fullmatch(r'\d+\.\d+\.\d+(?:-macos\.\d+)?', args.version):
    parser.error('Use an app version or a macOS release version, without the v prefix.')
subprocess.run(['codesign', '--verify', '--strict', str(app)], check=True)
info = plistlib.loads((app / 'Contents/Info.plist').read_bytes())
if info['CFBundleShortVersionString'] != args.version.split('-')[0]:
    parser.error('The release version must match the app version.')
arch = subprocess.check_output(['uname', '-m'], text=True).strip()
output = app.parent / f'Yapfast-{args.version}-macOS-{arch}.dmg'
if output.exists():
    parser.error(f'Refusing to replace {output}')
with tempfile.TemporaryDirectory(prefix='yapfast-dmg-') as scratch:
    stage = pathlib.Path(scratch) / 'stage'
    stage.mkdir()
    subprocess.run(['ditto', str(app), str(stage / 'Yapfast.app')], check=True)
    (stage / 'Applications').symlink_to('/Applications')
    with DSStore.open(str(stage / '.DS_Store'), 'w+') as store:
        store['.']['bwsp'] = {'ShowToolbar': False, 'ShowStatusBar': False,
            'ShowSidebar': False, 'ShowPathbar': False, 'ShowTabView': False,
            'WindowBounds': '{{240, 240}, {540, 330}}', 'ContainerShowSidebar': False}
        store['.']['icvp'] = {'viewOptionsVersion': 1, 'backgroundType': 1,
            'backgroundColorRed': 1.0, 'backgroundColorGreen': 1.0,
            'backgroundColorBlue': 1.0, 'iconSize': 96.0, 'textSize': 13.0,
            'gridSpacing': 100.0, 'gridOffsetX': 0.0, 'gridOffsetY': 0.0,
            'arrangeBy': 'none', 'labelOnBottom': True, 'showIconPreview': True}
        store['.']['vSrn'] = ('long', 1)
        store['.']['icvl'] = ('type', b'icnv')
        store['Yapfast.app']['Iloc'] = (145, 145)
        store['Applications']['Iloc'] = (395, 145)
    temporary = pathlib.Path(scratch) / 'Yapfast.dmg'
    subprocess.run(['hdiutil', 'create', '-volname', 'Yapfast', '-srcfolder', str(stage),
                    '-fs', 'HFS+', '-format', 'UDZO', str(temporary)], check=True)
    subprocess.run(['hdiutil', 'verify', str(temporary)], check=True)
    shutil.move(temporary, output)
print(output)
