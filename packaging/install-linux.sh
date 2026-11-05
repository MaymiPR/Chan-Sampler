#!/bin/sh
# Installs Chan Sampler from this folder for the current user: VST3 to ~/.vst3, CLAP to ~/.clap (the root plug-in
# folders, where every release since 1.0 has put it) and the standalone app (when the tarball carries a Standalone/
# folder) to ~/.local/bin.
#   sh install-linux.sh              install
#   sh install-linux.sh --uninstall  remove again
set -e
cd "$(dirname "$0")"
V="$HOME/.vst3"
C="$HOME/.clap"
B="$HOME/.local/bin"
if [ "$1" = "--uninstall" ]; then
    for p in VST3/*.vst3; do
        [ -e "$p" ] && rm -rf "${V:?}/$(basename "$p")"
    done
    for p in CLAP/*.clap; do
        [ -e "$p" ] && rm -f "${C:?}/$(basename "$p")"
    done
    for p in Standalone/*; do
        [ -f "$p" ] && rm -f "${B:?}/$(basename "$p")"
    done
    echo "Chan Sampler removed from $V, $C and $B"
    exit 0
fi
mkdir -p "$V" "$C"
for p in VST3/*.vst3; do
    [ -e "$p" ] || continue
    rm -rf "${V:?}/$(basename "$p")"
    cp -r "$p" "$V/"
done
for p in CLAP/*.clap; do
    [ -e "$p" ] && cp "$p" "$C/"
done
for p in Standalone/*; do
    [ -f "$p" ] || continue
    mkdir -p "$B"
    cp "$p" "$B/"
    chmod +x "$B/$(basename "$p")"
done
echo "Installed to $V and $C:"
ls VST3 CLAP
if [ -d Standalone ]; then echo "Standalone app in $B:"; ls Standalone; fi
echo "Rescan plug-ins in your DAW."
