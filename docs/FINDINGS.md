# Findings: why does ExoPlayer not show the fault?

A Kodi maintainer asked on [issue #29675](https://github.com/xbmc/xbmc/issues/29675) whether Kodi sets up or drives the decoder wrongly, since ExoPlayer does not show the problem. This page sums up what we tested to answer that, and where we stopped.

**Short answer:** the specific setup and buffer-handling differences we tested did not explain the fault. One change in how Kodi polls the decoder reduced recorded timestamp-anomaly events from 22 to 11 in a randomized comparison, but did not eliminate them. The reorder workaround in `patches/` removed the visible bounce in tested viewing; neither approach is established as universally correct.

## Setup

- **Device and content:** one Fire TV Stick 4K Max (2nd gen), `OMX.MTK.VIDEO.DECODER.HEVC`, Surface output, one 4K HDR10 HEVC title at 23.976 fps, the same one-minute scene every time (three minutes for the last comparison). Display mode and tracks were held fixed within each comparison.
- **Players:** Kodi 21.3 and Media3 (ExoPlayer) 1.11.1, both as lab builds that record every call at the MediaCodec boundary: inputs, outputs, releases, flushes.
- **Screening:** complete traces were checked for output timestamps that were never queued, missing input timestamps, and descending output timestamps. SIGNATURE denotes the investigated pre-keyframe anomaly pattern (see [HOW-IT-WORKS.md](HOW-IT-WORKS.md)); CLEAN means no detected timestamp anomaly, not proof of correct pixels. Failed acquisition checks were not counted as clean runs.
- **Evidence:** timestamp measurements, not pixel comparisons. The Media3 transplant conditions below had at least 3 runs each, with unchanged controls collected alongside the experimental program; not every condition used alternating order. The final Kodi comparison used randomized treatment order within blocks.

## Results

**Unmodified Kodi:** repeatedly showed the signature in early controls, but later controls were not uniformly faulty (7 of 8 in the final comparison). Fault locations and counts varied between runs.

**Unmodified Media3:** timestamp-clean in the accepted measured runs. Separate viewing of the official demo was reported smooth; the individual transplant runs were not all visually assessed.

**Kodi's traits moved into Media3 — all stayed CLEAN:**

| Kodi trait moved into Media3 | Runs |
|---|---|
| omit max-width/height configuration keys | 6/6 CLEAN |
| app holds up to 5 decoded outputs, as Kodi does | 3/3 CLEAN |
| Kodi's exact input bytes | 3/3 CLEAN |
| shallow feeding: cap the app-boundary queued-input minus dequeued-picture balance at 5 (usual observed median 12–13) | 3/3 CLEAN |
| shallow feeding and held outputs together | 3/3 CLEAN |
| timestamps in Kodi's range | 3/3 CLEAN |
| tested Kodi codec-configuration keys and codec-specific data | 3/3 CLEAN |
| combined configuration, exact input bytes, timestamp range, shallow feeding and held outputs | 3/3 CLEAN |

**Media3's traits moved into Kodi — no reliable effect:**

- adding max-width/height: 6/6 SIGNATURE;
- operating rate 23.976: no convincing reduction against interleaved controls;
- Media3's timestamp range: 2 of 3 still SIGNATURE.

**Polling without waiting — a partial mitigation:**

While running, Kodi requests an output-dequeue wait of up to 10 ms; the end-of-stream wait is separate. Media3 used synchronous mode with a non-blocking output check in our controls. With Kodi's running wait set to 0, the median app-boundary input-minus-output balance was typically 6 instead of 4. This is not a measurement of the physical decoder queue, and timeout and feeding effects were not isolated.

- First comparison: 6/6 CLEAN against 6/6 SIGNATURE. It did not replicate that cleanly afterwards.
- Predeclared, randomized comparison: four blocks of four three-minute runs, with two control and two modified runs randomly ordered within each block. All 16 acquisitions were valid and retained, without replacements. **22 control events versus 11 modified events**: 2.75 versus 1.375 events/run. The exact one-sided, within-block randomization test gave **p = 52/1296 = 0.04012**, meeting the predeclared 0.05 threshold. These are grouped timestamp-anomaly events, not counts of visibly bad frames. The signature remained in **7/8 control runs and 6/8 modified runs**; the probability of a faulty run was not halved.
- Two tracing revisions were used in fixed alternating block order. Their descriptive control/modified event totals were 15/4 and 7/7; revision differences are time-confounded, not a tested build effect. The pooled 50% reduction is an observed result, not a guaranteed effect size. The secondary invented-timestamp count was 36 versus 20 (one-sided p = 0.05324).
- A separate exploratory attempt to constrain feeding did not establish whether the benefit came from the output wait itself or changed feeding. Its limit allowed escape behavior and was not an exact match of decoder depth.

That change is in [`experimental/`](../experimental/). It is **not** part of the workaround.

## What we think it means

- The specific codec-facing differences listed above, individually and in the combined condition, did not reproduce the fault in Media3. This narrows the tested explanations; it does not exclude all decoder-configuration differences or configuration–timing interactions.
- What we could not move is the **timing** of Kodi's calls: which thread calls the decoder when, around keyframes, and how Kodi's player and renderer schedule that. [Another commenter](https://github.com/xbmc/xbmc/issues/29675#issuecomment-6615747361) reports reproducing their variant in a plain MediaCodec test app with paced output, outside Kodi, and it disappeared when they drained output as fast as possible. Together with the polling result, a timing-dependent race in the decoder is our leading guess. It is not proven.
- We tried to replay recorded call timing in a standalone app. The final Kodi-style and Media3-style dry runs both exceeded the predeclared timing tolerances; the Kodi-style run also exceeded the harness-delay bound. We stopped under the agreed stopping rule. Their raw timestamp results are not accepted evidence of faithful reproduction or of a causal timing contrast.

## Limits

- One title, one device, one firmware.
- Timestamp evidence only. The polling change was not validated for picture quality, A/V sync, seek, pause or stop behavior, power use, or other decoders. CPU counters were exploratory, not a general performance assessment. Input/output identity checks do not demonstrate pixel correctness.
- The polling change was never tested together with the reorder workaround.
- We do not know the decoder's internal state; "inputs in flight" is counted at the app boundary.

## Where we left it

The reorder workaround stays the recommendation. The polling change is published so others can try it or build on it. Whether combining both helps with anything the workaround alone gets wrong is an open question we did not test.
