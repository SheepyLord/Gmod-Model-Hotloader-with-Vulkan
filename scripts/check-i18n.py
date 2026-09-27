"""Check the addon's translation catalogues against the Lua that uses them.

  python scripts/check-i18n.py                  check English and every translation
  python scripts/check-i18n.py --csv out.csv    also export a review sheet (key, English,
                                                placeholders, context, where it is used)
  python scripts/check-i18n.py --template de    print a translation file for a new language
  python scripts/check-i18n.py --sync ja [--merge new.properties ...] [--glossary terms.txt]
                                                rewrite a translation in the English order:
                                                new phrases appear empty, removed ones go,
                                                merged files supply new or updated phrases

The English catalogue is addon/resource/localization/en/mmdhl.properties. Lua looks
phrases up with L('key',{name=value}) or mmdhl.L(...); the catalogue key is mmdhl.<key>.
Dynamic keys must be listed in a comment:  -- i18n-keys: props.mode.none props.mode.world
Each translated phrase is preceded by the English it translates (# en: ...), so phrases
whose English changed since are reported as outdated.
Exit status is 1 when there are errors (warnings alone exit 0).
"""
import argparse, csv, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
LUA = ROOT / 'addon/lua'
LOCALIZATION = ROOT / 'addon/resource/localization'
ENGLISH = LOCALIZATION / 'en/mmdhl.properties'
KEY = re.compile(r'^mmdhl(\.[a-z0-9_]+){2,}$')
PLACEHOLDER = re.compile(r'\{([A-Za-z0-9_]+)\}')
# L after a concatenation (..L'x') counts; a field or method named L (x.L, x:L) does not.
CALL = re.compile(r'''(?:(?<![\w.:])mmdhl\.|(?<![\w:])(?<![^.]\.))L\s*\(?\s*(['"])([A-Za-z0-9_.]+)\1''')
LISTED = re.compile(r'--\s*i18n-keys:\s*(.+)')
SECTION = re.compile(r'^#\s*=====')


class Entry:
    def __init__(self, key, value, context, en, line, section):
        self.key, self.value, self.context, self.en, self.line, self.section = key, value, context, en, line, section


def parse(path):
    """Entries in file order (key -> Entry), duplicates, the leading comment block and
    the numbers of lines that are not key=value (the game drops them).
    Mirrors I.Parse in i18n.lua; comments are kept for context, # en: and sections."""
    entries, duplicates, comment, header, section, malformed = {}, [], [], [], None, []
    text = path.read_text(encoding='utf-8-sig')
    started = False
    for number, line in enumerate(text.splitlines(), 1):
        stripped = line.strip()
        if SECTION.match(stripped):
            started, section, comment = True, stripped, []
            continue
        if not stripped:
            if not started and comment:
                header.extend(comment + [''])
            comment = []
            continue
        if stripped[0] in '#!':
            comment.append(stripped[1:].strip())
            continue
        match = re.match(r'^\s*([^=\s]+)\s*=(.*)$', line)
        if not match:
            malformed.append(number)
            continue
        started = True
        key, value = match.group(1), match.group(2).strip()
        if key in entries:
            duplicates.append((key, number))
        en = next((c[3:].strip() for c in comment if c.startswith('en:')), None)
        context = ' '.join(c for c in comment if not c.startswith('en:'))
        entries[key] = Entry(key, value, context, en, number, section)
        comment = []
    while header and header[-1] == '':
        header.pop()
    return entries, duplicates, header, malformed


def malformed_errors(path, lines):
    return [f'{path}:{line}: not a key=value line; keep each phrase on one line (write \\n for a line break)' for line in lines]


def call_vars(source, start):
    """Variable names of a literal {name=...} table after the key, or None when not literal."""
    i = start
    while i < len(source) and source[i] in ' \t':
        i += 1
    if i >= len(source) or source[i] != ',':
        return set()
    i += 1
    while i < len(source) and source[i] in ' \t':
        i += 1
    if i >= len(source) or source[i] != '{':
        return None
    depth, names, j = 0, set(), i
    while j < len(source):
        c = source[j]
        if c in '\'"':
            end = j + 1
            while end < len(source) and source[end] != c:
                end += 2 if source[end] == '\\' else 1
            j = end
        elif c in '{([':
            depth += 1
            if depth == 1 and c == '{':
                m = re.match(r'\s*([A-Za-z_]\w*)\s*=(?!=)', source[j + 1:])
                if m:
                    names.add(m.group(1))
        elif c in '})]':
            depth -= 1
            if depth == 0:
                return names
        elif c == ',' and depth == 1:
            m = re.match(r'\s*([A-Za-z_]\w*)\s*=(?!=)', source[j + 1:])
            if m:
                names.add(m.group(1))
        j += 1
    return None


