#!/usr/bin/env python3
"""Measure export variants and record decoded-stream identity, size, and optional quality metrics."""
import argparse
import json
import re
from pathlib import Path
import subprocess
import tempfile
import time


DEADLINE = time.monotonic() + 600


def run(args, timeout=90):
    remaining = DEADLINE - time.monotonic()
    if remaining <= 0:
        raise TimeoutError("Export benchmark exceeded its ten-minute budget")
    return subprocess.run(args, check=True, capture_output=True, text=True, timeout=min(timeout, remaining))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--start', type=float, default=20)
    parser.add_argument('--duration', type=float, default=4)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cpu', action='store_true')
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument('--multi-source', action='store_true', help='Compare two-source exports that select later clips')
    modes.add_argument('--layout', action='store_true', help='Compare pixel layout and decoder-thread variants')
    modes.add_argument('--presets', action='store_true', help='Compare NVIDIA quality presets with source SSIM and PSNR')
    modes.add_argument('--quick', action='store_true', help='Only baseline, decoder threading, hardware decoding and repeated baseline')
    args = parser.parse_args()
    args.source = args.source.resolve()
    if args.start < 0 or args.duration <= 0:
        parser.error('Start must be nonnegative and duration must be positive')
    if args.cpu and args.presets:
        parser.error('Preset comparisons require NVIDIA encoding')
    metadata = json.loads(run(['ffprobe', '-v', 'error', '-show_streams', '-show_format', '-of', 'json', str(args.source)]).stdout)
    if args.start + args.duration > float(metadata['format']['duration']):
        parser.error('The selected clip extends beyond the source duration')
    video = next(s for s in metadata['streams'] if s['codec_type'] == 'video')
    w, h = video['width'], video['height']
    bitrate = int(int(metadata['format']['size']) * 8 / float(metadata['format']['duration']) / 1000)
    seek = max(0, args.start - .5)
    local_start = args.start - seek
    graph = (f'[0:v]trim=start={local_start}:duration={args.duration},setpts=PTS-STARTPTS[s_seg0];'
             '[s_seg0]null[s_v0];'
             f'[s_v0]crop=trunc(iw*(1-0)/2)*2:trunc(ih*(0.96-0.03)/2)*2:0:trunc(ih*0.03/2)*2,scale={w}:{h},setsar=1,format=yuv420p[s_vx0];'
             f'[0:a:0]atrim=start={local_start}:duration={args.duration},asetpts=PTS-STARTPTS,volume=1,aresample=async=1,aformat=sample_rates=48000:channel_layouts=stereo[s_a0];'
             '[s_vx0][s_a0]concat=n=1:v=1:a=1[outv][outa]')
    encoder = (['-c:v', 'libx264', '-preset', 'slow', '-crf', '18', '-maxrate', f'{bitrate}k', '-bufsize', f'{bitrate * 2}k'] if args.cpu else
               ['-c:v', 'h264_nvenc', '-preset', 'p7', '-rc', 'vbr', '-b:v', f'{bitrate}k', '-maxrate', f'{bitrate * 2}k'])
    results = []
    with tempfile.TemporaryDirectory(prefix='potato-export-bench-') as directory:
        reference = None
        # Bound: nine variants and at most 90 seconds for each FFmpeg process.
        variants = [('baseline', False, None), ('bounded', True, None), ('bounded-1', True, 1), ('bounded-4', True, 4), ('bounded-8', True, 8), ('hardware-auto', True, None), ('hardware-auto-4', True, 4), ('decoder-4', True, None), ('baseline-repeat', False, None)]
        if args.quick:
            variants = [v for v in variants if v[0] in {'baseline', 'hardware-auto', 'decoder-4', 'baseline-repeat'}]
        if args.layout:
            variants = [('baseline', True, None), ('nv12', True, None), ('nv12-decoder-4', True, None), ('nv12-repeat', True, None), ('baseline-repeat', True, None)]
        if args.presets:
            variants = [('baseline', True, None), ('preset-p5', True, None), ('preset-p4', True, None), ('baseline-repeat', True, None)]
        if args.multi_source:
            variants = [('baseline', False, None), ('bounded', True, None), ('bounded-repeat', True, None), ('baseline-repeat', False, None)]
        for name, bounded, threads in variants:
            output = Path(directory) / f'{name}.mp4'
            command = ['ffmpeg', '-v', 'error', '-y']
            if threads is not None:
                command += ['-filter_complex_threads', str(threads)]
            if name.startswith('hardware-auto'):
                command += ['-hwaccel', 'auto']
            if name in {'decoder-4', 'nv12-decoder-4'}:
                command += ['-threads', '4']
            if not args.multi_source:
                command += ['-ss', str(seek)]
            if bounded:
                command += ['-t', str(local_start + args.duration + .5)]
            chosen_graph = graph.replace('format=yuv420p', 'format=nv12') if name.startswith('nv12') else graph
            if args.multi_source:
                command = ['ffmpeg', '-v', 'error', '-y']
                pieces = []
                for source_index in range(2):
                    if bounded:
                        command += ['-ss', str(seek), '-t', str(local_start + args.duration + .5)]
                    command += ['-i', str(args.source)]
                    piece = graph.replace('[0:', f'[{source_index}:').replace('s_', f's{source_index}_')
                    piece = piece[:piece.index(f'[s{source_index}_vx0][s{source_index}_a0]concat')]
                    if not bounded:
                        piece = piece.replace(f'start={local_start}', f'start={args.start}')
                    pieces.append(piece)
                chosen_graph = ''.join(pieces) + '[s0_vx0][s0_a0][s1_vx0][s1_a0]concat=n=2:v=1:a=1[outv][outa]'
                command += ['-filter_complex_threads', '4']
                command += ['-filter_complex', chosen_graph, '-map', '[outv]', '-map', '[outa]']
            else:
                command += ['-i', str(args.source), '-filter_complex', chosen_graph, '-map', '[outv]', '-map', '[outa]']
            chosen_encoder = list(encoder)
            if name.startswith('preset-'):
                chosen_encoder[chosen_encoder.index('-preset') + 1] = name.removeprefix('preset-')
            command += chosen_encoder + ['-c:a', 'aac', '-b:a', '192k', '-pix_fmt', 'nv12' if name.startswith('nv12') else 'yuv420p', '-movflags', '+faststart', str(output)]
            started = time.monotonic()
            run(command)
            elapsed = time.monotonic() - started
            hashes = run(['ffmpeg', '-v', 'error', '-i', str(output), '-map', '0:v:0', '-map', '0:a:0', '-f', 'streamhash', '-hash', 'sha256', '-']).stdout.strip()
            if reference is None:
                reference = hashes
            result = dict(variant=name, seconds=round(elapsed, 3), bytes=output.stat().st_size, identical_decoded_streams=hashes == reference, decoded_stream_hashes=hashes, command=command)
            if args.presets:
                reference_graph = (f'[0:v]trim=start={local_start}:duration={args.duration},setpts=PTS-STARTPTS,'
                                   f'crop=trunc(iw/2)*2:trunc(ih*(0.96-0.03)/2)*2:0:trunc(ih*0.03/2)*2,scale={w}:{h},setsar=1,format=yuv420p,split[r0][r1];'
                                   '[1:v]split[e0][e1];[r0][e0]ssim[s];[r1][e1]psnr[p]')
                quality = run(['ffmpeg', '-v', 'info', '-ss', str(seek), '-t', str(local_start + args.duration + .5), '-i', str(args.source), '-i', str(output), '-filter_complex', reference_graph, '-map', '[s]', '-map', '[p]', '-f', 'null', '-']).stderr
                result['ssim'] = float(re.findall(r'All:([0-9.]+)', quality)[-1])
                result['psnr'] = float(re.findall(r'average:([0-9.]+)', quality)[-1])
            results.append(result)
            args.output.write_text(json.dumps(results, indent=2) + '\n')
            print(json.dumps({key: value for key, value in result.items() if key != 'command'}), flush=True)


if __name__ == '__main__':
    main()
