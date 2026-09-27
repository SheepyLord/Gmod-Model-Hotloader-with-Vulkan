"""Offline comparison of the renderer's private interfaces against a pinned build.

Requires pefile and capstone. This does not alter binaries or approve arbitrary
updates: every new fingerprint still needs review and an in-game render check.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

import capstone
import pefile
from compatibility_profiles import evidence, GUARDS, write_policy


def tables(pe, classname):
    data = pe.__data__
    pos = data.find(classname)
    assert pos >= 16, f'Missing RTTI {classname!r}'
    typ = pe.get_rva_from_offset(pos - 16)
    result = {}
    for off in range(0, len(data) - 24, 4):
        sig, member, constructor, type_rva, hierarchy, self_rva = struct.unpack_from('<6I', data, off)
        if sig != 1 or type_rva != typ or self_rva != pe.get_rva_from_offset(off):
            continue
        needle = struct.pack('<Q', pe.OPTIONAL_HEADER.ImageBase + self_rva)
        at = data.find(needle)
        assert at >= 0
        values = []
        while True:
            target = struct.unpack_from('<Q', data, at + 8 + len(values) * 8)[0] - pe.OPTIONAL_HEADER.ImageBase
            section = pe.get_section_by_rva(target) if target >= 0 else None
            if not section or not (section.Characteristics & 0x20000000):
                break
            values.append(target)
        result[member] = {'rva': pe.get_rva_from_offset(at + 8), 'slots': values}
    return result


def function(pe, rva):
    ranges = {e.struct.BeginAddress: e.struct.EndAddress for e in pe.DIRECTORY_ENTRY_EXCEPTION}
    if rva in ranges:
        end = ranges[rva]
        # MSVC splits unwind entries inside one function (e.g. SetupLighting's
        # prologue and body). Check every contiguous fragment, not just entry.
        while end in ranges:
            end = ranges[end]
        return pe.get_data(rva, end - rva)
    raise AssertionError(f'Missing function boundary {rva:x}')


def compare_function(old, new, a, b):
    ca, cb = function(old, a), function(new, b)
    cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    cs.detail = True
    ia, ib = list(cs.disasm(ca, a)), list(cs.disasm(cb, b))
    assert len(ia) == len(ib)
    relocated = 0
    for x, y in zip(ia, ib):
        if x.bytes == y.bytes:
            continue
        assert x.id == y.id and x.size == y.size, (x, y)
        assert any(o.type == capstone.x86.X86_OP_MEM and o.mem.base == capstone.x86.X86_REG_RIP for o in x.operands), (x, y)
        assert x.disp_offset == y.disp_offset and x.disp_size == y.disp_size
        bx, by = bytearray(x.bytes), bytearray(y.bytes)
        bx[x.disp_offset:x.disp_offset+x.disp_size] = bytes(x.disp_size)
        by[y.disp_offset:y.disp_offset+y.disp_size] = bytes(y.disp_size)
        assert bx == by, (x, y)
        # Every changed RIP-relative reference must still point at identical data.
        assert old.get_data(x.address+x.size+x.disp, 32) == new.get_data(y.address+y.size+y.disp, 32), (x, y)
        relocated += 1
    return {'oldRva': hex(a), 'newRva': hex(b), 'bytes': len(ca), 'instructions': len(ia), 'relocatedDataReferences': relocated}


def main():
    p = argparse.ArgumentParser()
    p.add_argument('baseline', type=Path)
    p.add_argument('candidate', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--emit-profile', type=Path, help='Emit Workshop profile only if all additional code/data evidence is unchanged')
    p.add_argument('--variant', choices=('vanilla','rtx'), default='vanilla')
    args = p.parse_args()
    report = {}
    for name, cls in [('engine.dll', b'.?AVCModelRender@@'), ('client.dll', b'.?AVCClientEntityList@@')]:
        old, new = pefile.PE(str(args.baseline/name)), pefile.PE(str(args.candidate/name))
        ta, tb = tables(old, cls), tables(new, cls)
        assert set(ta) == set(tb)
        for member in ta:
            assert ta[member]['slots'] == tb[member]['slots'], (name, member)
        record = {'baselineSHA256': hashlib.sha256(old.__data__).hexdigest(), 'candidateSHA256': hashlib.sha256(new.__data__).hexdigest(), 'baselineTables': ta, 'candidateTables': tb}
        if name == 'engine.dll':
            record['functions'] = {slot: compare_function(old, new, ta[0]['slots'][slot], tb[0]['slots'][slot]) for slot in [12, 13, 21]}
            tool_a, tool_b = tables(old, b'.?AVCEngineTool@@'), tables(new, b'.?AVCEngineTool@@')
            record['fourLightQuery'] = compare_function(old, new, tool_a[0]['slots'][77], tool_b[0]['slots'][77])
            assert tool_b[0]['slots'][77] == 0x23ad30, 'Update the guarded map-lighting ABI after review'
        else:
            a = next(s for s in old.sections if s.Name.rstrip(b'\0') == b'.text').get_data()
            b = next(s for s in new.sections if s.Name.rstrip(b'\0') == b'.text').get_data()
            assert a == b, 'Client executable code changed; requires further validation'
            record['identicalExecutableCodeSHA256'] = hashlib.sha256(a).hexdigest()
        report[name] = record
    if args.emit_profile:
        names=['engine.dll','client.dll','vphysics.dll','materialsystem.dll','shaderapidx9.dll', 'stdshader_dx6.dll' if args.variant=='rtx' else 'stdshader_dx9.dll']
        profiles=[]
        for name in names:
            old=(args.baseline/name).read_bytes();new=(args.candidate/name).read_bytes()
            before,after=evidence(old),evidence(new)
            assert before==after, f'{name}: executable/read-only evidence changed; inspect ABI and test in-game before explicit profile approval'
            record={'name':name,'variant':args.variant,'sha256':hashlib.sha256(new).hexdigest(),'evidence':after,'guards':GUARDS.get(name,{})}
            profiles.append(record)
            report.setdefault(name,{})['normalizedEvidence']=after
        write_policy(args.emit_profile,{'schema':1,'family':'source-win64-v1','libraries':profiles})
    args.output.write_text(json.dumps(report, indent=2))
    print('PASS: renderer lighting/shadow functions and client entity interfaces retain their ABI')


if __name__ == '__main__':
    main()
