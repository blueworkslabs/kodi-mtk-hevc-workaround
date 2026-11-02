# Building an APK

Kodi's own guide is `docs/README.Android.md` in the 21.3-Omega source; read it first. Below is what was actually used for the tested build, and what was not re-run.

## What was used

**Build host:** Linux x86_64, Ubuntu 22.04 userland, OpenJDK 17, run without network for the build itself (sources and tarballs fetched beforehand).

**Android SDK:**
- command-line tools 12.0
- platform `android-34`
- build-tools `33.0.1`
- NDK `21.4.7075529`

These are the versions Kodi 21.3 documents.

**Target:** `armeabi-v7a`. The Fire TV Stick 4K Max (2nd gen) runs a 32-bit userspace even though its CPU is 64-bit; check `adb shell getprop ro.product.cpu.abilist` on your device.

## Steps

```sh
scripts/prepare-source.sh $HOME/kodi-21.3-mtk
cd $HOME/kodi-21.3-mtk

# signing key: your own, kept private and reused for every update (see INSTALL.md)
export KODI_ANDROID_STORE_FILE=$HOME/keys/my-kodi.keystore
export KODI_ANDROID_STORE_PASSWORD=...  KODI_ANDROID_KEY_ALIAS=...  KODI_ANDROID_KEY_PASSWORD=...

cd tools/depends
./bootstrap
./configure --with-tarballs=$HOME/kodi-tarballs --host=arm-linux-androideabi \
  --with-sdk-path=$HOME/android-sdk --with-ndk-path=$HOME/android-sdk/ndk/21.4.7075529 \
  --prefix=$HOME/kodi-depends --enable-debug=no
make -j$(nproc)
cd ../..
make -C tools/depends/target/cmakebuildsys BUILD_DIR=$HOME/kodi-build Configuration=Release
make -C $HOME/kodi-build -j$(nproc)
make -C $HOME/kodi-build apk
```

The APK ends up in the source directory, named after `APP_NAME`.

**Gotcha:** if the SDK command-line tools are installed as `cmdline-tools/12.0`, Kodi's configure does not find them. Add a `cmdline-tools/latest` symlink to that directory.

Before building, decide on the app identity; see [INSTALL.md](INSTALL.md#app-identity).

## What was and was not re-run

**Built and tested:**
- The tested APK was built with these commands, on a tree first built from scratch at the same Kodi commit. That earlier build used a separate app identity, the change described in INSTALL.md, which is not part of this patch.
- The workaround was then applied and built incrementally: the decoder files and the two MediaCodec renderers were recompiled and the native library relinked.
- That APK ran on the test device.

**Checked for publication:**
- `scripts/prepare-source.sh` was run against a fresh GitHub clone; the patch applied cleanly.
- The five workaround files match the reviewed, built implementation except for two attribution-comment lines at the top of each new helper file. No executable code differs. The separate test-app identity changes are not included in this patch.
- `tests/run-tests.sh` and `tests/run-controls.py` were run against that clone.

**Not done:**
- A full build from scratch of exactly this repository's patch, without the identity change.
- arm64-v8a, x86, or any other Kodi version.
