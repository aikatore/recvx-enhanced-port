#!/usr/bin/env python3
"""Checks the importer's output (docs/IMPORT_OUTBREAK.md "Verification"): a .glb (GLB header, accessors
inside their views, joint indices, weights summing to 1, the inverse bind matrices against the node
tree) or a .cvxm (the MLB, the vertex chunk, the strips, the texture chunks as the game reads them),
and renders a preview PNG (rest pose front and back; for a .glb also a test pose with the arms
lowered through the skin). Needs numpy and Pillow. Never commit its output (game data).
    pc/tools/import_check.py MODEL.glb|MODEL.cvxm OUT.png
"""
import sys, json, struct, io
import numpy as np
from PIL import Image

# ---------------------------------------------------------------- a tiny rasterizer (textured, z-buffered)
def render(tris, size=(512, 768), yaw=0.0, out=None):
    """tris: list of (P[3x3], UV[3x2], tex rgba array or None)"""
    W, H = size
    allp = np.concatenate([t[0] for t in tris])
    c = (allp.max(0) + allp.min(0)) / 2
    ext = (allp.max(0) - allp.min(0)).max() / 2 * 1.1
    cy, sy = np.cos(yaw), np.sin(yaw)
    R = np.array([[cy, 0, -sy], [0, 1, 0], [sy, 0, cy]])
    img = np.full((H, W, 3), 40, np.uint8)
    zb = np.full((H, W), -1e9)
    s = min(W, H) / 2 / ext
    L = np.array([0.3, 0.5, 0.8]); L /= np.linalg.norm(L)
    for P, UV, tex in tris:
        q = (P - c) @ R.T
        sx = W / 2 + q[:, 0] * s; syy = H / 2 - q[:, 1] * s; z = q[:, 2]
        n = np.cross(q[1] - q[0], q[2] - q[0]); nl = np.linalg.norm(n)
        if nl == 0: continue
        shade = 0.45 + 0.55 * abs(n @ L) / nl
        x0, x1 = int(max(0, np.floor(sx.min()))), int(min(W - 1, np.ceil(sx.max())))
        y0, y1 = int(max(0, np.floor(syy.min()))), int(min(H - 1, np.ceil(syy.max())))
        if x1 < x0 or y1 < y0: continue
        X, Y = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        d = (syy[1] - syy[2]) * (sx[0] - sx[2]) + (sx[2] - sx[1]) * (syy[0] - syy[2])
        if abs(d) < 1e-9: continue
        a = ((syy[1] - syy[2]) * (X - sx[2]) + (sx[2] - sx[1]) * (Y - syy[2])) / d
        b = ((syy[2] - syy[0]) * (X - sx[2]) + (sx[0] - sx[2]) * (Y - syy[2])) / d
        g = 1 - a - b
        m = (a >= 0) & (b >= 0) & (g >= 0)
        if not m.any(): continue
        zz = a * z[0] + b * z[1] + g * z[2]
        sub = zb[y0:y1 + 1, x0:x1 + 1]
        m &= zz > sub
        if tex is not None:
            u = a * UV[0, 0] + b * UV[1, 0] + g * UV[2, 0]
            v = a * UV[0, 1] + b * UV[1, 1] + g * UV[2, 1]
            th, tw = tex.shape[:2]
            tx = (np.floor(u * tw).astype(int)) % tw; ty = (np.floor(v * th).astype(int)) % th
            col = tex[ty, tx]
            m &= col[..., 3] > 40
            rgb = col[..., :3] * shade
        else:
            rgb = np.full(X.shape + (3,), 200 * shade)
        sub[m] = zz[m]
        img[y0:y1 + 1, x0:x1 + 1][m] = rgb[m].astype(np.uint8)
    im = Image.fromarray(img)
    if out: im.save(out)
    return im

# ---------------------------------------------------------------- .glb
def load(path):
    d = open(path, 'rb').read()
    magic, ver, length = struct.unpack_from('<III', d, 0)
    assert magic == 0x46546C67 and ver == 2 and length == len(d), 'bad GLB header'
    jl, jt = struct.unpack_from('<II', d, 12); assert jt == 0x4E4F534A
    js = json.loads(d[20:20 + jl])
    bl, bt = struct.unpack_from('<II', d, 20 + jl); assert bt == 0x004E4942
    bin_ = d[28 + jl:28 + jl + bl]
    assert js['buffers'][0]['byteLength'] <= len(bin_)
    return js, bin_

