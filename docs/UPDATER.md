# FFAtmo update system

The companion checks `update/latest.json` from the `main` branch once when it starts. Ordinary commits and pull requests do not notify users. A notification is shown only when the manifest contains a newer semantic version than the compiled `kAppVersion`.

## Stable release procedure

After this workflow is merged to `main`, stable releases are automated. Create and push a semantic version tag such as `v0.4.1`. GitHub Actions builds the companion and updater, runs tests, creates the ZIP, calculates SHA-256, publishes the GitHub Release, and updates the stable manifest. Installed applications see the new manifest the next time they start.

Manual equivalent:

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

## Test modes

- `--test-update-popup` renders a non-destructive preview and cannot install files.
- `--test-update-live` reads `update/test.json` from the updater branch and exercises the real HTTPS download, checksum, staging, rollback, and relaunch path. Use it only with a deliberately published test prerelease.
