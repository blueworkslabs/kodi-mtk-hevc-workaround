# kodi-mtk-hevc-workaround

An **unofficial** patch for Kodi 21.3 on Android. It works around a MediaTek HEVC decoder that sometimes shows a few frames out of order just before a keyframe. You see a short jerk: the picture jumps ahead and back.

This is not part of Kodi, it is not endorsed by Team Kodi, and it is not a fix for the decoder. It corrects the decoder's output at the one layer we can change.

## Is this your problem?

All of these should hold:

- Kodi on Android, with video through MediaCodec **Surface** rendering (the default).
- The HEVC decoder in the log is `OMX.MTK.VIDEO.DECODER.HEVC`.
- You see brief back-and-forth jerks ("bounces") in HEVC video. They hit the same scenes, but not on every replay. Network buffering, refresh-rate switching and subtitles have been ruled out.
- A Kodi debug log with the video component shows the decoder's output timestamps jumping about 125 ms forward, then rising in steps of about 16 ms that never appear among the timestamps queued to the decoder. See [docs/HOW-IT-WORKS.md](docs/HOW-IT-WORKS.md).

It was found and tested on a **Fire TV Stick 4K Max (2nd gen)** with 23.976 fps HDR10 HEVC. Nothing else has been tested; see [Limits](#limits).

## What it does

- Kodi records every timestamp it queues to the decoder.
- If a frame comes out while earlier queued frames are still missing, Kodi holds that frame.
- The late frames arrive with invented labels. Kodi gives them their real timestamps back, in ascending order, then releases the held frame. On screen you get A B C D E again instead of A D B C E.
- Anything that does not match the observed pattern is passed on in the decoder's order, so a mismatch falls back to today's behaviour.

The workaround switches on only for that decoder with Surface output and plain HEVC. Dolby Vision content is left alone.

## Contents

| Path | What |
|---|---|
| `patches/0001-*.patch` | The workaround, against Kodi `21.3-Omega` (`a3a448d26b8d560a65655dab2cd122994dc4e146`). It adds `MediaCodecOutputReorder.{h,cpp}` and changes `DVDVideoCodecAndroidMediaCodec.{h,cpp}` and `CMakeLists.txt` in `xbmc/cores/VideoPlayer/DVDCodecs/Video/`. |
| `scripts/prepare-source.sh` | Clones Kodi 21.3-Omega, checks the commit and applies the patch. |
| `tests/` | Unit and replay tests for the reorder logic: captured decoder sequences, hand-written lifecycle cases, and negative controls. |
| `tools/extract_fixture.py` | Turns a Kodi debug log into a replay fixture. |
| `docs/` | [How it works](docs/HOW-IT-WORKS.md), [building](docs/BUILD.md), [installing and rolling back](docs/INSTALL.md), [findings from comparing with ExoPlayer](docs/FINDINGS.md). |
| `experimental/` | A measured experiment, not part of the workaround: polling the decoder without waiting halves how often the fault appears. See [experimental/README.md](experimental/README.md). |

## Quick start (source)

```sh
scripts/prepare-source.sh ../kodi-21.3-mtk          # clone, verify, patch
tests/run-tests.sh ../kodi-21.3-mtk                 # 57 checks, needs g++ with ASan/UBSan
tests/run-controls.py ../kodi-21.3-mtk              # each control must fail
```

Then build the APK as described in [docs/BUILD.md](docs/BUILD.md), and read [docs/INSTALL.md](docs/INSTALL.md) **before** installing anything. A self-built Kodi cannot be installed over the official one, and moving your library needs care.

There are no prebuilt APKs here. You build and sign your own.

## Limits

- **Coverage:** one device family, one decoder and 23.976 fps content only.
  - Other frame rates, other MediaTek devices and other Fire OS versions are untested.
  - Dolby Vision is excluded. H.264 and other codecs never reach this code.
- **Bounds:** the thresholds come from captured cases.
  - One early frame ahead of at most 3 late frames.
  - Late labels are 12–20 ms apart.
  - The early frame comes at most 200 ms after the previous one.
  - A held frame expires on the next codec poll after more than 250 ms. This is not a timer-driven wall-clock guarantee while polling is paused.
  - It does not read keyframe flags or the bitstream.
- **Evidence:** the tests check the decision logic and the order in which frames are handed over. They do not exercise Android's MediaCodec, buffer ownership or the renderer. Those were only checked by reading the code and by watching real playback.
- **Draining:** at the end of a stream the held frame is handed over before Kodi drains the decoder. Kodi's own drain can still drop frames that are queued for display. This is not lossless end-of-stream rendering.
- **What was observed:** on the test device, the bouncing in the tested scene was gone, and the log showed 25 completed recoveries and no fallbacks. That is one person's viewing, not a full A/V-sync or every-title verification.
- **Scope:** Kodi 21.3 only. Porting to later releases is up to you.

## Background

[Kodi issue #29675](https://github.com/xbmc/xbmc/issues/29675) describes a similar symptom on another MediaTek Fire TV device and proposes swapping adjacent frames whose timestamps go backwards. That patch did not help here: the decoder's labels never go backwards, they only stop matching the frames. This patch takes a different approach and does not include that code.

## Why not prevent it instead?

ExoPlayer plays the same file on the same decoder without the fault. We moved every codec-facing Kodi trait we could find into ExoPlayer and it stayed clean, so the cause is most likely timing inside the decoder, not a wrong setup in Kodi. One Kodi-side change halves the fault rate but does not remove it. Details: [docs/FINDINGS.md](docs/FINDINGS.md).

## License

The patch modifies Kodi, which is licensed under GPL-2.0-or-later. Everything in this repository is distributed under the **GNU General Public License v2.0 or later**; see [LICENSE](LICENSE) and [NOTICE.md](NOTICE.md). If you distribute a binary built from it, the GPL requires you to offer the matching source, including this patch.
