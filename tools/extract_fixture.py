#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Turn a Kodi debug log into a replay fixture for the reorder guard.

Needs debug logging with the video component enabled, so that CDVDVideoCodecAndroidMediaCodec logs
AddData and GetPicture. Review the output before sharing it: it contains your stream's timestamps.
Use a log from a build without the workaround: with it, GetPicture logs the corrected pts.

Keeps only decoder events, one per line, with the log wall time in milliseconds:
  <ms> I <pts_us>          AddData that queued a picture (lines with indexBuffer:-1 queued nothing)
  <ms> O <buffer> <pts_us> GetPicture (picture taken from the decoder, label as reported)
  <ms> R                   Reset (flush, e.g. seek)
  <ms> X                   decoder opened or disposed (new instance)
  <ms> S <shift_us>        dts shift of the epoch: decoder pts + shift = VideoPlayer pts
pts are converted to the decoder timebase the codec uses (presentationTimeUs): the VideoPlayer pts
minus the epoch's dts shift, which is the dts of the first queued picture after open or flush.
"""
import re
import sys
from datetime import datetime

NOPTS = 1e18  # DVD_NOPTS_VALUE prints as a huge number
TS = r'^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3}) '
PATTERNS = [
    ('I', re.compile(TS + r'.*CDVDVideoCodecAndroidMediaCodec::AddData dts:(\S+) pts:(-?[\d.]+) sz:\d+ indexBuffer:(?!-1)\d+')),
    ('O', re.compile(TS + r'.*CDVDVideoCodecAndroidMediaCodec::GetPicture index: (\d+), pts:(-?[\d.]+)')),
    ('R', re.compile(TS + r'.*CDVDVideoCodecAndroidMediaCodec::Reset Current state')),
    ('X', re.compile(TS + r'.*CDVDVideoCodecAndroidMediaCodec::(?:Dispose$|Open Using codec)')),
]


def ms(stamp):
    return int(datetime.strptime(stamp, '%Y-%m-%d %H:%M:%S.%f').timestamp() * 1000)


def main(path):
    shift = None
    for line in open(path, errors='replace'):
        line = line.rstrip('\n')
        for kind, pattern in PATTERNS:
            m = pattern.search(line)
            if not m:
                continue
            t = ms(m.group(1))
            if kind == 'I':
                dts, pts = float(m.group(2)), float(m.group(3))
                if shift is None:
                    shift = 0.0 if abs(dts) >= NOPTS else dts
                    print(t, 'S', repr(shift))
                print(t, 'I', int(pts - shift))  # int64_t conversion truncates like the codec
            elif kind == 'O':
                label = float(m.group(3))
                print(t, 'O', m.group(2), round(label - (shift or 0.0)))
            else:
                shift = None
                print(t, kind)
            break


if __name__ == '__main__':
    main(sys.argv[1])
