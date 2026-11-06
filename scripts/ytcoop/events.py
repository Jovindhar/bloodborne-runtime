#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Yharnam Together's co-op map event files for this game (run.sh, online mode Yharnam Together).

Yharnam Together plays with copies of the map event files that its patcher
(party_boss_events.py, vendored from Yharnam Together v1.4.0 beside this file) makes from the
player's own game, as its Windows launcher does: party guests finish boss events, and every
player's events match (the peers compare their hashes). They are made once into OUT/dvdroot_ps4/
event, and again when the game's files or the patcher change. Prints OUT, a mod folder for
mods.py --layer.

  events.py GAME OUT
"""
import contextlib
import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import party_boss_events  # noqa: E402


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def current(events, game_events):
    """Whether `events` holds this patcher's files for these game files."""
    try:
        manifest = json.loads((events / party_boss_events.MANIFEST).read_text(encoding='utf-8'))
    except (OSError, ValueError):
        return False
    files = manifest.get('files') or {}
    if manifest.get('tool_sha256') != sha256(party_boss_events.__file__) or not files:
        return False
    for name, entry in files.items():
        source, output = game_events / name, events / name
        if not source.is_file() or not output.is_file():
            return False
        if sha256(source) != entry.get('input_sha256') or sha256(output) != entry.get('output_sha256'):
            return False
    return True


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    game, out = Path(sys.argv[1]), Path(sys.argv[2])
    game_events = game / 'dvdroot_ps4' / 'event'
    if not (game_events / 'common.emevd.dcx').is_file():
        sys.exit(f'Yharnam Together: no dvdroot_ps4/event/common.emevd.dcx in {game}: is it a complete dump?')
    if not current(out / 'dvdroot_ps4' / 'event', game_events):
        print('Yharnam Together: making the co-op event files from your game...', file=sys.stderr)
        out.parent.mkdir(parents=True, exist_ok=True)
        made = Path(tempfile.mkdtemp(prefix='.events-', dir=out.parent))
        try:
            # The patcher's report goes to the log, not to run.sh's answer.
            with contextlib.redirect_stdout(sys.stderr):
                party_boss_events.main([str(made / 'dvdroot_ps4' / 'event'), str(game_events)])
            if out.exists():
                shutil.rmtree(out)
            made.rename(out)
        except BaseException:
            shutil.rmtree(made, ignore_errors=True)
            raise
    print(out)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, party_boss_events.EventPatchError) as error:
        print(f'Yharnam Together: making the co-op event files failed: {error}', file=sys.stderr)
        sys.exit(1)
