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

Device checks: open an existing GBK sample from TF, scroll, switch between Auto
and GBK, and return; then open an existing UTF-8 sample. Confirm Chinese text,
header selection and scrolling are usable. Repeat from the media library and
on-board file browser. Check large files still stop at the bounded prefix,
and exiting/reopening does not retain a previous file's state or leak buffers.
