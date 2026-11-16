#!/usr/bin/env python3
"""Read one release's notes from the changelog."""
import pathlib
import sys

version = sys.argv[1]
sections = pathlib.Path('CHANGELOG.md').read_text().split('\n## ')
for section in sections[1:]:
    heading, _, body = section.partition('\n')
    if heading.split(' - ')[0] == version:
        print('Open the DMG and drag Yapfast into Applications. Apple Silicon, macOS 13+.\n')
        print(body.strip())
        print('\nAd hoc signed; not notarized by Apple. Calls and screen sharing still need validation.')
        print('\nBased on [yapfast/yapfast](https://github.com/yapfast/yapfast), under GPL-3.0.')
        break
else:
    raise SystemExit(f'No changelog entry for {version}')
