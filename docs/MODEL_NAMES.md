# Model names in the player's language

Text that comes from model files (model names, material/part names,
expression names) is usually Japanese or Chinese. The addon shows it in the
player's language, the way the Cats Blender plugin translates MMD models, but
without ever making the player wait.

Code: `addon/lua/mmdhl/names.lua` (client). Tests: `tests/test_model_names.py`.

## Where names are translated

- Library: character and prop rows (searchable by either name), the selected
  model's title (original name in the tooltip) and its status line.
- Bodygroup editor, entity editor (visible parts, expressions), Face Poser
  labels, props parts editor, collision editor regions: the translation with
  the original in brackets, `Hair (髪)`.
- Static Prop tool list, export window checklist, spawn-menu NPC names (shown
  on the spawn menu's next rebuild).

Translation happens only when a name is shown. `entry.name`, package items,
preset names and the stock bodygroup names (which Source uses as lookup keys)
keep the names as authored. Names the player typed (renames, prop presets)
are never translated.

## Sources, in order

1. The model's own English name, for English readers: the PMX English model
   name (kept in the model's terms-of-use record, see MODEL_TERMS.md) and the
   PMX English morph name. No request is made.
2. The translation cache, `data/mmd_hotloader/translations/<language>.json`,
   stored as `[source, translation]` pairs; `false` marks names that are
   already in the target language.
3. Google Translate: `https://clients5.google.com/translate_a/t?client=dict-chrome-ex&sl=auto&tl=<language>`,
   the keyless endpoint of Google's browser extension. POST form data with one
   `q` per name; the answer is one `[translation, detected language]` pair per
   name. (The `translate_a/single` endpoint Cats-style tools use answered
   "429 automated queries" from the development machine; this one did not.)

The target is the addon's chosen language, else `gmod_language` (a German game
gets German names even though the addon itself is English). GMod codes map to
Google's: `zh-cn`→`zh-CN`, `zh-tw`→`zh-TW`, `pt-br`→`pt`, `es-es`→`es`, and
so on. English readers do not send names that are already ASCII; digits and
punctuation alone, and texts over 200 bytes, are never sent. Translations into
Latin and Cyrillic scripts start with a capital letter.

## Never in the way

- Lookups return the original immediately and queue the name.
- A 0.5 s timer sends one request at a time: at most 40 names and 4000 bytes
  of form data, 15 s timeout, only after `InitPostEntity` (HTTP does not work
  while the game loads).
- On an error, a timeout, a 429 or an unreadable answer: wait 15 s, 30 s,
  1 min … up to 10 min; after six failures, pause until the language changes or
  the setting is switched. Physics & Performance then says Google Translate is
  not answering.
- When translations arrive, hook `MMDHL.NamesTranslated` fires: the library
  redraws, and controls registered with `mmdhl.names.Bind` in windows that are
  already open are relabelled.

## Consent

Names are shared with Google, so:

- `mmdhl_translate_names` (default 1) is the main switch, shown in
  Physics & Performance → Model name translation. Off: names show as written
  and nothing is sent. "Restore defaults" there leaves it alone.
- The import window has **Show this model's names in my language** (default
  on, shown while the main switch is on) with a note on what is sent. The
  choice is stored for that model in `translations/models.json` and remembered
  for the next import (`mmdhl_translate_new_imports`). With the import warning
  dismissed, the remembered choice applies.
- Models without a stored choice (imported before this existed, installed from
  the Workshop, shared by a server) are sent only after the player has seen
  what is shared (`mmdhl_translate_notice_seen`): by answering an import window,
  by changing the setting, or through the one-time library notice
  (**Translate names** / **Keep original names**, which switches translation
  off).
- A model whose choice is "no" never has its names sent, and cached
  translations are not shown for it either. Deleting a model forgets its
  choice.
- A queued name remembers which models asked for it and is sent only while one
  of them is still allowed, checked again when its request is made: a choice
  changed while the name waits (or backs off after a failure) takes effect for
  names already queued, and deleting a model drops its queued names.

## Validation (2026-09-25)

`tests/test_model_names.py`: target languages, background queue and batching,
cache across sessions, identity and response shapes, back-off and recovery,
consent and the notice, library names, relabelling of open windows, batch
limits, consent changed while names are queued or backing off.

In game, with a test PMX whose model, parts and one expression were given
Japanese names (テストミク, 髪, スカート, まばたき):

- The import window showed the translation choice.
- English: "Generated fixture" (the PMX English name, no request) and
  "Hair (髪)", "Skirt (スカート)" about a second after import.
- French: "Tester Miku", "Cheveux (髪)", "Jupe (スカート)", relabelled in a
  bodygroup editor that was already open.
- Entity editor parts, collision regions and title were translated.
- An unreachable host showed the "not answering" line and nothing blocked.
- Switching off showed the names as written at once.
- The library notice appeared for models imported earlier, with nothing
  queued until it was answered.