def usages():
    """key -> list of (file:line, vars or None)."""
    found = {}
    for path in sorted(LUA.rglob('*.lua')):
        source = path.read_text(encoding='utf-8')
        rel = path.relative_to(ROOT).as_posix()
        for m in CALL.finditer(source):
            if m.group(2).endswith('.'):
                continue  # a key completed at run time; its keys are listed in an i18n-keys comment
            line = source.count('\n', 0, m.start()) + 1
            found.setdefault('mmdhl.' + m.group(2), []).append((f'{rel}:{line}', call_vars(source, m.end())))
        for m in LISTED.finditer(source):
            line = source.count('\n', 0, m.start()) + 1
            for key in m.group(1).split():
                found.setdefault('mmdhl.' + key, []).append((f'{rel}:{line}', None))
    return found


def translation_file(language, english, entries, header):
    """A translation in the English order: sections, # en: lines and values."""
    out = header[:] if header else [f'Model Hotloader: {language} translation.',
                                     'Each phrase follows the English it translates (# en:). An empty value keeps the English.',
                                     'Keep every {placeholder} unchanged; see docs/TRANSLATING.md.']
    out = ['# ' + line if line else '#' for line in out]
    section = None
    for key, source in english.items():
        if source.section != section:
            section = source.section
            out += ['', section]
        entry = entries.get(key)
        value = entry.value if entry else ''
        en = entry.en if entry and entry.value and entry.en is not None else source.value
        out += ['', f'# en: {en}', f'{key}={value}']
    return '\n'.join(out) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--csv', help='write a review sheet to this path')
    parser.add_argument('--template', metavar='LANGUAGE', help='print a translation file for LANGUAGE')
    parser.add_argument('--sync', metavar='LANGUAGE', help='rewrite LANGUAGE in the English order')
    parser.add_argument('--merge', nargs='+', default=[], help='with --sync: files of new key=value translations')
    parser.add_argument('--glossary', help='with --sync: text file placed in the header as comments')
    parser.add_argument('--catalog', action='append', help='English catalogue file(s) instead of the addon one')
    args = parser.parse_args()
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8')
    errors, warnings = [], []
    catalogs = [pathlib.Path(c) for c in args.catalog] if args.catalog else [ENGLISH]
    english = {}
    for catalog in catalogs:
        entries, duplicates, _, malformed = parse(catalog)
        errors.extend(malformed_errors(catalog, malformed))
        for key, line in duplicates:
            errors.append(f'{catalog}:{line}: duplicate key {key}')
        for key in entries:
            if key in english:
                errors.append(f'{catalog}: {key} is also defined in another catalogue')
        english.update(entries)
    if args.template:
        sys.stdout.write(translation_file(args.template, english, {}, None))
        return 0
    if args.sync:
        path = LOCALIZATION / args.sync / 'mmdhl.properties'
        entries, header, unreadable = ({}, None, [])
        if path.is_file():
            entries, _, header, malformed = parse(path)
            unreadable += malformed_errors(path, malformed)
        if args.glossary:
            header = (header or [f'Model Hotloader: {args.sync} translation.',
                                 'Each phrase follows the English it translates (# en:). An empty value keeps the English.',
                                 'Keep every {placeholder} unchanged; see docs/TRANSLATING.md.'])
            # The glossary is the rest of the header after its title line; replace it whole.
            start = next((i for i, line in enumerate(header) if line.startswith('Glossary')), len(header))
            header = header[:start]
            while header and header[-1] == '':
                header.pop()
            header += [''] + pathlib.Path(args.glossary).read_text(encoding='utf-8-sig').rstrip().splitlines()
        for merge in args.merge:
            merged, duplicates, _, malformed = parse(pathlib.Path(merge))
            unreadable += malformed_errors(merge, malformed)
            for key, line in duplicates:
                warnings.append(f'{merge}:{line}: duplicate key {key}; the last one wins')
            for key, entry in merged.items():
                if key not in english:
                    errors.append(f'{merge}:{entry.line}: {key} is not an English phrase')
                    continue
                entry.en = english[key].value
                entries[key] = entry
        removed = [key for key in entries if key not in english]
        errors.extend(unreadable)
        if unreadable:
            # Rewriting would silently drop the text on those lines.
            print(f'Did not write {path}: fix the lines that are not key=value first')
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(translation_file(args.sync, english, entries, header).replace('\n', '\r\n').encode('utf-8'))
            print(f'Wrote {path} ({sum(1 for k in english if k in entries and entries[k].value)}/{len(english)} translated, {len(removed)} obsolete phrases removed)')
    used = usages()
    for key, entry in english.items():
        where = f'{catalogs[0].name}:{entry.line}'
        if not KEY.match(key):
            errors.append(f'{where}: key {key} must look like mmdhl.area.name (lowercase, digits, underscores)')
        if not entry.value:
            errors.append(f'{where}: {key} has no English text')
        if not entry.context:
            warnings.append(f'{where}: {key} has no context comment for translators')
        if key not in used:
            warnings.append(f'{where}: {key} is not used by any Lua file')
    for key, sites in sorted(used.items()):
        if key not in english:
            errors.append(f'{sites[0][0]}: {key} is not in the English catalogue')
            continue
        placeholders = set(PLACEHOLDER.findall(english[key].value))
        for site, names in sites:
            if names is None:
                continue
            for missing in sorted(placeholders - names):
                errors.append(f'{site}: {key} needs {{{missing}}} but the call does not pass it')
            for extra in sorted(names - placeholders):
                warnings.append(f'{site}: {key} is passed {extra}, which its text does not use')
    if LOCALIZATION.is_dir() and not args.catalog:
        for folder in sorted(p for p in LOCALIZATION.iterdir() if p.is_dir() and p.name != 'en'):
            path = folder / 'mmdhl.properties'
            if not path.is_file():
                continue
            if folder.name != folder.name.lower():
                errors.append(f'{folder}: language folders must be lowercase, like the game\'s (e.g. zh-cn)')
            entries, duplicates, _, malformed = parse(path)
            errors.extend(malformed_errors(path, malformed))
            for key, line in duplicates:
                errors.append(f'{path}:{line}: duplicate key {key}')
            translated = outdated = 0
            for key, entry in entries.items():
                if key not in english:
                    warnings.append(f'{path}:{entry.line}: {key} no longer exists in English; run --sync {folder.name}')
                    continue
                if not entry.value:
                    continue
                translated += 1
                source = english[key].value
                want, got = sorted(PLACEHOLDER.findall(source)), sorted(PLACEHOLDER.findall(entry.value))
                if want != got:
                    errors.append(f'{path}:{entry.line}: {key} placeholders {got} differ from English {want}')
                if source.count('\\n') != entry.value.count('\\n'):
                    warnings.append(f'{path}:{entry.line}: {key} has a different number of line breaks than English')
                if entry.en is not None and entry.en != source:
                    outdated += 1
                    warnings.append(f'{path}:{entry.line}: {key} was translated from older English; update it, then --sync {folder.name} --merge')
            missing = sum(1 for k in english if k not in entries or not entries[k].value)
            print(f'{folder.name}: {translated}/{len(english)} phrases translated, {missing} missing, {outdated} outdated')
    if args.csv:
        # One column per translation beside the English, for reviewers of either.
        languages = sorted(p.name for p in LOCALIZATION.iterdir() if p.is_dir() and p.name != 'en' and (p / 'mmdhl.properties').is_file()) if not args.catalog else []
        translations = {language: parse(LOCALIZATION / language / 'mmdhl.properties')[0] for language in languages}
        with open(args.csv, 'w', newline='', encoding='utf-8-sig') as f:
            writer = csv.writer(f)
            writer.writerow(['key', 'english'] + languages + ['placeholders', 'context', 'used in', 'reviewer notes'])
            for key, entry in english.items():
                translated = [translations[l][key].value.replace('\\n', '\n') if key in translations[l] else '' for l in languages]
                writer.writerow([key, entry.value.replace('\\n', '\n')] + translated + [' '.join('{' + p + '}' for p in PLACEHOLDER.findall(entry.value)),
                                 entry.context, '\n'.join(site for site, _ in used.get(key, [])), ''])
        print(f'Wrote {len(english)} phrases{" with " + ", ".join(languages) if languages else ""} to {args.csv}')
    for w in warnings:
        print('warning: ' + w)
    for e in errors:
        print('error: ' + e)
    print(f'{len(english)} English phrases, {len(used)} keys used in Lua, {len(errors)} errors, {len(warnings)} warnings')
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
