"""Fetch pinned sources. Installed users need neither Python nor a compiler."""
import concurrent.futures, hashlib, io, json, pathlib, struct, sys, tarfile, urllib.request, zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]

def set_rpath(elf, rpath):
    """Overwrites a 64-bit ELF library's DT_RPATH/DT_RUNPATH in place with a shorter path."""
    data = bytearray(elf)
    if data[:4] != b'\x7fELF' or data[4] != 2 or data[5] != 1:
        raise RuntimeError('not a 64-bit little-endian ELF file')
    phoff, = struct.unpack_from('<Q', data, 0x20)
    phentsize, phnum = struct.unpack_from('<HH', data, 0x36)
    headers = [struct.unpack_from('<IIQQQQQQ', data, phoff + i * phentsize) for i in range(phnum)]
    def offset(vaddr):
        for kind, _, off, start, _, filesz, _, _ in headers:
            if kind == 1 and start <= vaddr < start + filesz:
                return off + vaddr - start
        raise RuntimeError('address outside the file')
    dynamic = next(h for h in headers if h[0] == 2)
    entries = [struct.unpack_from('<qQ', data, dynamic[2] + i) for i in range(0, dynamic[5], 16)]
    strtab = offset(next(v for t, v in entries if t == 5))
    paths = [v for t, v in entries if t in (15, 29)]
    if not paths:
        raise RuntimeError('no RPATH to replace')
    for at in paths:
        start = strtab + at
        length = data.index(0, start) - start
        if len(rpath) > length:
            raise RuntimeError('replacement RPATH is longer than the original')
        data[start:start + length] = rpath + bytes(length - len(rpath))
    return bytes(data)

def fetch(item):
    name, (repo, revision) = item
    target = ROOT / 'vendor' / name
    marker = target / '.mmdhl-revision'
    if marker.is_file() and marker.read_text() == revision:
        return name + ': ready'
    if target.exists():
        raise RuntimeError(f'{target} exists without matching revision; inspect before replacing')
    req = urllib.request.Request(f'https://codeload.github.com/{repo}/tar.gz/{revision}', headers={'User-Agent': 'ModelHotloader-build'})
    with urllib.request.urlopen(req, timeout=120) as response:
        blob = response.read()
    target.mkdir(parents=True)
    with tarfile.open(fileobj=io.BytesIO(blob), mode='r:gz') as archive:
        for member in archive:
            parts = pathlib.PurePosixPath(member.name).parts[1:]
            if not parts or '..' in parts or member.issym() or member.islnk():
                continue
            dest = target.joinpath(*parts)
            if member.isdir():
                dest.mkdir(parents=True, exist_ok=True)
            elif member.isfile():
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes(archive.extractfile(member).read())
    marker.write_text(revision)
    (target / '.archive-sha256').write_text(hashlib.sha256(blob).hexdigest())
    return name + ': fetched ' + revision

