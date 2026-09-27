# Languages and translations

The addon's Lua shows its text in English, Simplified Chinese (`zh-cn`), Traditional Chinese
(`zh-tw`), Japanese (`ja`), Korean (`ko`), French (`fr`) and Russian (`ru`). That covers the
External Models library, Physics & Performance, the Static Prop tool, dialogs, notifications
and chat messages.

## Choosing the language

- **Automatic (default):** the addon uses the game's language (Options → Language, stored
  in `gmod_language`). If the addon has no translation for it, the addon uses English.
- **By hand:** use the globe button beside **Refresh** in the library, or the **Language**
  choice under **Utilities → Character Models**. The choice is saved in the
  `mmdhl_language` console variable (`mmdhl_language ja`; empty means automatic).
- Open library windows rebuild in the new language at once, keeping their selection, and
  the External Models tab is renamed. The rest of the spawn menu is rebuilt when it next
  closes: the Utilities pages, the tool gun's texts and panel, and the weapon list. This
  works the same way as Sandbox's own refresh when the game language changes.
- Each language uses a Windows UI font that has its characters: Microsoft YaHei UI for
  Simplified Chinese, Microsoft JhengHei UI for Traditional Chinese, Meiryo UI for
  Japanese and Malgun Gothic for Korean. The others use Segoe UI. Garry's Mod's own
  menus and dialogs keep the game's font.
- The server does not know a player's language. It sends its messages (spawn errors,
  sharing, notices) as language-neutral tokens, and each player sees them in their own
  language.

## Files

    addon/resource/localization/en/mmdhl.properties      English, the source
    addon/resource/localization/<code>/mmdhl.properties  one file per translation

The folder names are the game's lowercase language codes (`zh-cn`, not `zh-CN`). Any phrase a
translation leaves out, or leaves empty, is shown in English.

```
# Status line after moving library items into a folder.
# {count}: number of items. {folder}: folder path, or the word for “Unfiled”.
mmdhl.ui.moved_items=Moved {count} item(s) to {folder}.
```

- One phrase per line: `key=text`, UTF-8. `\n` is a line break; `\uXXXX` escapes (as in the
  game's own files) are also understood. Spaces at the start or end are ignored.
- In the English file, the comment above each phrase says where it appears and what each
  `{placeholder}` contains. Keep every placeholder spelled exactly; you can move it.
- In a translation, each phrase follows the English it was translated from (`# en: …`).
  When the English changes later, the checker reports that phrase as outdated.
- Each translation starts with a glossary: the words chosen for the addon's own tabs and
  buttons, and the game's terms. Garry's Mod features (Physics Gun, Face Poser, spawn
  menu, Undo…) use the game's official translation. Product names, file formats, console
  commands and key names stay as they are.

## Reviewing English and translations

1. `python scripts/check-i18n.py` checks everything: keys used in Lua but missing from the
   English file, unused phrases, placeholders a call does not pass, and for each
   translation how many phrases are translated, missing or outdated, placeholders that
   differ from English, and a different number of line breaks. It exits with an error when
   placeholders do not match.
2. `python scripts/check-i18n.py --csv review.csv` writes a spreadsheet with one row per
   phrase: key, English, one column per translation, placeholders, context, the Lua lines
   that use it, and a notes column.
3. In game, `mmdhl_i18n_debug 1` wraps every catalogue phrase in brackets, accents it and
   makes it about a third longer; open windows rebuild at once.
   - Text without brackets is still hard-coded, except the names the game shows itself
     (see above) and the game's own words such as NONE on key buttons.
   - A clipped `~…` shows where a longer translation would not fit.
   - `mmdhl_i18n_debug 2` shows each phrase's key instead, and `0` turns the check off.

## Adding or updating a translation

1. New language: `python scripts/check-i18n.py --sync de` creates
   `addon/resource/localization/de/mmdhl.properties` with every English phrase as a
   `# en:` line and an empty value. Add `de` with its own name to `I.Languages` in
   `addon/lua/mmdhl/i18n.lua` so it appears in the language list; automatic selection
   works without that.
2. Fill in values, in the file itself or in separate `key=text` files that you merge:
   `python scripts/check-i18n.py --sync de --merge new.properties [--glossary terms.txt]`.
3. After the English changes, run `--sync` for each language. New phrases then appear empty
   and removed ones disappear. Retranslate the phrases reported as outdated and merge them;
   merging records the current English in their `# en:` lines.
4. Check in game with `mmdhl_language de`.

## What stays in English

- **Messages written by the native module.** The Lua shows them as they are. This includes
  import errors, conversion warnings and notes, and compatibility and installation checks
  reported by the DLL. Some sit inside translated sentences, for example
  `Import failed: {reason}`. The library also recognises a few of them by their English
  wording to offer a hint. Translating them means changing the native messages and those
  hints together.
- **Console variable help text**, console commands, and developer diagnostics: the
  performance overlay, performance dumps and log lines. They are meant for bug reports.
- **Product names**, and model, bone and morph names from the files themselves.

## For developers: adding text

- Look phrases up with `L'area.name'` or `L('area.name',{count=n})`, where
  `local L=mmdhl.L`. The catalogue key is `mmdhl.area.name`. Add a context comment above
  every new phrase, then run `--sync` for each language.
- Write whole sentences with placeholders. Never join translated fragments; word order
  differs between languages.
- On the server, `mmdhl.L` returns a token rather than text. Tokens can be sent to clients
  in net messages, status values and callbacks, and `mmdhl.ChatPrint(player,text)` shows
  one in chat. The client renders any text it receives from the server with
  `mmdhl.Localize` before showing it. Server logs render tokens in English with
  `mmdhl.Localize` too.
- Logic must never compare display text; compare ids.
- UI built once should rebuild on `hook.Add('MMDHL.LanguageChanged',…)`. A table of labels
  built when a file loads keeps the language of that moment; wrap its entries with
  `mmdhl.I18n.Lazy(entry,{name=function() return L'area.name' end})` so each read looks the
  label up.
- Some names are shown by the game itself: the tool gun's texts, undo entries, context menu
  `MenuLabel`s, the cleanup type, and weapon and entity names. `i18n.lua` registers these
  with `language.Add` or copies them into the stored tables, and repeats that when the
  language changes. `menu.lua` then renames the creation tab and rebuilds the spawn menu.
- Fonts come from `mmdhl.I18n.FontData(size,weight)`. It picks the language's face and
  adjusts its size, because these faces render smaller at the same size. It also snaps the
  weight to regular or bold, since other weights smear.
- `tests/test_i18n.py` covers the lookup, escapes, fallback, the language choice and
  server tokens.
