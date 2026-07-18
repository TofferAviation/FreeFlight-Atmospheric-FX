# FFAtmo update system

The companion checks `update/latest.json` from the `main` branch once when it starts. Ordinary commits and pull requests do not notify users. A notification is shown only when the manifest contains a newer semantic version than the compiled `kAppVersion`.

## Stable release procedure

1. Build and test `FFAtmoCompanion.exe`, `FFAtmoUpdater.exe`, the X-Plane plugin, and runtime assets.
2. Create an update ZIP whose root contains the contents of `package/FFAtmo` (do not wrap those files in another `FFAtmo` directory).
3. Calculate the ZIP's SHA-256 checksum.
4. Create a tagged GitHub Release and attach the ZIP.
5. Update `update/latest.json` with the release version, direct asset URL, byte size, checksum, channel, and concise release notes.
6. Merge that manifest change only after the release asset is available.

## Safety model

- HTTPS is mandatory.
- The helper refuses packages whose SHA-256 does not exactly match the manifest.
- The running companion closes before files are replaced.
- The helper extracts to a temporary staging directory before copying files.
- Failed download, verification, or extraction leaves the installed application untouched.
- Existing files are backed up before replacement and restored if installation fails.
- The running updater executable is skipped so Windows never has to overwrite a live process.
- Code signing with an Authenticode certificate is recommended before the public V1 release.

## Channels

The initial application uses the `stable` channel. The checker already understands a `beta` channel; a future Settings control can opt users into it.
