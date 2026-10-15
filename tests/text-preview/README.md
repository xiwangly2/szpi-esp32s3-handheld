# Text Preview Encoding Checks

Run from the repository root:

```powershell
cmake -S tests/text-preview -B tests/text-preview/build
cmake --build tests/text-preview/build --config Debug
ctest --test-dir tests/text-preview/build -C Debug --output-on-failure
```

The host target compiles the production decoder and the unchanged repository
FatFs `ffunicode.c` CP936 table. A small forced-include shim supplies only the
FatFs types/options needed by that table, bypassing ESP/RTOS headers. No test-only
Chinese lookup implementation substitutes for the real mapping.

Coverage includes ASCII/UTF-8, UTF-8 BOM, GBK Chinese punctuation, the CP936 euro
byte, both UTF-16 byte orders/BOMs, surrogate pairs, malformed UTF-8/surrogates,
embedded NUL/control bytes, CRLF normalization, ambiguous UTF-8/GBK bytes, empty
input, bounded output canaries, and incomplete characters at a 4096-byte prefix.
An additional 12000 deterministic malformed-input/capacity cases check bounds;
on Windows their outputs are also independently validated as UTF-8.

On Windows, extra filename arguments exercise real files and compare the
firmware output against Windows `MultiByteToWideChar`/`WideCharToMultiByte`, with
the same input boundary and whitespace normalization. Unicode filenames use
`wmain`/`_wfopen` rather than the process ANSI code page. Example:

```powershell
.\tests\text-preview\build\Debug\text_preview_tests.exe 'E:\path\sample.txt'
```

On 2026-10-15, six user-provided samples (five CP936 and one UTF-8, from 311 to
669950 bytes) passed this comparison with zero replacement characters in their
preview prefixes. Their original files were read only, not rewritten or added
to the repository. This establishes conversion correctness, not LCD rendering.

The mapped-window decoder is additionally checked by slicing long generated
GBK and UTF-8 texts into thousands of small pages and reassembling them. The
result must exactly match normalized text, including CRLF split at a read
boundary, with no skipped/duplicated characters. Optional Windows file arguments
also run a full-file mapped-page comparison against the Windows decoder. Only
the initial 4 KiB comparisons above were run on the user's original six samples;
the card was back in the device before the new full-file host test was available.

Device checks: open an existing GBK sample from TF, swipe left/right, switch between Auto
and GBK, and return; then open an existing UTF-8 sample. Confirm Chinese text,
header selection and page turns are usable. Repeat from the media library and
on-board file browser. Read past the old 4 KiB limit, then exit/reopen and check
resume. Restart and check persistence. Rapidly switch books during loading;
only the current page may update. Check save failures/corrupt checkpoint fallback
and returning to normal operation after cancelling a reader. Real display,
timing, gesture, and power-loss behavior cannot be proven by host decoder tests.

## Device Evidence

On 2026-10-15 the user confirmed the encoding-only `1ffd163` firmware displayed
Chinese correctly and allowed encoding selection/scrolling/Back, but reported
that the long-text control still felt slow. It was superseded by the paginated
reader in `26e9e3a`.

For `26e9e3a`, COM6 logs recorded GBK reading through page 49 (offset 6824), past
the previous 4096-byte preview cap, and UTF-8 page turns. Observed page-job times
were 4-31 ms; these measure read/decode/layout/UI submission, not physical touch
latency or LCD scanout. Reopening restored separate books at pages 48, 11 and
24. Worker exits reported at least 3824 bytes of free stack. No panic appeared
in this capture. The user confirmed smooth page turns, correct Chinese, and
working resume. This is bounded acceptance; abrupt power loss, every encoding
choice, read-only/full cards, and the maximum file/page limits remain broader
test cases, not qualified by this short run.

Packaged firmware: `release/szpi-esp32s3-handheld-26e9e3a-text-reader.bin`.
SHA-256: `F5122DB8D881ABC39D897672D25B493552DDE3CF1C5D9B0DC093F788118C4E29`.
