# QR Bench

Desktop benchmark for QR decode strategies.

## Build

```bash
cmake -B tools/qr_bench/build -S tools/qr_bench
cmake --build tools/qr_bench/build -- -j$(nproc)
```

## Run

```bash
./tools/qr_bench/build/qr_bench tmp_imtoken_qr
```

The tool scans `.png` files, reads the matching `.txt` sidecar when present,
and compares several decode strategies:

- `zbar_raw`
- `zbar_contrast`
- `quirc_raw`
- `quirc_inverted`
- `quirc_contrast`
- `quirc_contrast_inv`

It prints per-file matches and a summary with average decode time.
