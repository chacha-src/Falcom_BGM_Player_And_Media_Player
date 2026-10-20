import json, struct, sys

def load(path):
    d = open(path, 'rb').read()
    if d[:4] == b'glTF':
        off = 12; js = None; bn = None
        while off < len(d):
            ln, ty = struct.unpack_from('<II', d, off); off += 8
            ch = d[off:off + ln]; off += ln
            if ty == 0x4E4F534A: js = json.loads(ch)
            elif ty == 0x004E4942: bn = ch
        return js, bn
    return json.loads(d), None

for p in sys.argv[1:]:
    j, b = load(p)
    print('==', p)
    print(' nodes', len(j['nodes']), 'meshes', len(j.get('meshes', [])), 'skins', len(j.get('skins', [])), 'anims', len(j.get('animations', [])))
    for i, n in enumerate(j['nodes']):
        extra = ''
        if 'mesh' in n: extra += ' mesh=%d' % n['mesh']
        if 'skin' in n: extra += ' skin=%d' % n['skin']
        print('  node', i, n.get('name'), 'children', n.get('children', []), extra)
    for s in j.get('skins', []):
        print(' skin joints', len(s['joints']))
    for m in j.get('meshes', []):
        print(' mesh', m.get('name'), [ (list(pr['attributes'].keys()), pr.get('material')) for pr in m['primitives']])
    print(' anims', [a.get('name') for a in j.get('animations', [])])
    print(' images', j.get('images'))
    print(' materials', [m.get('name') for m in j.get('materials', [])])
