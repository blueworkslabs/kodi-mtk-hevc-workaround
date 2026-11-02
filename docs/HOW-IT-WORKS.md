# How it works

## The fault

A decoder outputs frames in presentation order. Near some keyframes, `OMX.MTK.VIDEO.DECODER.HEVC` on the test device broke that rule. Take the last frames before a keyframe in **presentation order**, A B C D, with E the keyframe. In decode order, D was queued before B and C.

- **Shown:** `A D B C E` — one frame forward, two back, then on.
- **Labels Kodi received:** `A`, `D` (its real timestamp), then `D+16 ms`, `D+32 ms`, then `E`.
- **The real timestamps** of B and C never came back from the decoder.

Two things kept this hidden:

- The labels never go backwards, so a check for timestamps that decrease (for example the patch in Kodi issue #29675) never fires.
- Buffer levels look normal, so network or cache diagnostics show nothing either.

## How it was established

1. **Compare decoder input and output timestamps** in a Kodi debug log, with the `video` log component on. Some output timestamps never appear among the queued input timestamps, and some input timestamps never come back. Each time this showed up as a jump of about +125 ms followed by +16 ms steps.
2. **Find where it happens.** The source had a closed GOP with an IDR about every second and hierarchical B-frames. Every anomaly sat on the last frames before an IDR, but only some IDRs were affected (4 of about 32 in one 35-second window). That is consistent with timing inside the decoder; the exact firmware mechanism is unproven.
3. **Prove what is on screen.** Timestamps alone cannot tell "right frames, wrong timing" from "frames in the wrong order". A phone filmed the TV at 120 fps, and each filmed frame was matched against a reference software decode of the same file: perspective-corrected, compared on high-pass detail, in two separate image regions. Two independent events showed `A D B C E` in the same run whose log had the signature. A clean diagnostic run without the signature proves nothing, because the fault is intermittent.

The fixtures in `tests/fixtures/` come from such runs (decoder events only, shifted timestamps). The expected frame order in the tests comes from the filmed frames, not from the code under test.

## The workaround

`CMediaCodecOutputReorder` is plain C++ with no Android dependencies. It works in the decoder's timebase (`presentationTimeUs`).

1. **Ledger.** Every timestamp actually queued to the decoder is recorded; a retry that queued nothing is not. A reported timestamp within ±2 µs of a queued one counts as that frame, which covers the decoder's rounding.
2. **Hold.** When a frame comes out with a queued timestamp, Kodi checks whether queued frames that belong between the last handed-out frame and this one are still missing. They can only be late. If 1–3 are missing and the frame comes at most 200 ms after the last one, Kodi holds it (D).
3. **Recognise the late frames.** A following frame is taken as late only if its label meets all three conditions:
   - it was never queued;
   - it is 12–20 ms above the previous label (the decoder uses 16 ms);
   - it is below the next queued frame after D.

   Such a frame gets the earliest missing timestamp. A late frame that kept its real timestamp is accepted as well.
4. **Release.** When no frames are missing any more, D is handed over after them.
5. **Fall back.** If the next frame does not fit, D is handed over first, then that frame unchanged, which is the decoder's own order.
   - A frame without a timestamp, a drain, or an input without a timestamp turns reordering off until the next flush.
   - Expiry is checked by `GetPicture()`: the next codec poll after more than 250 ms hands D over. There is no independent timer, so a pause or stalled polling can keep it held longer in wall-clock time.
   - At end of stream D is handed over.
   - On a flush or seek D is dropped without being shown, like every other frame then.

Only one frame is ever held. The late frames go out as soon as they arrive, with their real (earlier) timestamps, so Kodi's player schedules them normally. D waits roughly one to two frame times.

## Gate

`CDVDVideoCodecAndroidMediaCodec::Open()` creates the guard only if **all** of these hold:

- Surface rendering (`videoplayer.usemediacodecsurface`, the default);
- mime `video/hevc`;
- decoder name starting with `OMX.MTK.VIDEO.DECODER.HEVC`;
- the stream is **not** signalled as Dolby Vision. Dolby Vision is excluded even when it is decoded by the HEVC decoder; it was not part of the captured evidence.

The log says `keyframe output reorder enabled`, or `keyframe output reorder off for Dolby Vision`. Every other decoder runs Kodi's original code path.

## Integration in Kodi

`DVDVideoCodecAndroidMediaCodec` keeps one extra `VideoPicture` slot for the held frame, or for a frame that waits one call to be handed over. Every decision of the guard is carried out by `CMediaCodecOutputReorder::Apply()`, which decides what goes out now and what waits. The same function is used by the tests' fake codec. Buffer ownership follows Kodi's existing rules:

- **Handing over a frame** releases the caller's previous buffer exactly once.
- **Reset and dispose** release the held buffer to the decoder without rendering, before the decoder is flushed.
- **Drain:** before `SignalEndOfStream()` releases every outstanding output buffer, the held frame is handed over and reordering stops.

## Logging

At INFO level:

- `keyframe output reorder enabled`
- `put late pictures back before pts:…` — one per corrected event
- `handing over held pts:… unchanged (…)` — a fallback or expiry

DEBUG adds one line per hold and per restored frame.

## What the tests prove, and what they do not

**`tests/run-tests.sh` (57 checks):**

- **Captured runs:** two decoder sequences replayed through a fake codec that uses the real `Apply()`. Expected results:
  - the two filmed events come out in filmed order with their buffers;
  - every substituted label gets back exactly the missing timestamp;
  - frames leave in presentation order, each exactly once;
  - nothing changes outside the events;
  - no false hold in about 5,300 frames.
- **Hand-written cases:**
  - the pattern itself;
  - a clean stream;
  - flush, end of stream, drain and expiry;
  - an unrelated far-away label;
  - a near-repeat of D (+1/+2 µs);
  - a too-large step, and a label past the next real frame;
  - a long gap;
  - too many missing frames;
  - duplicate input timestamps;
  - a frame without a timestamp (order `A D X E`);
  - a dropped late frame.

**`tests/run-controls.py`:** the behaviour before the fix, 13 deliberate breaks, and a fixture cut before the first filmed event. Each must make the tests fail.

**Not covered:**

- Android's MediaCodec, real buffer reference counts, the renderer and A/V sync. These were checked only by reading the code and by real playback on the one test device.
