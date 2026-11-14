# H.264 missing-SPS regression

Run `packages/artinchip/mpp/tests/run-h264-missing-sps.sh` from the SDK.
It builds the actual PPS parser in `h264_nal.c` with ASan/UBSan and a host
allocation shim. Public MPP/decoder headers are used unchanged; only the
platform integer typedef header is replaced for the host.

A bounded PPS referring to SPS 0 without a preceding SPS must return an error.
The original parser dereferenced its null SPS while copying scaling matrices.
An optional source-file argument lets the same test reproduce that failure
against the original `h264_nal.c`. This test does not exercise the VE hardware,
physical memory allocator, live SPS payloads or visible display.

The v15 wired screen trace exposed two application sink defects: SPS/PPS
caused independent decoder resets, and packet submission waited without
pumping the two-slot decoder. Those are fixed separately in rt-carplay.
The SDK now rejects missing SPS references and propagates frame-buffer setup
failure instead of using absent or partially allocated buffers. The exact
values behind the negative allocation in the hardware trace remain unmeasured.