CT = {5120: 'b', 5121: 'B', 5122: 'h', 5123: 'H', 5125: 'I', 5126: 'f'}
NC = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}
def acc(js, b, i):
    a = js['accessors'][i]; v = js['bufferViews'][a['bufferView']]
    n = NC[a['type']]; ct = CT[a['componentType']]
    size = struct.calcsize(ct) * n * a['count']
    off = v.get('byteOffset', 0) + a.get('byteOffset', 0)
    assert off + size <= v.get('byteOffset', 0) + v['byteLength'], 'accessor past its view'
    arr = np.frombuffer(b, dtype=np.dtype(ct).newbyteorder('<'), count=n * a['count'], offset=off)
    return arr.reshape(a['count'], n) if n > 1 else arr

def check_glb(path, out):
    js, b = load(path)
    nodes = js['nodes']; skin = js['skins'][0]; joints = skin['joints']
    mesh = js['meshes'][0]
    prim0 = mesh['primitives'][0]['attributes']
    P = acc(js, b, prim0['POSITION']).astype(float); UV = acc(js, b, prim0['TEXCOORD_0']).astype(float)
    J = acc(js, b, prim0['JOINTS_0']); W = acc(js, b, prim0['WEIGHTS_0'])
    IBM = acc(js, b, skin['inverseBindMatrices']).reshape(-1, 4, 4).transpose(0, 2, 1)  # column-major -> row
    assert len(IBM) == len(joints)
    assert J.max() < len(joints), 'joint index out of range'
    ws = W.sum(1); assert np.allclose(ws, 1, atol=1e-3), 'weights do not sum to 1'
    nt = sum(acc(js, b, p['indices']).size // 3 for p in mesh['primitives'])
    imgs = []
    for im in js.get('images', []):
        if 'bufferView' in im:
            v = js['bufferViews'][im['bufferView']]
            imgs.append(np.array(Image.open(io.BytesIO(b[v.get('byteOffset', 0):v.get('byteOffset', 0) + v['byteLength']])).convert('RGBA')))
        else: imgs.append(None)
    # rest world matrices from the node tree
    par = {c: i for i, n in enumerate(nodes) for c in n.get('children', [])}
    def local(i):
        n = nodes[i]; t = n.get('translation', [0, 0, 0]); q = n.get('rotation', [0, 0, 0, 1])
        x, y, z, w = q
        R = np.array([[1 - 2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)], [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)], [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])
        M = np.eye(4); M[:3, :3] = R; M[:3, 3] = t; return M
    def world(i, override=None):
        M = local(i) if not override or i not in override else override[i]
        p = par.get(i)
        return world(p, override) @ M if p is not None and p in joints else M
    Wr = [world(j) for j in joints]
    err = max(np.abs(Wr[k] @ IBM[k] - np.eye(4)).max() for k in range(len(joints)))
    print(f'{path}: {len(nodes)} nodes, {len(joints)} joints, {len(P)} verts, {nt} tris, {len(mesh["primitives"])} primitives, '
          f'{len(imgs)} images, bind check {err:.2e}')
    assert err < 1e-5 * max(1.0, max(np.abs(w_[:3, 3]).max() for w_ in Wr)) * 100, "bind matrices do not invert the node tree"
    # test pose: arms (the joints whose rest x is far from the centre) lowered 60 degrees about z at the shoulder
    pos = np.array([w_[:3, 3] for w_ in Wr])
    side = [k for k in range(len(joints)) if abs(pos[k][0]) > 5 and abs(pos[k][0]) < 12 and pos[k][1] > 120]
    ov = {}
    for k in side:
        a = -np.sign(pos[k][0]) * np.pi / 3
        Rz = np.eye(4); Rz[:2, :2] = [[np.cos(a), -np.sin(a)], [np.sin(a), np.cos(a)]]
        if not any(par.get(joints[k]) == joints[s] for s in side): ov[joints[k]] = local(joints[k]) @ Rz
    Wp = [world(j, ov) for j in joints]
    Sk = np.array([Wp[k] @ IBM[k] for k in range(len(joints))])
    Ph = np.c_[P, np.ones(len(P))]
    Pp = np.zeros_like(P)
    for c in range(4): Pp += W[:, c:c + 1] * np.einsum('nij,nj->ni', Sk[J[:, c]], Ph)[:, :3]
    def tris_of(PP):
        out = []
        for pr in mesh['primitives']:
            I = acc(js, b, pr['indices']).reshape(-1, 3)
            mat = js['materials'][pr['material']]
            ti = mat['pbrMetallicRoughness'].get('baseColorTexture', {}).get('index')
            tex = imgs[js['textures'][ti]['source']] if ti is not None else None
            for t in I: out.append((PP[t], UV[t], tex))
        return out
    a = render(tris_of(P), (300, 520), 0)
    c = render(tris_of(P), (300, 520), np.pi)
    p = render(tris_of(Pp), (300, 520), 0)
    im = Image.new('RGB', (900, 520)); im.paste(a, (0, 0)); im.paste(c, (300, 0)); im.paste(p, (600, 0)); im.save(out)

