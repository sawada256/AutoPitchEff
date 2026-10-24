#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
version=$(sed -n 's/^project(AutoPitchEff VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
mkdir -p dist
package_dir=$(mktemp -d "$PWD/dist/.package.XXXXXX")
trap 'rm -rf "$package_dir"' EXIT
release_dir="$package_dir/AutoPitchEff-$version"
mkdir -p "$release_dir/source/dependencies"
/usr/bin/ditto build-universal/AutoPitchEff_artefacts/Release/VST3/AutoPitchEff.vst3 "$release_dir/AutoPitchEff.vst3"
/usr/bin/ditto build-universal/AutoScaleLink_artefacts/Release/VST3/AutoScaleLink.vst3 "$release_dir/AutoScaleLink.vst3"
cp README.md LICENSE THIRD_PARTY.md CHANGELOG.md "$release_dir/"
cp -R docs licenses "$release_dir/"
cp CMakeLists.txt CMakePresets.json README.md LICENSE THIRD_PARTY.md CHANGELOG.md .gitignore "$release_dir/source/"
cp -R Source Tests Tools scripts docs licenses "$release_dir/source/"
archive=.cache/juce-8.0.12.tar.gz
if [ ! -f "$archive" ]; then
  mkdir -p .cache
  curl -fL https://codeload.github.com/juce-framework/JUCE/tar.gz/29396c22c93392d6738e021b83196283d6e4d850 -o "$archive"
fi
expected=97cc01e0fe15278f6edadc895a3cf0aac2fd98bcd023823384d5ca7025beb91c
actual=$(shasum -a 256 "$archive" | awk '{print $1}')
[ "$actual" = "$expected" ] || { echo 'JUCE archive hash mismatch' >&2; exit 1; }
cp "$archive" "$release_dir/source/dependencies/"
/usr/bin/codesign --verify --deep --strict "$release_dir/AutoPitchEff.vst3"
/usr/bin/codesign --verify --deep --strict "$release_dir/AutoScaleLink.vst3"
/usr/bin/ditto -c -k --sequesterRsrc --keepParent "$release_dir" "$PWD/dist/AutoPitchEff-$version-macOS-universal.zip"
echo "Package: $PWD/dist/AutoPitchEff-$version-macOS-universal.zip"
