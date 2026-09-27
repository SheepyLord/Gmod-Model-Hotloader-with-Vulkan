# Model terms of use

Many models are published only for making videos with MikuMikuDance. Their
terms often forbid using them in games or other software, sharing or
re-uploading them, or showing them in sexual or violent content. Model
Hotloader cannot check or enforce these terms, and importing a model grants no
right to use it. The player is responsible for following each model's terms,
its author's wishes and the law; the addon's authors grant no rights to any
model and accept no liability for how one is used. The addon's part is to put
the model's own texts in front of the player at the moments that matter:
importing and sharing.

## Importing

After the model file is chosen (and before anything is imported), a
**Terms of use** window shows:

- a warning about common restrictions and the disclaimer above;
- the topics the texts talk about: sharing, games or other software, videos,
  sexual content, violence, commercial use, modification, credit;
- **Rules at a glance**: the lines that state rules about those topics (for
  example `・再配布は禁止します。` or `Do not redistribute this model.`), and a
  VRM licence's permission fields;
- one tab per text: the comment stored in the model, a glTF copyright, the VRM
  licence, and each readme (a tab marked ↑ comes from the folder above).

**Accept and import** is enabled once the player ticks *I have read the model's
terms and take full responsibility for how I use it*. Cancel or closing the
window cancels the import. The window also appears for a model with no texts
at all: it then says that none were found and that the download page is where
to look. Topic detection is a keyword aid in eight languages, not a legal
reading. A text that mentions no topic may still restrict use.

Reimporting a prop, saving a prop preset and importing the same file again as
the other kind (character/prop) do not ask again.

## Exporting

The export window shows how many selected models mention restrictions or have
no recorded terms (models imported before this version). **Export package**
then lists each model with what its texts mention, flags VRM licences that
forbid redistribution, and asks the player to confirm that each model's terms
allow sharing it this way.

## Switching the warnings off

Both windows have **Don't show this warning again**, which takes effect when
the player accepts (cancelling never switches a warning off). The settings are
the client convars `mmdhl_terms_warning_import` and `mmdhl_terms_warning_export`
(1 = ask, the default), shown as checkboxes under **Utilities → User →
Character Models**. With the import warning off, the texts are still read and
kept.

## Where the texts come from

`native.InspectModelNotes(path)` (client module, `native/model_notes.cpp`)
reads, without importing anything:

- **Embedded texts**: PMX name, English name, comment and English comment
  (UTF-16LE or UTF-8, as the header says); PMD name and comment (Shift-JIS).
- **glTF/GLB/VRM**: the `asset.copyright` field, and the VRM 0.x or 1.0 licence
  metadata (the same reader the VRM importer uses). A GLB's JSON chunk is read
  up to 64 MB, a `.gltf` file up to 32 MB.
- **Readme files**: `.txt`, `.md`, `.text`, `.nfo`, and extensionless
  `license`/`licence`/`copying`/`readme`.
  - In the model's own folder, every text file is read when the folder holds at
    most 3 model files and 12 text files: a folder as models are distributed.
    In a crowded folder, only files named like readmes are read.
  - In the folder above, only files named like readmes (never a drive root).
  - Readme names include readme, license, terms, eula, 利用規約, 規約, 読んで,
    必読, 許諾, 著作権, 使用说明, 条款, 授權, 라이선스, 약관, … (strong), and
    説明, 注意, はじめに, credit, about, notice, 설명, … (weak). Strong names
    come first, then the model's own folder, then alphabetical order.
  - At most 8 files, each at most 1 MB; binary-looking files are skipped; the
    first 48 KB of each is kept.

Text encodings are detected per file: a UTF-8/UTF-16 byte order mark, then
strict UTF-8, then Shift-JIS, UHC (Korean), GBK or Big5, chosen by which one
decodes and which script the result looks like (kana for Japanese, hangul for
Korean, simplified or traditional hanzi), with Shift-JIS as the lossy
fallback. Line endings are normalized and control characters removed.

## What is kept

After a successful import the texts are saved to
`garrysmod/data/mmd_hotloader/terms/<asset id>.json` with the file name, the
topics found and when the player accepted. A saved prop preset uses its
original prop's record. Deleting a model deletes its record. The player already
acknowledged the terms at import, so the library shows no banner above the
preview: **Terms of use** in a model's right-click menu (present when it has a
record) opens the same tabs read-only. Characters imported before this version
show their VRM licence there when they have one.

## Workshop packages

Each package item carries the model's `terms`: the embedded texts, the VRM
licence and the readme-named files (other text that happened to sit beside the
model, such as a player's own notes, is left out), each readme cut to 32 KB and
the whole to 240 KB. When a package installs, its terms become the model's
record, marked with the package title, unless the player already has their own
record for that model. Package terms are untrusted: only known fields are kept,
every text is bounded, and at most 8 readmes are read.

## Source engine text limits

- A label keeps only 1023 bytes of text, so each paragraph of the warnings is a
  label of its own. Every `terms.*` phrase must stay below that in every
  language (`tests/test_model_terms.py` checks this).
- Source wraps labels only at spaces, so a Japanese or Chinese sentence after a
  Latin word would start a new line early. The warnings wrap such text
  themselves (`mmdhl.terms.WrapLabel`): after any Han, Kana or full-width
  character, and at spaces between other words.
- Text starting with `#` is a localization token to Source and is looked up
  through a 1024-character buffer, which would cut off a Markdown readme. The
  first `#` is sent to the text view on its own.

## Tests

- `build/bin/Release/mmdhl_model_notes_tests.exe` (CTest `model_terms_of_use`):
  readme discovery, every encoding, PMX/PMD comments, VRM 0.x/1.0 licences,
  glTF copyright, a missing model.
- `python tests/test_model_terms.py`: topic and rule detection, the import and
  export acknowledgements and their dismissal, records, reimports and presets,
  derived props, untrusted package terms, package subsets, Workshop installs,
  the library summary, the viewer and the Source text limits.

In game (2026-09-25), a fixture PMX in a Japanese-named folder was imported
through the library, with a Shift-JIS `【必読】利用規約.txt`, an English readme and
a note in the folder above. The window showed all three texts decoded, with 9
rule lines and 8 topics, and reading them took 0.6 ms. Tested: Accept stays
disabled until ticked; the record is saved; the library button and viewer;
the export summary and confirmation; the terms inside the exported `.gma`;
switching both warnings off and back on in Utilities; the layout in English,
Japanese, Simplified Chinese and Russian.
