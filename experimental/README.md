# Experimental

Patches here are **not** part of the workaround and are not recommended for daily use. They are published as measured experiments; see [docs/FINDINGS.md](../docs/FINDINGS.md).

## `0001-MediaCodec-poll-for-output-without-waiting-experimental.patch`

Changes one value in `CDVDVideoCodecAndroidMediaCodec::GetOutputPicture()`: while running, `dequeueOutputBuffer()` no longer waits up to 10 ms for output. The end-of-stream wait (100 ms) is unchanged. It applies cleanly to Kodi `21.3-Omega` (`a3a448d`), on its own or on top of `patches/0001-*.patch`.

**Measured:**
- On a Fire TV Stick 4K Max (2nd gen) this halved how often the frame-order fault appeared (randomised comparison, one-sided p = 0.04).
- It did not remove it: 6 of 8 three-minute runs still showed it.

**Not established:**
- Visual effect, A/V sync, CPU and power use, seek/pause/stop behaviour, other decoders and devices.
- How it behaves together with the reorder workaround: that combination was never run.

**How it was tested:** the same change was made through a lab tracing build of Kodi 21.3. That build had only this behaviour switched, and no reorder workaround. This exact patch file was checked to apply, but not built and run as such.

```sh
scripts/prepare-source.sh ../kodi-21.3-mtk
git -C ../kodi-21.3-mtk apply "$PWD/experimental/0001-MediaCodec-poll-for-output-without-waiting-experimental.patch"
```
