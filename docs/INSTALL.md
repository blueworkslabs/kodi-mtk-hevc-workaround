# Installing, updating and rolling back

A self-built Kodi is signed with **your** key. Android will not install it over the official Kodi, and it will not let you update it with an APK signed by anyone else. Plan the switch before you start.

## Before anything

1. **Back up the whole Kodi profile** (the `.kodi` folder: databases, settings, add-ons, skin) from the device. Keep a list of files with their sizes or hashes, so you can tell later that the copy is complete.
2. **Keep the official Kodi APK** you are running now, for rollback.
3. Note where the profile lives:
   - The profile folder is called `.` plus the lower-cased app name: `.kodi` for the official app.
   - By default it sits in the app's storage, `Android/data/<package>/files/.kodi`.
   - If `/sdcard/xbmc_env.properties` exists, its `xbmc.data=` line names the folder that **contains** the profile folder, not the profile folder itself. For example, `xbmc.data=/storage/emulated/0/MyMedia` means the profile is `/storage/emulated/0/MyMedia/.kodi`.
   - On Android 11 and later, other apps cannot write into `Android/data/<package>`. `adb` can on many devices, but not on all. If it can't, use an `xbmc.data` folder outside `Android/data` instead.

## App identity

Pick one of two ways.

### A. Separate app (recommended for trying it)

Give the build its own identity so it installs next to the official Kodi. Before building, edit two places:

- **`version.txt`:** set `APP_NAME` and `APP_PACKAGE` to your own, e.g. `KodiMtkFix` and a package name under a domain you control.
- **`tools/android/packaging/xbmc/src/XBMCProperties.java.in`:** change the file name `xbmc_env.properties` to your own, e.g. `kodimtkfix_env.properties`.

The second change matters. Otherwise your build also reads the official Kodi's `xbmc_env.properties` and follows its `xbmc.data`. Editing that file for one app then moves both. If you kept the same `APP_NAME`, both apps would even share one profile folder.

With a separate app you get a separate profile. Its folder name follows the new `APP_NAME`: with `APP_NAME` `KodiMtkFix` it is `.kodimtkfix`, not `.kodi`.

- Copy the **contents** of your backed-up `.kodi` folder into that new folder, either in the app's storage (`Android/data/<your package>/files/.kodimtkfix`) or under the folder named by `xbmc.data=` in your own env file (`<xbmc.data>/.kodimtkfix`).
- Never point the new app at the folder the official Kodi uses.

Then:

- open the library and check a few movies, resume points, watched state and add-ons;
- play a title that showed the bounce;
- check the log for `keyframe output reorder enabled`.

### B. Replacing the official Kodi

Keep the official package name. The official app has to be uninstalled first, and uninstalling deletes its app storage.

1. Make sure the profile backup is complete and current.
2. Uninstall the official Kodi.
3. Install your build and restore the profile.
4. Check the library, resume points and add-ons before you watch anything.

Expect the Play Store or Amazon Appstore to show "update" offers for the official app that cannot be installed over yours.

## Updates and signing

- This build does not auto-update. Each new Kodi release needs a new build, and the patch may need porting; this repository only covers 21.3-Omega.
- Use **one** private signing key per installation, forever. A build signed with a different key cannot update the installed app; you would have to uninstall it and lose its app storage.
- Never commit or publish the key or its passwords. Keep a backup of the key somewhere safe.
- A build made from this patch is an unofficial Kodi build. Don't distribute it as "Kodi". If you share a binary, the GPL requires you to offer the matching source, this patch included.

## Rolling back

1. **Copy the current profile off the device first.** It holds your latest watch progress; an old backup does not.
2. Uninstall your build (or keep it, with way A).
3. Reinstall the official APK, grant its storage permission again if it needs one, and put the **current** profile copy in place.
4. Do not restore an old database over newer progress. If databases changed since the backup, take the newer ones.
