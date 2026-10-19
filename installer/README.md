# Installing LÖÖP BRÖTHER

LÖÖP BRÖTHER is an instrument for macOS and Windows. It comes as a VST3
plugin and a standalone app, plus an Audio Unit on macOS. In plugin lists
it appears as **LOOP BROTHER** by **minus**.

The installers aren't signed yet, so macOS and Windows warn you that they
don't know who made them. The steps below get past those warnings.

## macOS

macOS 11 or later, Apple silicon or Intel.

1. **Download** `LOOP-BROTHER-<version>-macos.pkg` from this folder.
2. **Open it.** macOS says it can't check the package for malicious
   software. Click **Done** (not *Move to Bin*).
3. **Allow it.**
   - **macOS 15 or later:** open **System Settings → Privacy & Security**,
     scroll down to the message about `LOOP-BROTHER-….pkg`, click
     **Open Anyway**, then confirm with your password.
   - **macOS 14 or earlier:** Control-click (or right-click) the package,
     choose **Open**, then **Open** again.
4. **Install.** To leave out a format, click **Customize**. The installer
   puts:
   - the VST3 in `/Library/Audio/Plug-Ins/VST3`;
   - the Audio Unit in `/Library/Audio/Plug-Ins/Components`;
   - the app in `/Applications`.
5. **Rescan your plugins** in your DAW, or restart it. Logic Pro checks new
   Audio Units on its next start.

The plugins and the app then open like any other, without further warnings.

**To uninstall,** delete `LOOP BROTHER.vst3`, `LOOP BROTHER.component` and
`LOOP BROTHER.app` from the folders above, and the folder
`/Library/Application Support/minus/LOOP BROTHER`.

## Windows

Windows 10 or 11, 64-bit.

1. **Download** `LOOP-BROTHER-<version>-windows.exe` from this folder. If
   your browser says the file isn't commonly downloaded, choose **Keep**.
2. **Run it.** When Windows shows *Windows protected your PC*, click
   **More info**, then **Run anyway**. Allow it to make changes when asked.
3. **Install.** The installer puts:
   - the VST3 in `C:\Program Files\Common Files\VST3`;
   - the app in `C:\Program Files\minus\LOOP BROTHER`, with a Start menu
     entry.
4. **Rescan your plugins** in your DAW, or restart it.

**To uninstall,** use **Settings → Apps → Installed apps → LOOP BROTHER →
Uninstall**.
