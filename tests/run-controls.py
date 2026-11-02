#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Negative controls for the tests.

Each control breaks the workaround on purpose, in a temporary copy, and the tests must then fail.
A control that passes means the tests no longer guard that behaviour.

usage: tests/run-controls.py <patched-kodi-directory>
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURES = os.path.join(HERE, 'fixtures')
CXX = os.environ.get('CXX', 'g++')

# (name, file, text to replace, replacement)
MUTATIONS = [
    ('late pictures restored in descending order', 'cpp',
     'const int64_t restored = m_missing.front();\n        m_missing.pop_front();',
     'const int64_t restored = m_missing.back();\n        m_missing.pop_back();'),
    ('never hold', 'cpp',
     'if (!missing.empty() && missing.size() <= MAX_MISSING && pts - m_lastOut <= MAX_GAP_US)',
     'if (false)'),
    ('no gap limit', 'cpp',
     'missing.size() <= MAX_MISSING && pts - m_lastOut <= MAX_GAP_US)',
     'missing.size() <= MAX_MISSING)'),
    ('no lower step limit', 'cpp',
     'step < MIN_STEP_US || step > MAX_STEP_US', 'step <= 0 || step > MAX_STEP_US'),
    ('no upper step limit', 'cpp',
     'step < MIN_STEP_US || step > MAX_STEP_US', 'step < MIN_STEP_US'),
    ('late label may pass the next queued picture', 'cpp',
     'return queued > m_heldPts && queued <= pts;', 'return false && queued == pts;'),
    ('picture without pts skips Abandon', 'cpp',
     '  Result result = Abandon();\n  result.Add(Source::CURRENT, 0);',
     '  Result result;\n  result.Add(Source::CURRENT, 0);'),
    ('Abandon keeps the held picture', 'cpp',
     '  if (m_holding)\n    ReleaseHeld(result, Event::GAVE_UP);\n  Disable();', '  Disable();'),
    ('Reset keeps the hold', 'cpp', '  m_holding = false;\n  m_heldPts = 0;', '  m_heldPts = 0;'),
    ('Disable ignored', 'cpp', '  m_disabled = true;\n  m_ledger.clear();', '  m_ledger.clear();'),
    ('no expiry', 'cpp', 'if (m_holding && nowMs - m_heldSinceMs > MAX_HOLD_MS)', 'if (false)'),
    ('exact label match only', 'cpp',
     'return queued >= pts - PTS_TOLERANCE_US && queued <= pts + PTS_TOLERANCE_US;',
     'return queued == pts;'),
    ('Apply hands over the second picture first', 'h',
     '    handOver(result.emit[0].source, result.emit[0].pts);\n'
     '    if (result.count > 1)\n'
     '      keep(result.emit[1].source, result.emit[1].pts, true);',
     '    if (result.count > 1)\n    {\n'
     '      handOver(result.emit[1].source, result.emit[1].pts);\n'
     '      keep(result.emit[0].source, result.emit[0].pts, true);\n    }\n'
     '    else\n      handOver(result.emit[0].source, result.emit[0].pts);'),
]


def build(src, binary):
    subprocess.run([CXX, '-std=c++17', '-O1', '-I' + src, os.path.join(HERE, 'test_reorder.cpp'),
                    os.path.join(src, 'MediaCodecOutputReorder.cpp'), '-o', binary], check=True)


SUMMARY = re.compile(r'^(\d+) checks, (\d+) failed$', re.M)


def expect_fail(name, cmd):
    """The tests must run to the end and report failed checks (exit 1). A crash, a missing fixture
    or any other error is not a valid control result."""
    run = subprocess.run(cmd, capture_output=True, text=True)
    summary = SUMMARY.search(run.stdout)
    first = next((l for l in run.stdout.splitlines() if l.startswith('FAIL')), '')
    if run.returncode == 0:
        print('CONTROL PASSED (bad):', name)
        return False
    if run.returncode != 1 or not summary or int(summary.group(2)) == 0 or not first:
        print('CONTROL BROKEN (exit %d, no failed-check summary):' % run.returncode, name)
        print((run.stdout + run.stderr)[-400:])
        return False
    print('control fails as it should:', name, '--', first[:120])
    return True


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    src = os.path.join(sys.argv[1], 'xbmc/cores/VideoPlayer/DVDCodecs/Video')
    if not os.path.exists(os.path.join(src, 'MediaCodecOutputReorder.cpp')):
        sys.exit(src + ' has no MediaCodecOutputReorder.cpp; is the patch applied?')

    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        real = os.path.join(tmp, 'real')
        build(src, real)
        ok &= expect_fail('behaviour before the fix (--passthrough)', [real, FIXTURES, '--passthrough'])

        for i, (name, ext, old, new) in enumerate(MUTATIONS):
            work = os.path.join(tmp, 'm%d' % i)
            os.mkdir(work)
            for f in ('MediaCodecOutputReorder.h', 'MediaCodecOutputReorder.cpp'):
                shutil.copy(os.path.join(src, f), work)
            path = os.path.join(work, 'MediaCodecOutputReorder.' + ext)
            text = open(path).read()
            if text.count(old) != 1:
                sys.exit('mutation target for "%s" not found exactly once' % name)
            open(path, 'w').write(text.replace(old, new))
            build(work, os.path.join(work, 't'))
            ok &= expect_fail(name, [os.path.join(work, 't'), FIXTURES])

        # omission: a fixture cut just before the first filmed event must fail, not pass vacuously
        cut = os.path.join(tmp, 'cut')
        os.mkdir(cut)
        lines = open(os.path.join(FIXTURES, 'filmed-run.txt')).read().splitlines(True)
        at = next(i for i, l in enumerate(lines) if l.rstrip().endswith(' O 12 112324000'))
        open(os.path.join(cut, 'filmed-run.txt'), 'w').writelines(lines[:at])
        shutil.copy(os.path.join(FIXTURES, 'timing-run.txt'), cut)
        ok &= expect_fail('fixture cut before the first filmed event', [real, cut])

    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
