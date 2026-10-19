# LÖÖP BRÖTHER — releases

Decided 2026-10-19: releases are **unsigned for now** on both platforms.
The Mac release is built on the Mac; Windows is built by GitHub Actions.
The bundle ID is `com.minus.loopbrother`, set before the first release. The
plugin codes (manufacturer `Mnro`, plugin `Jngl`) never change: saved
sessions find the plugin by them.

## macOS

```sh
sh installer/macos/make_pkg.sh            # → dist/LOOP-BROTHER-<version>-macos.pkg
```

- Builds a universal release (Apple silicon + Intel) in `build-release/`
  without touching the plugins installed by the dev build
  (`-DJM_INSTALL_AFTER_BUILD=OFF`).
- Checks every binary has both architectures. Seals each bundle with an
  ad-hoc signature (`codesign --sign -`).
- Packs one installer with a choice of parts:
  - the VST3 → `/Library/Audio/Plug-Ins/VST3`;
  - the Audio Unit → `/Library/Audio/Plug-Ins/Components`;
  - the app → `/Applications`;
  - the font licence → `/Library/Application Support/minus/LOOP BROTHER`.

  Bundles are marked not relocatable, so the installer never "updates" a
  copy it finds elsewhere instead.

**What users see while it's unsigned:** macOS blocks the downloaded package
on first open. On macOS 15 and later: try to open it, then System Settings →
Privacy & Security → *Open Anyway*. On older macOS: right-click → Open.
Installed plugins aren't quarantined, so hosts load them normally.

## Windows

`.github/workflows/windows.yml` runs on every push to `main` (and by hand).
It does four things:

1. Builds the VST3 and the app.
2. Runs the engine tests.
3. Packs `installer/windows/loopbrother.iss` with Inno Setup. The VST3 goes
   to `C:\Program Files\Common Files\VST3`; the app and the font licence go
   to `C:\Program Files\minus\LOOP BROTHER`.
4. Uploads `LOOP-BROTHER-<version>-windows.exe` as an artifact.

This needs the project on GitHub (a private repo is fine). It has never run
yet; expect the first run to need fixes, since nothing here has been built
with MSVC.

**What users see while it's unsigned:** SmartScreen warns about an
unrecognised app: *More info → Run anyway*. DAWs load unsigned VST3s.

## Shipping notes

- The logo font (Cherry Bomb One) is compiled in under the SIL Open Font
  License: `fonts/OFL-CherryBombOne.txt` ships in both installers and must
  stay with any other distribution.
- The breaks in `breaks/` are sampled from records. Distributing them is a
  rights question separate from signing.

## Signing later: the checklist

**macOS**

1. Join the Apple Developer Program as an individual ($99/year; signs as
   "Developer ID Application: Daniel Stanciu"). An organisation instead
   needs a legal entity and a D-U-N-S number, and shows its name. Not the
   LUMINARY ROLI LIMITED team on this Mac.
2. Create **Developer ID Application** and **Developer ID Installer**
   certificates (Xcode → Settings → Accounts → Manage Certificates).
3. In `make_pkg.sh`, replace the ad-hoc seal:
   `codesign --force --options runtime --timestamp --sign "Developer ID Application: …"`
   on each bundle, innermost first. The plugins need no entitlements, and
   neither does the app while it takes no audio input.
4. Sign the package with `productsign --sign "Developer ID Installer: …"`.
5. Notarize: `xcrun notarytool submit dist/….pkg --keychain-profile <profile> --wait`
   (store the credentials once with `notarytool store-credentials`, from an
   app-specific password or an App Store Connect API key).
6. Staple the notarization: `xcrun stapler staple dist/….pkg`.

**Windows**

1. Get a code-signing certificate on hardware or a cloud signing service
   (required since 2023). Options: Certum (EU individuals, about €100/year),
   SSL.com eSigner (about $250/year, suits CI), or Azure Trusted Signing
   (about $10/month, depending on eligibility).
2. Sign `LOOP BROTHER.exe`, the DLL inside the VST3 bundle, and the
   installer. Inno Setup's `SignTool=` directive also signs its uninstaller:
   `signtool sign /fd sha256 /tr <timestamp URL> /td sha256 …`.
3. SmartScreen warnings fade as downloads build a reputation; EV
   certificates no longer skip that.
