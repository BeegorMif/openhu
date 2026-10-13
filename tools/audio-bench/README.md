# Audio benchmarks

Two measurements of the Android Auto audio output (`QtAudioOutput`), both at the default sink's monitor in
PipeWire, so they include everything OpenHU buffers.

## audio_bench: synthetic, without a phone

Feeds `QtAudioOutput` like the io_service thread does: Android Auto packets (media 2048 frames stereo 48 kHz,
guidance 1024 frames mono 16 kHz) with the packet timing of a protocol capture (`gaps-media.txt`: the gaps
between media packets of a wireless connection in ms, nothing else from the capture). Every half second a
marker (a short tone burst whose length encodes its number) measures the latency from `write()` to the
monitor; silence inside the signal counts as a dropout.

Build in the development container (from the repository root):

    docker run --rm -u $(id -u):$(id -g) -e CCACHE_DISABLE=1 -v $PWD:/src:ro -v openhu-aasdk:/opt/aasdk \
        -v $PWD/docker/output:/out openhu:dev bash -c 'cmake -S /src/tools/audio-bench -B /tmp/b -G Ninja \
        -DCMAKE_PREFIX_PATH=/opt/aasdk && cmake --build /tmp/b && cp /tmp/b/audio_bench /out/'

Run it as the session user of an openhu-os VM, best one without host audio (`scripts/run-vm.sh` with an
`XDG_RUNTIME_DIR` without a PipeWire socket), the test tone is audible otherwise:

    runuser -u openhu -- env XDG_RUNTIME_DIR=/run/user/900 ./audio_bench --stream media --seconds 60 \
        --timing gaps-media.txt

| Option | |
|---|---|
| `--stream media\|guidance` | packet format |
| `--timing FILE` | packet gaps in ms (default: exactly one packet length) |
| `--burst N` | the first N packets at once (phones send 4–6 ahead after some starts) |
| `--hiccup MS --hiccup-every S` | a transmission stall, then the late packets at once |
| `--rate F` | phone clock against sound card, e.g. 1.0005 |
| `--ui-busy MS --ui-period MS`, `--spike MS --spike-every MS` | load on the Qt main thread |

Output: `lat_*` latency in ms (median, p95, first and last marker), `gaps`/`gap_ms` dropouts,
`inserted_ms`/`lost_ms` from the marker spacing. In a VM PipeWire forces `min-quantum` 1024, real hardware can
be lower.

## aa_bench.py: with a real phone

With Android Auto connected and music playing, it sends `media.pause`/`media.play` through the API and measures
how long until the audio stops/starts at the monitor, plus dropouts during continuous playback. The phone's
reaction time is part of both values, so compare two OpenHU builds with the same phone and player.

    runuser -u openhu -- env XDG_RUNTIME_DIR=/run/user/900 python3 aa_bench.py --label new

Needs python3-websockets (on openhu-os). The music pauses and plays audibly during the run.
