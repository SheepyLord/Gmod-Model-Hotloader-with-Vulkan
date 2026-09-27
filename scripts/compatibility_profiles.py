"""Generate evidence for the compiled source-win64-v1 ABI; never approve by version.

Without --audited, accepts only previously pinned vanilla/RTX DLLs. New profiles
require the offline audit and actual game smoke tests described in ABI.md.
"""
import argparse, hashlib, json, re, struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GUARDS = {
    'engine.dll': {'lighting': 0x23ad30},
    'vphysics.dll': {'physics': 0xbbf0, 'environment': 0x1b490, 'objectTable': 0xe7190, 'objectPosition': 0x25fe0, 'objectForce': 0x245b0},
    # RTTI vtables (IMatRenderContext at subobject offset 0) of the render
    # context the calling thread gets: CMatQueuedRenderContext on the main
    # thread in queued (multicore) mode, CMatRenderContext otherwise. Both
    # pinned builds (vanilla b933ff.., RTX daee78..) share them.
    'materialsystem.dll': {'queuedContext': 0xb6080, 'hardwareContext': 0xbb020},
}
NAMES = ('engine.dll', 'client.dll', 'vphysics.dll', 'materialsystem.dll', 'shaderapidx9.dll', 'stdshader_dx9.dll', 'stdshader_dx6.dll')

def evidence(data):
    import pefile
    pe = pefile.PE(data=data)
    if pe.FILE_HEADER.Machine != 0x8664 or pe.OPTIONAL_HEADER.Magic != 0x20b:
        raise ValueError('Expected Windows x64 PE')
    normalized = bytearray(data)
    for block in getattr(pe, 'DIRECTORY_ENTRY_BASERELOC', []):
        for entry in block.entries:
            if entry.type == 0:
                continue
            if entry.type != 10:
                raise ValueError('Unsupported PE relocation')
            at = pe.get_offset_from_rva(entry.rva)
            value = struct.unpack_from('<Q', data, at)[0] - pe.OPTIONAL_HEADER.ImageBase
            if not 0 <= value < pe.OPTIONAL_HEADER.SizeOfImage:
                raise ValueError('Relocation target outside image')
            struct.pack_into('<Q', normalized, at, value)
    sections = []
    for s in pe.sections:
        flags = s.Characteristics
        if not flags & 0x20000000 and (flags & 0x80000000 or not flags & 0x40):
            continue
        name = s.Name.rstrip(b'\0').decode('ascii')
        if name in ('.rsrc', '.reloc'):
            continue
        start, size = s.PointerToRawData, s.SizeOfRawData
        if start + size > len(data):
            raise ValueError('Invalid PE section')
        sections.append(dict(name=name, rva=s.VirtualAddress, virtualSize=s.Misc_VirtualSize, size=size,
                             flags=flags, sha256=hashlib.sha256(normalized[start:start+size]).hexdigest()))
    if not sections:
        raise ValueError('PE contains no ABI evidence')
    return dict(format=1, machine='win64', imageSize=pe.OPTIONAL_HEADER.SizeOfImage, sections=sections)

def lua_policy(path):
    return json.loads(path.read_text(encoding='utf8').split('[==[', 1)[1].split(']==]', 1)[0])

def write_policy(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('-- Generated release evidence. Do not edit hashes by hand.\nreturn util.JSONToTable([==[\n' +
                    json.dumps(value, indent=2) + '\n]==])\n', encoding='utf8')

def generate(game_root, variant, audited=False, overrides=None):
    pinned = set(re.findall(r'"([0-9a-f]{64})"', (ROOT/'native/render_binaries.hpp').read_text()))
    pinned.update(('4ebd6149f885dfc518a44dd32dda64cbd6ebb3f938d43bdead70e80771b7e414',
                   '11da950b136e4820815c106da8de65306828a27afa25f928ec6c4f86b10c3864',
                   '8ad1a66160097b5a5bd4c5eefd8c8bca328b69e3fea15170e7939ce303ec6226'))
    profiles = []
    for name in NAMES:
        if name == ('stdshader_dx9.dll' if variant == 'rtx' else 'stdshader_dx6.dll'):
            continue
        candidates = [game_root/'bin/win64'/name, game_root/'garrysmod/bin/win64'/name, game_root/'garrysmod/bin'/name]
        path = next((p for p in candidates if p.is_file()), None)
        if path is None:
            raise FileNotFoundError(name)
        data = path.read_bytes(); sha = hashlib.sha256(data).hexdigest()
        if sha not in pinned and not audited:
            raise ValueError(f'{name} is not a previously audited binary: {sha}')
        profiles.append(dict(name=name, variant=variant, sha256=sha, evidence=evidence(data),
                             guards=(overrides or {}).get(name, GUARDS.get(name, {}))))
    return profiles

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--game-root', type=Path, required=True)
    p.add_argument('--variant', choices=('vanilla','rtx'), required=True)
    p.add_argument('--output', type=Path, default=ROOT/'addon/lua/mmdhl/compatibility_policy.lua')
    p.add_argument('--append', action='store_true')
    p.add_argument('--audited', action='store_true', help='Explicit maintainer approval after ABI audit and game tests')
    p.add_argument('--guards', type=Path, help='Audited RVAs for existing guards; cannot change compiled slots/types')
    args = p.parse_args()
    value = lua_policy(args.output) if args.append and args.output.exists() else dict(schema=1, family='source-win64-v1', libraries=[])
    new = generate(args.game_root, args.variant, args.audited, json.loads(args.guards.read_text()) if args.guards else None)
    keys = {(x['name'], x['sha256']) for x in new}
    value['libraries'] = [x for x in value['libraries'] if (x['name'], x['sha256']) not in keys] + new
    write_policy(args.output, value)
    print(f'Generated {len(value["libraries"])} audited library profiles')

if __name__ == '__main__':
    main()
