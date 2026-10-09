# Potato Studio

A desktop video and audio editor built with Qt 6 and ffmpeg. Import media, trim and split clips, add timed effects, clean up silence, and export video, audio, or GIFs.

Build with CMake 3.16+, a C++17 compiler, and Qt 6 Widgets, Multimedia, MultimediaWidgets, and Concurrent. Put `ffmpeg` and `ffprobe` on PATH (on Windows, alongside the executable).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build --parallel
./build/PotatoEditor
```

Pushes to `main` and manual runs of **CI Build and AppImage** build and test the Linux app, then upload `potatoeditor.AppImage` in the `PotatoEditor-linux-x86_64` Actions artifact. Version tags (`v*`) attach the same AppImage to a GitHub release. The package includes Qt, `ffmpeg`, and `ffprobe`.

- **Ctrl+O** imports media. The app opens the newest video from your configured folders and recent files; autoplay follows Settings. Disable “Open the newest video on startup” in Settings to start with an empty workspace.
- **Ctrl+K** searches actions. Use Up/Down and Enter to run one.
- Search the media panel by filename. Recent imports and panel sizes persist between sessions.
- **View** shows or hides the media and effects panels, or resets their layout. Drag the dividers to resize panels; effects scroll in smaller windows.
- Drop files onto the timeline to append video or audio. Opening a file from the media panel replaces the current timeline.
- **Ctrl+wheel** zooms at the pointer. Wheel pans a zoomed timeline; **Fit** restores the full view.
- Use the **?** button for editing shortcuts and Settings to customize them.
- Export saves a file to the configured export directory and copies its location to the clipboard. An active export keeps the current media loaded until it finishes.

Media probing, waveform extraction, and preview processing run asynchronously. Waveforms reduce temporary PCM on a background worker and retain a compact RMS envelope; zoomed timeline painting only visits visible clips, frames, ticks, and waveform columns. Thumbnail caches include the full source path, size, and modification time. Shutdown disconnects media process signals before destroying timeline caches, preventing synchronous process completion callbacks from accessing freed state.

Tests require Qt 6 Test and ffmpeg/ffprobe. They use generated media and an isolated settings directory.

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The test suite covers paused imports, rapid media switching, waveform alignment, undo/redo, overlay deletion, action search, panel controls, failed export startup, process and preview worker lifetime, ruler seeking, trim undo, cache identity, and compact window layout.

Native playback hides the inactive editing layer, so a transparent QWidget does not repaint over the video on every frame. Effects can still be dropped onto the playing viewer. Thumbnail requests for the same source share one job, and thumbnail/audio preparation uses limited decoder threads and lower process priority on Unix to keep playback responsive.

Playback repaints only the old and new playhead strips while the active segment stays unchanged. Waveform and thumbnail drawing visits the damaged area; edits and segment transitions still refresh the whole timeline. A regression compares incremental rendering against a full redraw, including antialiased edges and ruler labels.

For 120 playhead moves across a two-hour, 100 Hz waveform at 1600×240, two alternating Release-build measurements gave median rendering times of 1,295 ms before this change and 138 ms afterward. Repainted pixels fell from 46,080,000 to 922,320 (98%). These measure timeline rendering, not overall decoding CPU; the separate 4K playback profile can still exceed its responsiveness budget on a busy desktop.

```sh
QT_QPA_PLATFORM=offscreen ./build/EditorTests playbackRepaintsOnlyMovingPlayhead -nocrashhandler
```

Tagged Linux releases verify that the tag matches `CURRENT_VERSION` in `src/Includes/mainWindow.h` and pass regression tests before packaging and publishing the AppImage.

To measure UI timer delays against a local recording on the desktop:

```sh
QT_QPA_PLATFORM=xcb POTATO_EDITOR_PROFILE_MEDIA=/path/to/recording.mp4 ./build/EditorTests profileInteractivePlayback -nocrashhandler
```
