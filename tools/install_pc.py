#!/usr/bin/env python3
"""Installs your Super Mario Galaxy for PCGalaxyVR on this computer.

Converts your own disc into the folder the game reads,
~/.local/share/PCGalaxyVR/game (with $XDG_DATA_HOME set, that folder
instead):

    python3 tools/install_pc.py "Super Mario Galaxy.rvz"
    python3 tools/install_pc.py ~/extracted-disc      # already extracted

A disc image (ISO, RVZ, WBFS...) is first extracted with Dolphin's
command-line tool: dolphin-tool on the PATH (Dolphin's own builds and most
distributions' packages have it), the Flatpak's, or --dolphin-tool PATH.
An extracted disc is the folder holding DATA, or DATA itself (sys and files).
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
DISC_EXTENSIONS = {'.iso', '.rvz', '.wbfs', '.gcz', '.wia', '.ciso'}
FLATPAK_DOLPHIN = 'org.DolphinEmu.dolphin-emu'


class InstallError(Exception):
    pass


def data_dir():
    base = os.environ.get('XDG_DATA_HOME', '')
    root = Path(base) if base.startswith('/') else Path.home() / '.local' / 'share'
    return root / 'PCGalaxyVR'


def game_partition(folder):
    """The extracted disc's game partition (the folder with sys and files)."""
    root = Path(folder).expanduser().resolve()
    for candidate in (root, root / 'DATA'):
        if ((candidate / 'sys' / 'fst.bin').is_file()
                and (candidate / 'sys' / 'main.dol').is_file()
                and (candidate / 'files' / 'ObjectData').is_dir()
                and (candidate / 'files' / 'StageData').is_dir()):
            return candidate
    raise InstallError('%s is not an extracted Super Mario Galaxy disc: it needs sys/main.dol, sys/fst.bin '
                       'and the game files (in Dolphin, use Extract Entire Disc).' % root)


def dolphin_tool_command(explicit):
    """The command that runs Dolphin's command-line tool."""
    if explicit:
        return [explicit]
    for name in ('dolphin-tool', 'DolphinTool'):
        found = shutil.which(name)
        if found:
            return [found]
    if shutil.which('flatpak'):
        listed = subprocess.run(['flatpak', 'info', FLATPAK_DOLPHIN], stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL).returncode == 0
        if listed:
            return ['flatpak', 'run', '--command=dolphin-tool', '--filesystem=host', FLATPAK_DOLPHIN]
    raise InstallError('Dolphin\'s command-line tool (dolphin-tool) was not found. Install Dolphin '
                       '(https://dolphin-emu.org), give its path with --dolphin-tool, or extract the disc '
                       'in Dolphin (Properties, Filesystem, Extract Entire Disc) and pass that folder.')


def extract_disc(image, destination, tool):
    print('extracting %s' % image, flush=True)
    result = subprocess.run(tool + ['extract', '-i', str(image), '-o', str(destination), '--gameonly', '--quiet'],
                            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, errors='replace')
    if result.returncode:
        raise InstallError('Dolphin could not extract %s: is the file complete and readable?\n%s'
                           % (image, result.stdout.strip()[-4000:]))
    # Dolphin may report success after an incomplete extraction.
    return game_partition(destination)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('disc', help='your disc image (ISO, RVZ, WBFS...) or extracted disc folder')
    ap.add_argument('--dest', type=Path, help='where to put the converted game (default: %s)' % (data_dir() / 'game'))
    ap.add_argument('--without-movies', action='store_true',
                    help='leave out the prologue and ending movies (2.3 GB less; they play as a black screen)')
    ap.add_argument('--dolphin-tool', help="path of Dolphin's command-line tool")
    ap.add_argument('--force', action='store_true', help='replace an existing converted game without asking')
    args = ap.parse_args()

    dest = (args.dest or data_dir() / 'game').expanduser().resolve()
    source = Path(args.disc).expanduser().resolve()
    if not source.exists():
        raise InstallError('%s does not exist' % source)
    if dest.exists() and not args.force:
        if not sys.stdin.isatty() or input('%s already holds a converted game. Replace it? [y/N] ' % dest).strip().lower() != 'y':
            raise InstallError('nothing changed (--force replaces it)')
    dest.parent.mkdir(parents=True, exist_ok=True)

    # Work next to the destination, so the result moves into place at once.
    with tempfile.TemporaryDirectory(prefix='.install-', dir=str(dest.parent)) as work:
        work = Path(work)
        if source.is_dir():
            game = game_partition(source)
        elif source.suffix.lower() in DISC_EXTENSIONS:
            game = extract_disc(source, work / 'extracted', dolphin_tool_command(args.dolphin_tool))
        else:
            raise InstallError('choose your disc image (ISO, RVZ, WBFS...) or an extracted disc folder')

        cooked = work / 'cooked'
        command = [sys.executable, str(HERE / 'cook' / 'cook.py'), str(game), str(cooked)]
        if not args.without_movies:
            command.append('--with-movies')
        print('converting (a few minutes)', flush=True)
        if subprocess.run(command).returncode:
            raise InstallError('the conversion failed (see above)')
        if dest.exists():
            shutil.rmtree(dest)
        cooked.rename(dest)
    print('done: the game is in %s. Start your OpenXR runtime (SteamVR, Monado, WiVRn...), then pcgalaxyvr.' % dest)


if __name__ == '__main__':
    try:
        main()
    except InstallError as error:
        sys.exit('install_pc: %s' % error)
    except KeyboardInterrupt:
        sys.exit(1)
