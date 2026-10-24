#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
plugin_dir="$HOME/Library/Audio/Plug-Ins/VST3"
if [ "$#" -gt 0 ]; then
  sources=("$@")
else
  sources=("$PWD/build-universal/AutoPitchEff_artefacts/Release/VST3/AutoPitchEff.vst3"
           "$PWD/build-universal/AutoScaleLink_artefacts/Release/VST3/AutoScaleLink.vst3")
fi
mkdir -p "$plugin_dir"
staging_dir=$(mktemp -d "$plugin_dir/.autopitcheff.XXXXXX")
trap 'rm -rf "$staging_dir"' EXIT
# Stage and verify both before replacing either installed bundle.
for source_bundle in "${sources[@]}"; do
  name=$(basename "$source_bundle")
  case "$name" in AutoPitchEff.vst3|AutoScaleLink.vst3) ;; *) echo "Unsupported bundle: $name" >&2; exit 1 ;; esac
  /usr/bin/codesign --verify --deep --strict "$source_bundle"
  /usr/bin/ditto "$source_bundle" "$staging_dir/$name"
  /usr/bin/codesign --verify --deep --strict "$staging_dir/$name"
done
for source_bundle in "${sources[@]}"; do
  name=$(basename "$source_bundle")
  if [ -e "$plugin_dir/$name" ]; then
    backup_dir=$(mktemp -d "$HOME/Library/Audio/Plug-Ins/AutoPitchEff-backup.XXXXXX")
    mv "$plugin_dir/$name" "$backup_dir/$name"
    echo "Previous version: $backup_dir/$name"
  fi
  mv "$staging_dir/$name" "$plugin_dir/$name"
  echo "Installed: $plugin_dir/$name"
done
