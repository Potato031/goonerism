#!/usr/bin/env bash
set -euo pipefail

project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=${1:-"$project_dir/build"}
package_dir=${2:-"$project_dir/appimage-build"}
mkdir -p "$package_dir"
cd "$package_dir"

for tool in linuxdeploy linuxdeploy-plugin-qt; do
    curl --fail --location --retry 3 --output "$tool-x86_64.AppImage" \
        "https://github.com/linuxdeploy/$tool/releases/download/continuous/$tool-x86_64.AppImage"
done
chmod +x linuxdeploy*.AppImage

export APPIMAGE_EXTRACT_AND_RUN=1
export LDAI_OUTPUT=potatoeditor.appimage
export LINUXDEPLOY_OUTPUT_VERSION=${APPIMAGE_VERSION:-development}
export QMAKE=${QMAKE:-$(command -v qmake6 || command -v qmake)}

# The source icon is 144px; desktop icon themes require a standard size.
ffmpeg -v error -y -i "$project_dir/potato.png" -vf scale=128:128 -frames:v 1 potato.png

# Qt's FFmpeg backend loads codec libraries at runtime. Deploy those shipped
# with this Qt build as well as the command-line tools used by the editor.
qt_lib_dir=$("$QMAKE" -query QT_INSTALL_LIBS)
codec_args=()
shopt -s nullglob
for library in "$qt_lib_dir"/lib{avcodec,avformat,avutil,swresample,swscale}.so.*; do
    codec_args+=(--library "$library")
done

./linuxdeploy-x86_64.AppImage --appdir AppDir \
    --executable "$build_dir/PotatoEditor" \
    --executable "$(command -v ffmpeg)" \
    --executable "$(command -v ffprobe)" \
    --desktop-file "$project_dir/packaging/PotatoEditor.desktop" \
    --icon-file "$package_dir/potato.png" \
    "${codec_args[@]}" --plugin qt --output appimage

test -s potatoeditor.appimage
# Verify the helpers survived packaging without requiring FUSE on CI.
./potatoeditor.appimage --appimage-extract >/dev/null
test -x squashfs-root/usr/bin/PotatoEditor
test -x squashfs-root/usr/bin/ffmpeg
test -x squashfs-root/usr/bin/ffprobe
