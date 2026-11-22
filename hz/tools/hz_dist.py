#!/usr/bin/env python3
"""The Windows package: `make dist`'s second half, after the build.

    hz_dist.py EXE ASSETS OUTDIR --map LINK.map --licences DIR
               [--q-licence FILE]

Makes OUTDIR/HerzogZwei/ afresh: EXE as HerzogZwei.exe, ASSETS as
assets/, shaders/'s SPIR-V and DXIL (Vulkan's and Direct3D 12's; Metal's
stay out), src/platform/win/README.txt, licenses/: SDL3's, Nuked OPN2's
(its notice, ym3438.c's head, and the LGPL 2.1's text,
src/audio/LICENSE.ym3438), Q's (--q-licence, when the build has Q), the
MinGW-w64 runtime's, winpthreads' and libiconv's (when the link map says
they were linked; DIR is MSYS2's share/licenses), and SOURCE.txt, the
commit the package was built from and the libraries' MSYS2 packages; and
source.zip, `git archive` of HEAD less the working notes (NOT_SOURCE), as
the Phantasy Star ports' packages carry theirs: the exe links Nuked OPN2
in statically, and the LGPL asks for the source to go with it. Then
OUTDIR/HerzogZwei-windows-COMMIT.zip of the folder. Every entry of both
zips is dated the commit's time in UTC (a zip's times are local: beside
a link stamp, a file's own time would give away the builder's offset).

Every text file the package carries, source.zip's included, has the
maintainer's name put as "the author" (scrub), as the Phantasy Star
ports' do.

(Session 56) servers.txt, the network game's known servers, goes beside
the game when the tree's top has one: it is git-ignored, so a server's
address is the packager's to give and never in the source.

First it looks through EXE for the user name and home folder of the
machine that built it (a debug build carries its folder's path in every
line's record): found, nothing is packed and it exits 1.

(Session 61) Last, the packed assets/ against HEAD's, byte for byte: the
network game's id hashes every byte of the assets the game reads, so a
package whose assets differ from the commit's is another game to every
build of it, and to the server's AI. Until then every package had
shipped 48 of them with CRLF line ends, from a Windows checkout made
before .gitattributes said LF. Any file not the commit's, and no zip is
made (an old one of the same name is removed) and it exits 1."""

import argparse
import datetime
import fnmatch
import io
import os
import re
import shutil
import subprocess
import sys
import zipfile

NAME = 'HerzogZwei'

# The working notes, which are not the port's source: left out of
# source.zip.
NOT_SOURCE = ('CLAUDE.md', 'HANDOVER.md', 'unitai_notes.md',
              'tools/names/*_notes.txt')

# The maintainer's name, in the notes and comments the package carries,
# and what it becomes.
MAINTAINER = 'The owner'
MAINTAINER_AS = 'the author'


