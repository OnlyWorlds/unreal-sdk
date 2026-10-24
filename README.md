# OnlyWorlds for Unreal: the core

The engine-independent core of an Unreal plugin for [OnlyWorlds](https://onlyworlds.com) world folders and `.owworld`
world files. Plain C++17, the standard library and zlib only; no Unreal header in `Core/`. It will sit under a UE
Runtime module, with UObject only at the edges.

| | |
|---|---|
| `OWJson` | strict JSON (RFC 8259) whose value keeps key order and each number's source text; the folder spec's section 5 writer (`JSON.stringify(value, null, 2)`'s bytes) |
| `OWUnicode` | NFC, the OW-18 fold (NFC, then CaseFolding's C and S rows), strict UTF-8, Python's `strip()` white space, the slug's NFKD: Unicode 15.1.0, generated from Unicode's own files |
| `OWZip` | the archive's own central-directory walk, each entry inflated by zlib and held to both sizes it declares, a writer with fixed stamps |
| `OWWorldArchive` | reads an `.owworld` by every reader rule of section 4.7 (OW-1 to OW-22), refusing with one cause; packs a folder |
| `OWWorld` | a world folder read and written back: an unedited file keeps its bytes, an edited one is written in section 5's form, a new element takes section 3.2's filename |

## Build and test (Windows, MSVC)

```
build.cmd                  # build\ow_tests.exe, linking the engine's zlib 1.3 (UE_ROOT, default UE_5.8), /MD
build\ow_tests.exe         # every test; exits 0 only when none failed (--filter=<text>, --require-all)
python codegen/generate.py --check                    # the generated Unicode tables match the vendored files
python tests/check_writer.py build/out/pass-good-repacked.owworld   # the reference checker on what pack wrote
```

The tests read the `.owworld` conformance set from the vendored schema-dist (`codegen/schema-dist/fixtures/owworld`,
pinned in `codegen/PIN`): every reader verdict and its reason. Two real worlds are read from this machine when present
(`OW_SIKELIA`, `OW_MOPPETOPIA`); a missing world is a skip, named, never a pass.

`codegen/schema-dist/` is vendored verbatim; never edit it. `Core/Private/OWUnicodeData.generated.cpp` is written by
`codegen/generate.py`; regenerate, never edit.

## Not built yet

The Unreal module (`.uplugin`, Build.cs), the generated USTRUCT models, the Blueprint library and the automation tests.
No platform other than Win64 has been built.