# ---------------------------------------------------------------- .cvxm
def check_cvxm(path, out, clamp_u=True):
    d = open(path, 'rb').read()
    magic, ver, moff, toff, soff, nv, nn = struct.unpack_from('<4sIIIIII', d, 0)
    assert magic == b'RXIM' and 1 <= ver <= 4 and moff == 60
    face = list(d[28:33]) if ver >= 2 else []   # the face bones' objects (lids, jaw), 255 none
    mlb = 64
    hs, on = struct.unpack_from('<HH', d, mlb + 4); to, oo = struct.unpack_from('<ii', d, mlb + 8)
    base = mlb + hs
    objs = []
    for i in range(on):
        q = base + oo + 0x34 * i
        ev, model = struct.unpack_from('<Ii', d, q); pos = struct.unpack_from('<3f', d, q + 8)
        child, sib = struct.unpack_from('<ii', d, q + 44)
        objs.append((ev, model, pos, child, sib))
    # parents from child/sibling (pre-order walk)
    par = [-1] * on
    def walk(i, p):
        while i is not None:
            par[i] = p
            c = objs[i][3]
            if c != -1: walk((c - oo) // 0x34, i)
            s = objs[i][4]
            i = (s - oo) // 0x34 if s != -1 else None
    walk(0, -1)
    rest = np.zeros((on, 3))
    for i in range(on): rest[i] = np.array(objs[i][2]) + (rest[par[i]] if par[i] >= 0 else 0)
    vl, pl = struct.unpack_from('<ii', d, base + objs[1][1])
    v = base + vl
    assert d[v] == 51 and (base + vl) % 16 == 0, 'vertex chunk'
    n = struct.unpack_from('<H', d, v + 6)[0]
    V = np.frombuffer(d, np.float32, n * 8, v + 64).reshape(n, 8)
    P = V[:, :3] + rest[1]
    # textures
    texs = []
    q = toff
    while True:
        code, sz = struct.unpack_from('<Ii', d, q)
        if code == 0xFFFFFFFF: break
        a = q + 32
        img, = struct.unpack_from('<I', d, a + 0x88); itype = d[a + 0x93]; w, h = struct.unpack_from('<HH', d, a + 0x94)
        px = np.frombuffer(d, np.uint8, img, a + 0x100)
        clut = np.frombuffer(d, np.uint8, 1024 if itype == 5 else 64, a + 0x100 + img).reshape(-1, 4)
        idx = np.arange(len(clut)); idx = (idx & ~0x18) | ((idx & 8) << 1) | ((idx & 16) >> 1) if itype == 5 else idx
        rgba = clut[idx][px].reshape(h, w, 4).astype(int); rgba[..., 3] = np.minimum(rgba[..., 3] * 2, 255)
        texs.append(rgba.astype(np.uint8))
        q += 32 + sz

    # strips
    tris = []; tex = None; p = base + pl; nstrips = 0
    while d[p] != 0xFF:
        t = d[p]; sz = struct.unpack_from('<H', d, p + 2)[0]
        if t == 8: tex = texs[sz & 0xFFF]; p += 4; continue
        if 17 <= t <= 23: p += 4 + 2 * sz; continue
        assert t == 0x42, t
        cnt = struct.unpack_from('<H', d, p + 4)[0] & 0x3FFF; r = p + 6
        for _ in range(cnt):
            ln = struct.unpack_from('<h', d, r)[0]; r += 2; ln = abs(ln)
            e = np.frombuffer(d, np.uint16, 3 * ln, r).reshape(ln, 3); r += 6 * ln
            uv = e[:, 1:] / 1023.0
            if clamp_u: uv = uv.copy(); uv[:, 0] = np.clip(uv[:, 0], 0, 0.9999)
            for k in range(ln - 2):
                ix = e[k:k + 3, 0]
                if len(set(ix)) < 3: continue
                tris.append((P[ix], uv[k:k + 3], tex))
            nstrips += 1
        p += 4 + 2 * sz
    print(f'{path}: {on} nodes, {n} vertices, {len(texs)} textures, {nstrips} strips, {len(tris)} triangles, face bones {face}')
    # the game faces -z: view from -z to see the front
    a = render(tris, (300, 520), np.pi); b = render(tris, (300, 520), 0)
    im = Image.new('RGB', (600, 520)); im.paste(a, (0, 0)); im.paste(b, (300, 0)); im.save(out)

if __name__ == '__main__':
    (check_cvxm if sys.argv[1].endswith('.cvxm') else check_glb)(sys.argv[1], sys.argv[2])
