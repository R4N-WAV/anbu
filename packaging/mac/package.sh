#!/bin/bash
# Packs the Mac build into Anbu-macOS.zip: an installer (Install Anbu.pkg) for both formats,
# the plugin bundles for a manual install, and INSTALL.md.
# usage (from the repository root, after building Anbu_VST3 and Anbu_AU in Release):
#   packaging/mac/package.sh [build dir, default: build]
# Needs the Xcode command line tools (codesign, pkgbuild, productbuild, ditto).
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
build="${1:-build}"
art="$build/Anbu_artefacts/Release"
version="$(sed -n 's/^project(Anbu VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

for b in "$art/VST3/Anbu.vst3" "$art/AU/Anbu.component"; do
    [ -d "$b" ] || { echo "not built: $b" >&2; exit 1; }
done

rm -rf dist Anbu-macOS.zip
mkdir -p "dist/Manual install" "$work/vst3" "$work/au" "$work/pkgs"
cp -R "$art/VST3/Anbu.vst3" "$art/AU/Anbu.component" "dist/Manual install/"

# Not notarized (that needs a paid Apple developer account), but signed ad hoc so the
# bundle is sealed and Apple Silicon loads it.
for b in "dist/Manual install/Anbu.vst3" "dist/Manual install/Anbu.component"; do
    codesign --force --deep --sign - --timestamp=none "$b"
    codesign --verify --deep --strict "$b"
done

cp -R "dist/Manual install/Anbu.vst3" "$work/vst3/"
cp -R "dist/Manual install/Anbu.component" "$work/au/"

# One package per format, installed into /Library/Audio/Plug-Ins/<format>.
# The bundles must not be "relocatable", or Installer would follow an older copy elsewhere
# on the disk instead of installing to the plug-in folder.
component_pkg() {   # <payload dir> <pkg id> <install location> <scripts dir> <out>
    pkgbuild --analyze --root "$1" "$work/components.plist" >/dev/null
    local i=0
    while plutil -extract "$i" xml1 -o /dev/null "$work/components.plist" 2>/dev/null; do
        plutil -replace "$i.BundleIsRelocatable" -bool NO "$work/components.plist"
        plutil -replace "$i.BundleIsVersionChecked" -bool NO "$work/components.plist"
        i=$((i + 1))
    done
    chmod +x "$4"/*
    pkgbuild --root "$1" --component-plist "$work/components.plist" --identifier "$2" --version "$version" \
             --install-location "$3" --scripts "$4" "$5"
}
component_pkg "$work/vst3" com.r4n.anbu.vst3 /Library/Audio/Plug-Ins/VST3 "$here/scripts-vst3" "$work/pkgs/Anbu-VST3.pkg"
component_pkg "$work/au" com.r4n.anbu.au /Library/Audio/Plug-Ins/Components "$here/scripts-au" "$work/pkgs/Anbu-AU.pkg"

sed "s/@VERSION@/$version/g" "$here/distribution.xml" > "$work/distribution.xml"
productbuild --distribution "$work/distribution.xml" --package-path "$work/pkgs" --resources "$here/resources" \
             "dist/Install Anbu.pkg"

cp "$root/INSTALL.md" dist/
ditto -c -k --norsrc --noextattr dist Anbu-macOS.zip
echo "packed Anbu-macOS.zip (version $version):"
unzip -l Anbu-macOS.zip | grep -v "/Contents/.*/" || true
