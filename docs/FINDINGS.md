# Findings: why does ExoPlayer not show the fault?

A Kodi maintainer asked on [issue #29675](https://github.com/xbmc/xbmc/issues/29675) whether Kodi sets up or drives the decoder wrongly, since ExoPlayer does not show the problem. This page sums up what we tested to answer that, and where we stopped.

**Short answer:** we found no setup difference that explains it. One change in how Kodi polls the decoder halves how often the fault appears, but does not remove it. The reorder workaround in `patches/` is still the only thing that hides it fully.

## Setup

- **Device and content:** one Fire TV Stick 4K Max (2nd gen), `OMX.MTK.VIDEO.DECODER.HEVC`, Surface output, one 4K HDR10 HEVC title at 23.976 fps, the same one-minute scene every time (three minutes for the last comparison). Same display mode and tracks in every run.
- **Players:** Kodi 21.3 and Media3 (ExoPlayer) 1.11.1, both as lab builds that record every call at the MediaCodec boundary: inputs, outputs, releases, flushes.
- **Screening:** a parser marks a run SIGNATURE when output labels appear that were never queued, or go backwards, before a keyframe (the fault described in [HOW-IT-WORKS.md](HOW-IT-WORKS.md)). Otherwise the run is CLEAN.
- **Evidence:** timestamps only, no pixel comparison. Each experimental arm had at least 3 runs, interleaved with unchanged controls.

## Results

**Unmodified Kodi:** SIGNATURE in all 13 control runs. The fault is intermittent: its positions differ from run to run.

**Unmodified Media3:** CLEAN in every run, also when watched by eye.

**Kodi's traits moved into Media3 — all stayed CLEAN:**

| Kodi trait moved into Media3 | Runs |
|---|---|
| no max-width/height (so no MTK adaptive playback) | 6/6 CLEAN |
| app holds up to 5 decoded outputs, as Kodi does | 3/3 CLEAN |
| Kodi's exact input bytes | 3/3 CLEAN |
| shallow feeding: at most 5 inputs in the decoder (Media3 keeps 12–13) | 3/3 CLEAN |
| shallow feeding and held outputs together | 3/3 CLEAN |
| timestamps in Kodi's range | 3/3 CLEAN |
| Kodi's full codec configuration | 3/3 CLEAN |
| all of the above at once | 3/3 CLEAN |

**Media3's traits moved into Kodi — no reliable effect:**

- adding max-width/height: 6/6 SIGNATURE;
- operating rate 23.976: no reduction against interleaved controls;
- Media3's timestamp range: 2 of 3 still SIGNATURE.

**Polling without waiting — a partial mitigation:**

Kodi waits up to 10 ms for decoder output on every poll; Media3 (in synchronous mode) does not wait. With the wait set to 0, Kodi keeps the decoder fuller: about 6 inputs in flight instead of 4.
- First comparison: 6/6 CLEAN against 6/6 SIGNATURE. It did not replicate that cleanly afterwards.
- Predeclared, randomised comparison, 16 runs of three minutes each: **half as many fault events per run** (1.375 against 2.75, one-sided p = 0.04). Still, 6 of 8 runs with the change showed the fault.

That change is in [`experimental/`](../experimental/). It is **not** part of the workaround.

## What we think it means

- Every codec-facing difference we could find between the two players went into Media3 without bringing the fault. A wrong decoder setup in Kodi, visible at the MediaCodec API, is therefore unlikely.
- What we could not move is the **timing** of Kodi's calls: which thread calls the decoder when, around keyframes, and how Kodi's player and renderer schedule that. Another commenter on the issue reproduced their variant in a plain MediaCodec test app with paced output, outside Kodi, and it disappeared when they drained output as fast as possible. Together with the polling result, a timing-dependent race in the decoder is our leading guess. It is not proven.
- We tried to replay Kodi's recorded call timing in a standalone app. The decoder's own wait times made the schedule impossible to hold closely enough, so we stopped and count those runs as no evidence either way.

## Limits

- One title, one device, one firmware.
- Timestamp evidence only. The polling change was not checked for picture quality, A/V sync, seek, pause or stop behaviour, power use, or other decoders.
- The polling change was never tested together with the reorder workaround.
- We do not know the decoder's internal state; "inputs in flight" is counted at the app boundary.

## Where we left it

The reorder workaround stays the recommendation. The polling change is published so others can try it or build on it. Whether combining both helps with anything the workaround alone gets wrong is an open question we did not test.
