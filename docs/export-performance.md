Export optimization measurements
===============================

The accepted change seeks each source independently to 0.5 seconds before its earliest selected segment and limits input to 0.5 seconds after its latest selected segment. Filter trim timestamps are adjusted by that source's seek offset. Previously, video/audio exports with multiple sources disabled seeking and decoded every source from its beginning. GIF export never sought at all.

Encoding presets, CRF, bitrate caps, pixel format, crop/scaling filters, and audio settings remain the same for regular video. NVIDIA availability now requires a successful tiny encode, cached for the process, rather than just appearing in FFmpeg's compiled encoder list. Systems without usable NVIDIA encoding use the existing x264 path.

The size budget uses the edited duration, including constant speeds and speed ramps. Muted exports now correctly convert bits per second to kilobits per second and explicitly use VBR when compressed. Previously the missing conversion usually selected the maximum bitrate, causing repeated attempts. Outputs exceeding the size target after retries are reported as failures and are not copied to the clipboard. The retry closure uses a weak self-reference so completed exports release their retained inputs.

Measurements use an i9-10850K, RTX 4060 Ti, FFmpeg n9.0.2, and a local 3840×2160, 60 fps H.264 recording with multiple Opus tracks. Files stayed local. The two-source benchmark selects two two-second clips beginning 40 seconds into the recording, applies the editor's default crop and rescale, and encodes with the existing p7 preset and bitrate settings. Both variants use four filter threads to hold that variable constant; the app's thread default is unchanged.

| Variant | Runs (seconds) | Output bytes | Decoded video/audio |
| --- | --- | --- | --- |
| Previous multi-source input handling | 33.742, 32.680 | 1,425,553 | Reference |
| Per-source seek and bounded input | 8.094, 7.704 | 1,425,553 | Identical SHA-256 stream hashes |

The mean improvement is 4.2× for this workload. Single-source exports were already seeking and showed little improvement from bounds alone (6.908 vs 6.847 seconds). This does not imply a 4.2× speedup for every edit.

Rejected experiments: filter thread caps, automatic hardware decoding, decoder thread caps, and NV12 pixel layout did not show a compelling repeatable gain. NV12 also changed decoded output. Presets p5/p4 were faster but reduced source-relative quality: baseline SSIM 0.998791 / PSNR 43.271 dB; p5 SSIM 0.996839 / PSNR 41.757 dB. They were not promoted. Full timing and command records, with local paths replaced by placeholders, are in `export-benchmark-results.json`.

Reproduce with a recording containing video and audio and at least 42 seconds of footage:

```sh
python3 scripts/benchmark_export.py /path/to/source.mp4 --multi-source --start 40 --duration 2 --output /tmp/export-benchmark.json
```

The harness limits each subprocess to 90 seconds and the run to ten minutes. Use `--quick`, `--layout`, or `--presets` for the rejected experiment families. `--cpu` exercises the unchanged x264 preset. Default mode compares input bounds and filter threading on a single source. The JSON ledger includes output size and decoded video/audio identity checks; preset experiments also record source-relative SSIM and PSNR.

```sh
cmake --build cmake-build-release --parallel 4
ctest --test-dir cmake-build-release --output-on-failure
```

Export regressions verify reordered sources and cuts, overlay timing after seeking, audio presence, geometry, slowed output duration, size limits, retry counts, and failure rather than clipboard publication for unattainable budgets. CPU fallback is also checked by launching the export regressions with a temporary FFmpeg wrapper that rejects NVIDIA encoding and delegates other commands to the real FFmpeg binary.
