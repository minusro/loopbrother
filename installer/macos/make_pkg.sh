#!/bin/sh
# Build LÖÖP BRÖTHER for release on this Mac, universal (Apple silicon and
# Intel), and pack the VST3, the Audio Unit and the app into one installer
# package, dist/LOOP-BROTHER-<version>-macos.pkg.
#
# Unsigned for now (installer/RELEASE.md): each bundle gets an ad-hoc seal only,
# the package none, so users open it from System Settings > Privacy &
# Security the first time.
#
# Usage, from the project root:   sh installer/macos/make_pkg.sh [build dir]
# JM_JUCE_DIR=<local JUCE checkout> is passed on to CMake if set.
set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
build="${1:-$root/build-release}"
ver=$(sed -n 's/^project(junglemassive VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")
name="LOOP BROTHER"
id="com.minus.loopbrother"
art="$build/junglemassive_artefacts/Release"
out="$root/dist/LOOP-BROTHER-$ver-macos.pkg"

echo "== building $ver (universal) in $build"
cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
      -DJM_INSTALL_AFTER_BUILD=OFF ${JM_JUCE_DIR:+-DJM_JUCE_DIR="$JM_JUCE_DIR"} >/dev/null
cmake --build "$build" --config Release --parallel 8 \
      --target junglemassive_VST3 junglemassive_AU junglemassive_Standalone

vst3="$art/VST3/$name.vst3"
au="$art/AU/$name.component"
app="$art/Standalone/$name.app"
for b in "$vst3" "$au" "$app"; do
    lipo -archs "$b/Contents/MacOS/$name" | grep -q 'x86_64 arm64\|arm64 x86_64' \
        || { echo "not universal: $b"; exit 1; }
    # An ad-hoc seal over the whole bundle; a Developer ID signature replaces
    # it once there is one.
    codesign --force --deep --sign - "$b"
done

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/vst3" "$work/au" "$work/app" "$work/licences" "$root/dist"
# Copied without extended attributes where macOS allows it. The provenance
# stamp it won't drop goes into the payload as ._ entries, which the
# installer turns back into an attribute: no ._ files land on disk.
ditto --norsrc --noextattr --noqtn "$vst3" "$work/vst3/$name.vst3"
ditto --norsrc --noextattr --noqtn "$au" "$work/au/$name.component"
ditto --norsrc --noextattr --noqtn "$app" "$work/app/$name.app"
ditto --norsrc --noextattr --noqtn "$root/fonts/OFL-CherryBombOne.txt" "$work/licences/OFL-CherryBombOne.txt"
xattr -cr "$work"

# One package per part. Bundles are marked not relocatable: otherwise the
# installer "updates" any other copy it finds elsewhere on the disk (a dev
# build, say) instead of installing where it should.
part() { # <dir> <install location> <part>
    plist="$work/$3.plist"
    pkgbuild --analyze --root "$1" "$plist" >/dev/null
    i=0
    while /usr/libexec/PlistBuddy -c "Print :$i" "$plist" >/dev/null 2>&1; do
        /usr/libexec/PlistBuddy -c "Delete :$i:BundleIsRelocatable" "$plist" >/dev/null 2>&1 || true
        /usr/libexec/PlistBuddy -c "Add :$i:BundleIsRelocatable bool false" "$plist"
        i=$((i + 1))
    done
    pkgbuild --root "$1" --component-plist "$plist" --install-location "$2" \
             --identifier "$id.$3" --version "$ver" "$work/$3.pkg" >/dev/null
}
part "$work/vst3" /Library/Audio/Plug-Ins/VST3 vst3
part "$work/au" /Library/Audio/Plug-Ins/Components au
part "$work/app" /Applications app
pkgbuild --root "$work/licences" --install-location "/Library/Application Support/minus/$name" \
         --identifier "$id.licences" --version "$ver" "$work/licences.pkg" >/dev/null

cat > "$work/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>LÖÖP BRÖTHER $ver</title>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_localSystem="true"/>
    <choices-outline>
        <line choice="vst3"/>
        <line choice="au"/>
        <line choice="app"/>
        <line choice="licences"/>
    </choices-outline>
    <choice id="vst3" title="VST3 plug-in"><pkg-ref id="$id.vst3"/></choice>
    <choice id="au" title="Audio Unit plug-in"><pkg-ref id="$id.au"/></choice>
    <choice id="app" title="Standalone app"><pkg-ref id="$id.app"/></choice>
    <choice id="licences" title="Licences" visible="false"><pkg-ref id="$id.licences"/></choice>
    <pkg-ref id="$id.vst3" version="$ver">vst3.pkg</pkg-ref>
    <pkg-ref id="$id.au" version="$ver">au.pkg</pkg-ref>
    <pkg-ref id="$id.app" version="$ver">app.pkg</pkg-ref>
    <pkg-ref id="$id.licences" version="$ver">licences.pkg</pkg-ref>
</installer-gui-script>
EOF
rm -f "$out"
productbuild --distribution "$work/distribution.xml" --package-path "$work" "$out" >/dev/null
echo "== wrote $out"
