# Experimental

Patches here are **not** part of the workaround and are not recommended for daily use. They are published as measured experiments; see [docs/FINDINGS.md](../docs/FINDINGS.md).

## `0001-MediaCodec-poll-for-output-without-waiting-experimental.patch`

Changes one value in `CDVDVideoCodecAndroidMediaCodec::GetOutputPicture()`: while running, `dequeueOutputBuffer()` no longer waits up to 10 ms for output. The end-of-stream wait (100 ms) is unchanged. It applies cleanly to Kodi `21.3-Omega` (`a3a448d`), on its own or on top of `patches/0001-*.patch`.

**Measured:**

- On a Fire TV Stick 4K Max (2nd gen), recorded timestamp-anomaly events fell from 22 to 11 in one randomized comparison (exact one-sided within-block p = 0.04012). This is an observed event-count reduction, not a halving of faulty-run frequency.
- It did not remove it: 6 of 8 three-minute runs still showed it.

**Not established:**

- Visual effect, A/V sync, CPU and power use, seek/pause/stop behaviour, other decoders and devices.
- How it behaves together with the reorder workaround: that combination was never run.

**How it was tested:** the same change was made through a lab tracing build of Kodi 21.3. Within each randomized block, this was the only treatment setting changed; both arms used the same tracing revision without the reorder workaround. This exact patch file was checked to apply, but not built and run as such.

The patch changes the general Android MediaCodec path; it is **not restricted to the MTK HEVC decoder**. Applying it cleanly on top of the reorder patch proves only textual compatibility, not runtime compatibility. The existing reorder unit tests do not validate non-blocking playback or the combination.

To prepare the standalone experiment from the repository root (this does not build or install it):

```sh
git clone --depth 1 --branch 21.3-Omega https://github.com/xbmc/xbmc.git ../kodi-21.3-poll
[ "$(git -C ../kodi-21.3-poll rev-parse HEAD)" = "a3a448d26b8d560a65655dab2cd122994dc4e146" ] && \
  git -C ../kodi-21.3-poll apply "$PWD/experimental/0001-MediaCodec-poll-for-output-without-waiting-experimental.patch"
```

`scripts/prepare-source.sh` instead prepares the reorder workaround. Applying this experiment afterward would create the untested combination; that is not the default experiment above.