def git(*args):
    try:
        return subprocess.run(('git',) + args, capture_output=True,
                              text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ''


def private_words():
    """The builder's user name and home folder, each as the exe might
    hold it."""
    words = set()
    for var in ('USERNAME', 'USER', 'LOGNAME'):
        v = os.environ.get(var, '')
        if len(v) >= 4:
            words.add(v)
    for var in ('HOME', 'USERPROFILE'):
        v = os.environ.get(var, '').rstrip('/\\')
        if v:
            words.update((v, v.replace('\\', '/'), v.replace('/', '\\')))
            base = os.path.basename(v.replace('\\', '/'))
            if len(base) >= 4:
                words.add(base)
    return words


def holds_private(path):
    data = open(path, 'rb').read().lower()
    found = 0
    for w in private_words():
        for enc in ('ascii', 'utf-16-le'):
            try:
                found += data.count(w.lower().encode(enc))
            except UnicodeEncodeError:
                pass
    return found


def nuked_notice():
    """ym3438.c's opening comment: Nuked OPN2's copyright and licence."""
    text = open('src/audio/ym3438.c', encoding='utf-8').read()
    end = text.find('*/')
    return text[:end + 2] + '\n' if text.startswith('/*') and end > 0 else ''


# A sentence's start before the name: a line's start (past a comment's
# marks), or after . ! ? : or an opening quote or bracket.
_SENTENCE = re.compile(r"(^[\s#*/;>|-]*|[.!?:][\s\"']*|[(\"'\[]\s*)$")


def scrub(text):
    """The name as MAINTAINER_AS -- "The author" where it begins a
    sentence -- in any of its forms (its 's, a colon after it). Returns
    the text and how many were put."""
    count = 0

    def put(m):
        nonlocal count
        count += 1
        before = text[max(0, m.start() - 40):m.start()].split('\n')[-1]
        word = MAINTAINER_AS
        if _SENTENCE.search(before):
            word = word[0].upper() + word[1:]
        return word
    return re.sub(r'\b%s\b' % MAINTAINER, put, text), count


def stamped(info, stamp):
    """A copy of a zip entry, a file's deflated, dated stamp: its name, its
    mode and the system that made it kept; its own time and extra fields
    (git archive's carry the time again) not."""
    new = zipfile.ZipInfo(info.filename, stamp)
    new.compress_type = (zipfile.ZIP_STORED if info.is_dir()
                         else zipfile.ZIP_DEFLATED)
    new.create_system = info.create_system
    new.external_attr = info.external_attr
    return new


def source_zip(dest, stamp):
    """git archive of HEAD under herzog-zwei/, less NOT_SOURCE, every text
    file scrubbed, every entry dated stamp. Returns how many names were
    put."""
    r = subprocess.run(['git', 'archive', '--format=zip',
                        '--prefix=herzog-zwei/', 'HEAD'], capture_output=True)
    if r.returncode != 0:
        raise SystemExit('hz_dist: git archive failed: '
                         + r.stderr.decode(errors='replace'))
    put = 0
    with zipfile.ZipFile(io.BytesIO(r.stdout)) as src, \
            zipfile.ZipFile(dest, 'w', zipfile.ZIP_DEFLATED,
                            compresslevel=9) as out:
        for info in src.infolist():
            rel = info.filename[len('herzog-zwei/'):]
            if any(fnmatch.fnmatch(rel, p) for p in NOT_SOURCE):
                continue
            data = src.read(info)
            if not info.is_dir():
                try:
                    text, n = scrub(data.decode('utf-8'))
                except UnicodeDecodeError:
                    n = 0
                if n:
                    data = text.encode('utf-8')
                    put += n
            out.writestr(stamped(info, stamp), data)
    return put


def assets_unlike_commit(packed):
    """The files of `packed` (the package's assets/) not as HEAD's assets/
    has them, each with how: missing, extra, CRLF line ends, differs."""
    r = subprocess.run(['git', 'archive', '--format=zip', 'HEAD', 'assets'],
                       capture_output=True)
    if r.returncode != 0:
        return ['git archive of assets/ failed: '
                + r.stderr.decode(errors='replace').strip()]
    want = {}
    with zipfile.ZipFile(io.BytesIO(r.stdout)) as z:
        for info in z.infolist():
            if not info.is_dir():
                want[info.filename[len('assets/'):]] = z.read(info)
    have = {}
    for root, dirs, names in os.walk(packed):
        for n in names:
            path = os.path.join(root, n)
            with open(path, 'rb') as f:
                have[os.path.relpath(path, packed).replace('\\', '/')] = \
                    f.read()
    unlike = []
    for rel in sorted(set(want) | set(have)):
        if rel not in have:
            unlike.append('%s: not packed' % rel)
        elif rel not in want:
            unlike.append('%s: not in the commit' % rel)
        elif have[rel] != want[rel]:
            crlf = have[rel].replace(b'\r\n', b'\n') == want[rel]
            unlike.append('%s: %s' % (rel, 'CRLF line ends' if crlf
                                      else 'differs'))
    return unlike


def licences(a, dest):
    os.makedirs(dest)
    shutil.copy(os.path.join(a.licences, 'SDL3', 'LICENSE.txt'),
                os.path.join(dest, 'SDL3.txt'))
    with open(os.path.join(dest, 'Nuked-OPN2.txt'), 'w', encoding='utf-8',
              newline='\r\n') as f:
        f.write(nuked_notice() + '\nThe GNU LGPL 2.1\'s text: LGPL-2.1.txt '
                'beside this. Its source, with the game\'s: source.zip, '
                'herzog-zwei/src/audio/ym3438.c.\n')
    shutil.copy('src/audio/LICENSE.ym3438',
                os.path.join(dest, 'LGPL-2.1.txt'))
    if a.q_licence:
        shutil.copy(a.q_licence, os.path.join(dest, 'Q.txt'))
    linked = open(a.map, encoding='utf-8', errors='replace').read()
    if 'libmingwex.a' in linked or 'libmingw32.a' in linked:
        shutil.copy(os.path.join(a.licences, 'crt',
                                 'COPYING.MinGW-w64-runtime.txt'),
                    os.path.join(dest, 'MinGW-w64-runtime.txt'))
    # winpthreads comes as libpthread.a, its members libwinpthread_la-*.o.
    if 'libwinpthread' in linked:
        shutil.copy(os.path.join(a.licences, 'winpthreads', 'COPYING'),
                    os.path.join(dest, 'winpthreads.txt'))
    # SDL3 converts its strings through libiconv (LGPL 2.1).
    if 'libiconv.a(' in linked:
        shutil.copy(os.path.join(a.licences, 'libiconv', 'COPYING.LIB'),
                    os.path.join(dest, 'libiconv.txt'))
    commit = git('rev-parse', 'HEAD') or 'unknown'
    dirty = git('status', '--porcelain', '--untracked-files=no')
    with open(os.path.join(dest, 'SOURCE.txt'), 'w', newline='\r\n') as f:
        f.write('Built from the commit %s%s\n' % (
            commit, ', with changes not yet committed' if dirty else ''))
        # The libraries linked in, by their MSYS2 packages (the sources:
        # packages.msys2.org), when pacman answers.
        try:
            p = subprocess.run(
                ['pacman', '-Q'] + ['mingw-w64-ucrt-x86_64-' + n for n in
                                    ('sdl3', 'libiconv', 'libwinpthread',
                                     'gcc', 'crt')],
                capture_output=True, text=True)
            got = [line for line in p.stdout.splitlines() if line.strip()]
        except OSError:
            got = []
        if got:
            f.write('\nLinked from MSYS2 UCRT64 packages (their sources at '
                    'packages.msys2.org):\n')
            f.write(''.join('    %s\n' % line for line in got))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('exe')
    ap.add_argument('assets')
    ap.add_argument('outdir')
    ap.add_argument('--map', required=True)
    ap.add_argument('--licences', required=True)
    ap.add_argument('--q-licence')
    a = ap.parse_args()

    found = holds_private(a.exe)
    if found:
        print('hz_dist: %s holds the user name or home folder of the '
              'machine that built it (%d places): nothing packed'
              % (a.exe, found))
        return 1

    top = os.path.join(a.outdir, NAME)
    shutil.rmtree(top, ignore_errors=True)
    os.makedirs(top)
    shutil.copy(a.exe, os.path.join(top, NAME + '.exe'))
    shutil.copytree(a.assets, os.path.join(top, 'assets'))
    os.makedirs(os.path.join(top, 'shaders'))
    for f in sorted(os.listdir('shaders')):
        if f.endswith(('.spv', '.dxil')):
            shutil.copy(os.path.join('shaders', f),
                        os.path.join(top, 'shaders', f))
    readme = open('src/platform/win/README.txt', encoding='utf-8').read()
    with open(os.path.join(top, 'README.txt'), 'w', encoding='utf-8',
              newline='\r\n') as f:
        f.write(readme)
    licences(a, os.path.join(top, 'licenses'))
    if os.path.exists('servers.txt'):
        shutil.copy('servers.txt', os.path.join(top, 'servers.txt'))

    short = git('rev-parse', '--short', 'HEAD') or 'unknown'
    when = git('log', '-1', '--format=%ct')
    stamp = datetime.datetime.fromtimestamp(
        int(when) if when else 315532800, datetime.timezone.utc)
    stamp = max(stamp.timetuple()[:6], (1980, 1, 1, 0, 0, 0))
    put = source_zip(os.path.join(top, 'source.zip'), stamp)
    # The name, in everything else the package carries as text.
    for root, dirs, names in os.walk(top):
        for n in names:
            path = os.path.join(root, n)
            if n.endswith(('.exe', '.zip')):
                continue
            try:
                with open(path, encoding='utf-8', newline='') as f:
                    text, k = scrub(f.read())
            except (UnicodeDecodeError, OSError):
                continue
            if k:
                with open(path, 'w', encoding='utf-8', newline='') as f:
                    f.write(text)
                put += k
    zpath = os.path.join(a.outdir, '%s-windows-%s.zip' % (NAME, short))
    # (Session 61) The commit's assets, or the network game is another.
    unlike = assets_unlike_commit(os.path.join(top, 'assets'))
    if unlike:
        if os.path.exists(zpath):
            os.remove(zpath)
        print('hz_dist: %d files of the packed assets/ are not the '
              'commit\'s, so its network game would be another game to '
              'every build of %s and to the server\'s AI: nothing packed'
              % (len(unlike), short))
        for u in unlike[:12]:
            print('    ' + u)
        if len(unlike) > 12:
            print('    ... and %d more' % (len(unlike) - 12))
        if any(u.endswith('CRLF line ends') for u in unlike):
            print('  CRLF: a checkout made before .gitattributes said LF '
                  '(`git ls-files --eol | grep w/crlf`); made LF, they are '
                  'the commit\'s blobs again')
        return 1
    files = []
    for root, dirs, names in os.walk(top):
        dirs.sort()
        files += [os.path.join(root, n) for n in sorted(names)]
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED,
                         compresslevel=9) as z:
        for path in files:
            info = zipfile.ZipInfo(
                os.path.relpath(path, a.outdir).replace('\\', '/'), stamp)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            with open(path, 'rb') as f:
                z.writestr(info, f.read())
    print('%s/: %d files; %s: %.1f MB; the name put as "%s" %d times'
          % (top, len(files), zpath, os.path.getsize(zpath) / 1e6,
             MAINTAINER_AS, put))
    return 0


if __name__ == '__main__':
    sys.exit(main())