if __name__ == '__main__':
    lock = json.loads((ROOT / 'dependencies.lock.json').read_text())
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        for result in pool.map(fetch, lock['repositories'].items()):
            print(result, flush=True)
    # CoACD (Balanced static-prop collision) ships as a pinned wheel binary.
    spec = lock['coacd']
    target = ROOT / 'vendor' / 'coacd'
    marker = target / '.mmdhl-revision'
    if not (marker.is_file() and marker.read_text() == spec['version']):
        request = urllib.request.Request(spec['url'], headers={'User-Agent': 'ModelHotloader-build'})
        with urllib.request.urlopen(request, timeout=120) as response:
            blob = response.read()
        if hashlib.sha256(blob).hexdigest() != spec['sha256']:
            raise RuntimeError('CoACD wheel checksum mismatch')
        target.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(io.BytesIO(blob)) as archive:
            for name in archive.namelist():
                if name.endswith(('.dll', '__init__.py')) or 'LICENSE' in name.upper():
                    (target / pathlib.PurePosixPath(name).name).write_bytes(archive.read(name))
        marker.write_text(spec['version'])
        print('coacd: fetched ' + spec['version'], flush=True)
    # The Linux x86-64 wheel (Linux builds only): lib_coacd.so and the OpenMP runtime it
    # bundles (coacd.libs). Both go beside the worker, so lib_coacd.so's search path becomes
    # $ORIGIN (set_rpath).
    if sys.platform.startswith('linux'):
        spec = lock['coacd_linux64']
        target = ROOT / 'vendor' / 'coacd' / 'linux64'
        marker = target / '.mmdhl-revision'
        if not (marker.is_file() and marker.read_text() == spec['version']):
            request = urllib.request.Request(spec['url'], headers={'User-Agent': 'ModelHotloader-build'})
            with urllib.request.urlopen(request, timeout=120) as response:
                blob = response.read()
            if hashlib.sha256(blob).hexdigest() != spec['sha256']:
                raise RuntimeError('CoACD Linux wheel checksum mismatch')
            target.mkdir(parents=True, exist_ok=True)
            with zipfile.ZipFile(io.BytesIO(blob)) as archive:
                for name in archive.namelist():
                    base = pathlib.PurePosixPath(name).name
                    if base == 'lib_coacd.so' or (name.startswith('coacd.libs/') and '.so' in base) or 'LICENSE' in name.upper():
                        destination = target / base
                        data = archive.read(name)
                        if base == 'lib_coacd.so':
                            data = set_rpath(data, b'$ORIGIN')
                        destination.write_bytes(data)
                        if '.so' in base:
                            destination.chmod(0o755)
            marker.write_text(spec['version'])
            print('coacd linux64: fetched ' + spec['version'], flush=True)
    # Local documented extension: expose the existing soft-solver phase virtually
    # so the bridge observes impulses after both solver stages, without changing
    # Bullet integration or solver behavior.
    header = ROOT / 'vendor/bullet/src/BulletSoftBody/btSoftRigidDynamicsWorld.h'
    source = header.read_text()
    old = '\tvoid solveSoftBodiesConstraints(btScalar timeStep);'
    new = '\tvirtual void solveSoftBodiesConstraints(btScalar timeStep); // MMDHL solver-stage observation'
    if old in source:
        header.write_text(source.replace(old, new))
    elif new not in source:
        raise RuntimeError('Bullet patch context changed')
    # nanoem's anchor vertex accessor mistakenly uses the rigid-body index.
    sourcefile = ROOT / 'vendor/nanoem/nanoem/nanoem.c'
    source = sourcefile.read_text(encoding='utf-8')
    old = 'vertex = nanoemModelGetOneVertexObject(parent_model, anchor->rigid_body_index);'
    new = 'vertex = nanoemModelGetOneVertexObject(parent_model, anchor->vertex_index); /* MMDHL anchor fix */'
    if old in source: sourcefile.write_text(source.replace(old,new),encoding='utf-8')
    elif new not in source: raise RuntimeError('nanoem anchor patch context changed')

    physics = ROOT / 'vendor/nanoem/nanoem/ext/physics_bullet.cc'
    source = physics.read_text(encoding='utf-8')
    marker = '        btTransform transformA, transformB, skinningTransformA, skinningTransformB, inverseTransformA, inverseTransformB;'
    if 'inverseTransformA.setIdentity();' not in source:
        if marker not in source: raise RuntimeError('nanoem world-anchor patch context changed')
        physics.write_text(source.replace(marker,marker+'\n        inverseTransformA.setIdentity();\n        inverseTransformB.setIdentity(); // MMDHL world-anchor initialization'),encoding='utf-8')

    # nanoem's soft bodies: a rope gets one node per vertex of its material (it
    # used the whole model's vertex count and end points), with links at the
    # authored spacing; pins are model vertex indices, resolved to their node.
    source = physics.read_text(encoding='utf-8')
    rope_old = ('                const nanoem_f32_t *fromOrigin = nanoemModelVertexGetOrigin(vertices[0]),\n'
                '                        *toOrigin = nanoemModelVertexGetOrigin(vertices[numVertices - 1]);\n'
                '                m_internalSoftBody = btSoftBodyHelpers::CreateRope(*world->m_worldInfo, btVector3(fromOrigin[0], fromOrigin[1], fromOrigin[2]), btVector3(toOrigin[0], toOrigin[1], toOrigin[2]), numVertices - 2, 0);\n')
    rope_new = ('                /* MMDHL rope fix: nodes are the material\'s vertices in first-use order */\n'
                '                const int numRopeNodes = int(m_points->size() / 3);\n'
                '                if (numRopeNodes < 2) {\n'
                '                    m_internalSoftBody = 0;\n'
                '                    break;\n'
                '                }\n'
                '                m_internalSoftBody = btSoftBodyHelpers::CreateRope(*world->m_worldInfo, btVector3(m_points->at(0), m_points->at(1), m_points->at(2)), btVector3(m_points->at(numRopeNodes * 3 - 3), m_points->at(numRopeNodes * 3 - 2), m_points->at(numRopeNodes * 3 - 1)), numRopeNodes - 2, 0);\n')
    rest_old = ('                        node.m_tag = vertex;\n'
                '                    }\n'
                '                }\n'
                '                break;\n'
                '            }\n'
                '            default:\n')
    rest_new = ('                        node.m_tag = vertex;\n'
                '                    }\n'
                '                }\n'
                '                m_internalSoftBody->resetLinkRestLengths(); /* MMDHL rope fix: authored spacing */\n'
                '                break;\n'
                '            }\n'
                '            default:\n')
    pins_old = ('            nanoem_rsize_t numPinnedVertices, numNodes = m_internalSoftBody->m_nodes.size();\n'
                '            const nanoem_u32_t *pinnedVertexIndices = nanoemModelSoftBodyGetAllPinnedVertexIndices(value, &numPinnedVertices);\n'
                '            for (nanoem_rsize_t i = 0; i < numPinnedVertices; i++) {\n'
                '                const nanoem_u32_t vertexIndex = pinnedVertexIndices[i];\n'
                '                if (vertexIndex < numNodes) {\n'
                '                    m_internalSoftBody->setMass(vertexIndex, 0);\n'
                '                }\n'
                '            }\n')
    pins_new = ('            /* MMDHL pin fix: pins are model vertex indices; pin the node made from that vertex */\n'
                '            nanoem_rsize_t numPinnedVertices;\n'
                '            const nanoem_u32_t *pinnedVertexIndices = nanoemModelSoftBodyGetAllPinnedVertexIndices(value, &numPinnedVertices);\n'
                '            for (nanoem_rsize_t i = 0; i < numPinnedVertices; i++) {\n'
                '                const nanoem_u32_t vertexIndex = pinnedVertexIndices[i];\n'
                '                khiter_t pinned = vertexIndex < numVertices ? kh_get_vertices(verticesMap, reinterpret_cast<khint64_t>(vertices[vertexIndex])) : kh_end(verticesMap);\n'
                '                if (pinned != kh_end(verticesMap)) {\n'
                '                    btSoftBody::Node *node = &m_internalSoftBody->m_nodes[kh_value(verticesMap, pinned)];\n'
                '                    m_internalSoftBody->setMass(kh_value(verticesMap, pinned), 0);\n'
                '                    /* An anchor from a pinned node to a massless (follow-bone) body constrains nothing, and Bullet cannot invert its zero impulse matrix */\n'
                '                    btAlignedObjectArray<btSoftBody::Anchor> kept;\n'
                '                    for (int a = 0; a < m_internalSoftBody->m_anchors.size(); a++) {\n'
                '                        const btSoftBody::Anchor &anchor = m_internalSoftBody->m_anchors[a];\n'
                '                        if (anchor.m_node != node || anchor.m_body->getInvMass() != 0) {\n'
                '                            kept.push_back(anchor);\n'
                '                        }\n'
                '                    }\n'
                '                    m_internalSoftBody->m_anchors = kept;\n'
                '                }\n'
                '            }\n')
    patched = source
    for name, old, new in (('rope', rope_old, rope_new), ('rope spacing', rest_old, rest_new), ('pin', pins_old, pins_new)):
        if old in patched: patched = patched.replace(old, new, 1)
        elif new not in patched: raise RuntimeError('nanoem soft-body ' + name + ' patch context changed')
    if patched != source: physics.write_text(patched, encoding='utf-8')
