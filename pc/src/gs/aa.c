/*
 * Anti-aliasing of the 3D scene (the DISPLAY page's "Anti-Aliasing", ini anti_aliasing): FXAA or
 * SMAA 1x over the scene's render target, mid-frame, once its 3D is drawn and before the 2D on top
 * (stream.c aa_split, gs_gl.c gs_scene_aa). The result is a new texture of the same size: every
 * pixel the filter leaves alone is copied exactly, alpha always, so a picture without edges stays
 * the same.
 *
 * FXAA: our own implementation of the published FXAA 3.11 "quality" algorithm (Timothy Lottes,
 * NVIDIA): local contrast, edge direction, a search along the edge for its ends, a sub-pixel blend;
 * no NVIDIA code. SMAA 1x: the shader functions of SMAA (Jorge Jimenez et al., MIT licence,
 * https://github.com/iryoku/smaa, THIRD_PARTY_NOTICES.md), SMAA.hlsl expanded for GLSL 3.30 with
 * the ULTRA preset (edge threshold 0.05: the game is dark) and only what SMAA 1x with luma edges
 * uses; its lookup textures are computed at start (smaa_tables.h). Three passes: edges (RG8), blend
 * weights (RGBA8), neighbourhood blending.
 *
 * SMAA (smaa_lib below and smaa_tables.h):
 * Copyright (C) 2013 Jorge Jimenez (jorge@iryoku.com)
 * Copyright (C) 2013 Jose I. Echevarria (joseignacioechevarria@gmail.com)
 * Copyright (C) 2013 Belen Masia (bmasia@unizar.es)
 * Copyright (C) 2013 Fernando Navarro (fernandn@microsoft.com)
 * Copyright (C) 2013 Diego Gutierrez (diegog@unizar.es)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software. As clarification, there is no
 * requirement that the copyright notice and permission be included in binary
 * distributions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "../render/gl_loader.h"
#include "smaa_tables.h"

#define GL_RG 0x8227
#define GL_RG8 0x822B

unsigned int gs_gl_compile(const char* vs, const char* fs);   /* gs_gl.c */

static const char* aa_vs =
    "#version 330 core\n"
    "void main(){\n"
    "  vec2 c = vec2((gl_VertexID & 1) != 0 ? 1.0 : 0.0, (gl_VertexID & 2) != 0 ? 1.0 : 0.0);\n"
    "  gl_Position = vec4(c * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

/* u_metrics: 1 / width, 1 / height, width, height. Texture row 0 is the picture's top (the GS's
   y), as SMAA's texture coordinates expect. */
#define AA_HEAD "#version 330 core\nuniform vec4 u_metrics;\nout vec4 o;\n"

static const char* fxaa_fs =
    AA_HEAD
    "uniform sampler2D u_src;\n"
    "float lum(vec2 uv){ return dot(clamp(textureLod(u_src, uv, 0.0).rgb, 0.0, 1.0), vec3(0.299, 0.587, 0.114)); }\n"
    "float lumi(ivec2 p, ivec2 d){ return dot(clamp(texelFetch(u_src, clamp(p + d, ivec2(0), ivec2(u_metrics.zw) - 1), 0).rgb, 0.0, 1.0), vec3(0.299, 0.587, 0.114)); }\n"
    "void main(){\n"
    "  ivec2 p = ivec2(gl_FragCoord.xy);\n"
    "  vec4 cm = texelFetch(u_src, p, 0);\n"
    "  float lm = dot(clamp(cm.rgb, 0.0, 1.0), vec3(0.299, 0.587, 0.114));\n"
    "  float ln = lumi(p, ivec2(0, -1)), ls = lumi(p, ivec2(0, 1)), lw = lumi(p, ivec2(-1, 0)), le = lumi(p, ivec2(1, 0));\n"
    "  float lmax = max(lm, max(max(ln, ls), max(lw, le))), lmin = min(lm, min(min(ln, ls), min(lw, le)));\n"
    "  float range = lmax - lmin;\n"
    "  if (range < max(0.0312, lmax * 0.125)) { o = cm; return; }\n"   /* no edge: the pixel as it is */
    "  float lnw = lumi(p, ivec2(-1, -1)), lne = lumi(p, ivec2(1, -1)), lsw = lumi(p, ivec2(-1, 1)), lse = lumi(p, ivec2(1, 1));\n"
    "  float eh = abs(lnw + lsw - 2.0 * lw) + 2.0 * abs(ln + ls - 2.0 * lm) + abs(lne + lse - 2.0 * le);\n"
    "  float ev = abs(lnw + lne - 2.0 * ln) + 2.0 * abs(lw + le - 2.0 * lm) + abs(lsw + lse - 2.0 * ls);\n"
    "  bool horz = eh >= ev;\n"                                         /* a horizontal edge: blended vertically */
    "  float l1 = horz ? ln : lw, l2 = horz ? ls : le;\n"
    "  float g1 = abs(l1 - lm), g2 = abs(l2 - lm);\n"
    "  float stepl = horz ? u_metrics.y : u_metrics.x, lavg, grad;\n"
    "  if (g1 >= g2) { stepl = -stepl; lavg = 0.5 * (l1 + lm); grad = g1; } else { lavg = 0.5 * (l2 + lm); grad = g2; }\n"
    "  vec2 uv = gl_FragCoord.xy * u_metrics.xy, cur = uv;\n"
    "  if (horz) cur.y += stepl * 0.5; else cur.x += stepl * 0.5;\n"   /* on the edge, between the pixel and its neighbour */
    "  vec2 off = horz ? vec2(u_metrics.x, 0.0) : vec2(0.0, u_metrics.y);\n"
    "  vec2 u1 = cur - off, u2 = cur + off;\n"
    "  float e1 = lum(u1) - lavg, e2 = lum(u2) - lavg;\n"
    "  bool d1 = abs(e1) >= grad * 0.25, d2 = abs(e2) >= grad * 0.25;\n"
    "  const float q[11] = float[11](1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0, 8.0, 8.0, 8.0, 8.0);\n"
    "  for (int i = 0; i < 11 && !(d1 && d2); i++) {\n"             /* along the edge to both of its ends */
    "    if (!d1) { u1 -= off * q[i]; e1 = lum(u1) - lavg; d1 = abs(e1) >= grad * 0.25; }\n"
    "    if (!d2) { u2 += off * q[i]; e2 = lum(u2) - lavg; d2 = abs(e2) >= grad * 0.25; }\n"
    "  }\n"
    "  float dist1 = horz ? uv.x - u1.x : uv.y - u1.y, dist2 = horz ? u2.x - uv.x : u2.y - uv.y;\n"
    "  bool near1 = dist1 < dist2;\n"
    "  float pix = 0.5 - min(dist1, dist2) / (dist1 + dist2);\n"
    "  bool good = ((near1 ? e1 : e2) < 0.0) != (lm < lavg);\n"        /* the nearer end turns the right way */
    "  float sub = clamp(abs((2.0 * (ln + ls + lw + le) + lnw + lne + lsw + lse) / 12.0 - lm) / range, 0.0, 1.0);\n"
    "  sub = (-2.0 * sub + 3.0) * sub * sub;\n"
    "  float f = max(good ? pix : 0.0, sub * sub * 0.75);\n"
    "  if (horz) uv.y += f * stepl; else uv.x += f * stepl;\n"
    "  o = vec4(textureLod(u_src, uv, 0.0).rgb, cm.a);\n"
    "}\n";

/* SMAA.hlsl (MIT, see the top of this file) for GLSL 3.30, ULTRA preset; the area texture is
   SMAA 1x's 160x80 slice */
static const char* smaa_lib =
    "void SMAAMovc(bvec2 cond, inout vec2 variable, vec2 value) {\n"
    " if (cond.x) variable.x = value.x;\n"
    " if (cond.y) variable.y = value.y;\n"
    "}\n"
    "void SMAAMovc(bvec4 cond, inout vec4 variable, vec4 value) {\n"
    " SMAAMovc(cond.xy, variable.xy, value.xy);\n"
    " SMAAMovc(cond.zw, variable.zw, value.zw);\n"
    "}\n"
    "vec2 SMAALumaEdgeDetectionPS(vec2 texcoord,\n"
    "       vec4 offset[3],\n"
    "       sampler2D colorTex\n"
    "       ) {\n"
    " vec2 threshold = vec2(0.05, 0.05);\n"
    " vec3 weights = vec3(0.2126, 0.7152, 0.0722);\n"
    " float L = dot(texture(colorTex, texcoord).rgb, weights);\n"
    " float Lleft = dot(texture(colorTex, offset[0].xy).rgb, weights);\n"
    " float Ltop = dot(texture(colorTex, offset[0].zw).rgb, weights);\n"
    " vec4 delta;\n"
    " delta.xy = abs(L - vec2(Lleft, Ltop));\n"
    " vec2 edges = step(threshold, delta.xy);\n"
    " if (dot(edges, vec2(1.0, 1.0)) == 0.0)\n"
    "  discard;\n"
    " float Lright = dot(texture(colorTex, offset[1].xy).rgb, weights);\n"
    " float Lbottom = dot(texture(colorTex, offset[1].zw).rgb, weights);\n"
    " delta.zw = abs(L - vec2(Lright, Lbottom));\n"
    " vec2 maxDelta = max(delta.xy, delta.zw);\n"
    " float Lleftleft = dot(texture(colorTex, offset[2].xy).rgb, weights);\n"
    " float Ltoptop = dot(texture(colorTex, offset[2].zw).rgb, weights);\n"
    " delta.zw = abs(vec2(Lleft, Ltop) - vec2(Lleftleft, Ltoptop));\n"
    " maxDelta = max(maxDelta.xy, delta.zw);\n"
    " float finalDelta = max(maxDelta.x, maxDelta.y);\n"
    " edges.xy *= step(finalDelta, 2.0 * delta.xy);\n"
    " return edges;\n"
    "}\n"
    "vec2 SMAADecodeDiagBilinearAccess(vec2 e) {\n"
    " e.r = e.r * abs(5.0 * e.r - 5.0 * 0.75);\n"
    " return round(e);\n"
    "}\n"
    "vec4 SMAADecodeDiagBilinearAccess(vec4 e) {\n"
    " e.rb = e.rb * abs(5.0 * e.rb - 5.0 * 0.75);\n"
    " return round(e);\n"
    "}\n"
    "vec2 SMAASearchDiag1(sampler2D edgesTex, vec2 texcoord, vec2 dir, out vec2 e) {\n"
    " vec4 coord = vec4(texcoord, -1.0, 1.0);\n"
    " vec3 t = vec3(u_metrics.xy, 1.0);\n"
    " while (coord.z < float(16 - 1) &&\n"
    "  coord.w > 0.9) {\n"
    "  coord.xyz = (t * vec3(dir, 1.0) + coord.xyz);\n"
    "  e = textureLod(edgesTex, coord.xy, 0.0).rg;\n"
    "  coord.w = dot(e, vec2(0.5, 0.5));\n"
    " }\n"
    " return coord.zw;\n"
    "}\n"
    "vec2 SMAASearchDiag2(sampler2D edgesTex, vec2 texcoord, vec2 dir, out vec2 e) {\n"
    " vec4 coord = vec4(texcoord, -1.0, 1.0);\n"
    " coord.x += 0.25 * u_metrics.x;\n"
    " vec3 t = vec3(u_metrics.xy, 1.0);\n"
    " while (coord.z < float(16 - 1) &&\n"
    "  coord.w > 0.9) {\n"
    "  coord.xyz = (t * vec3(dir, 1.0) + coord.xyz);\n"
    "  e = textureLod(edgesTex, coord.xy, 0.0).rg;\n"
    "  e = SMAADecodeDiagBilinearAccess(e);\n"
    "  coord.w = dot(e, vec2(0.5, 0.5));\n"
    " }\n"
    " return coord.zw;\n"
    "}\n"
    "vec2 SMAAAreaDiag(sampler2D areaTex, vec2 dist, vec2 e, float offset) {\n"
    " vec2 texcoord = (vec2(20, 20) * e + dist);\n"
    " texcoord = ((1.0 / vec2(160.0, 80.0)) * texcoord + 0.5 * (1.0 / vec2(160.0, 80.0)));\n"
    " texcoord.x += 0.5;\n"
    " texcoord.y += (1.0 / 7.0) * offset;\n"
    " return textureLod(areaTex, texcoord, 0.0).rg;\n"
    "}\n"
    "vec2 SMAACalculateDiagWeights(sampler2D edgesTex, sampler2D areaTex, vec2 texcoord, vec2 e, vec4 subsampleIndices) {\n"
    " vec2 weights = vec2(0.0, 0.0);\n"
    " vec4 d;\n"
    " vec2 end;\n"
    " if (e.r > 0.0) {\n"
    "  d.xz = SMAASearchDiag1(edgesTex, texcoord, vec2(-1.0, 1.0), end);\n"
    "  d.x += float(end.y > 0.9);\n"
    " } else\n"
    "  d.xz = vec2(0.0, 0.0);\n"
    " d.yw = SMAASearchDiag1(edgesTex, texcoord, vec2(1.0, -1.0), end);\n"
    " if (d.x + d.y > 2.0) {\n"
    "  vec4 coords = (vec4(-d.x + 0.25, d.x, d.y, -d.y - 0.25) * u_metrics.xyxy + texcoord.xyxy);\n"
    "  vec4 c;\n"
    "  c.xy = textureLodOffset(edgesTex, coords.xy, 0.0, ivec2(-1, 0)).rg;\n"
    "  c.zw = textureLodOffset(edgesTex, coords.zw, 0.0, ivec2( 1, 0)).rg;\n"
    "  c.yxwz = SMAADecodeDiagBilinearAccess(c.xyzw);\n"
    "  vec2 cc = (vec2(2.0, 2.0) * c.xz + c.yw);\n"
    "  SMAAMovc(bvec2(step(0.9, d.zw)), cc, vec2(0.0, 0.0));\n"
    "  weights += SMAAAreaDiag(areaTex, d.xy, cc, subsampleIndices.z);\n"
    " }\n"
    " d.xz = SMAASearchDiag2(edgesTex, texcoord, vec2(-1.0, -1.0), end);\n"
    " if (textureLodOffset(edgesTex, texcoord, 0.0, ivec2(1, 0)).r > 0.0) {\n"
    "  d.yw = SMAASearchDiag2(edgesTex, texcoord, vec2(1.0, 1.0), end);\n"
    "  d.y += float(end.y > 0.9);\n"
    " } else\n"
    "  d.yw = vec2(0.0, 0.0);\n"
    " if (d.x + d.y > 2.0) {\n"
    "  vec4 coords = (vec4(-d.x, -d.x, d.y, d.y) * u_metrics.xyxy + texcoord.xyxy);\n"
    "  vec4 c;\n"
    "  c.x = textureLodOffset(edgesTex, coords.xy, 0.0, ivec2(-1, 0)).g;\n"
    "  c.y = textureLodOffset(edgesTex, coords.xy, 0.0, ivec2( 0, -1)).r;\n"
    "  c.zw = textureLodOffset(edgesTex, coords.zw, 0.0, ivec2( 1, 0)).gr;\n"
    "  vec2 cc = (vec2(2.0, 2.0) * c.xz + c.yw);\n"
    "  SMAAMovc(bvec2(step(0.9, d.zw)), cc, vec2(0.0, 0.0));\n"
    "  weights += SMAAAreaDiag(areaTex, d.xy, cc, subsampleIndices.w).gr;\n"
    " }\n"
    " return weights;\n"
    "}\n"
    "float SMAASearchLength(sampler2D searchTex, vec2 e, float offset) {\n"
    " vec2 scale = vec2(66.0, 33.0) * vec2(0.5, -1.0);\n"
    " vec2 bias = vec2(66.0, 33.0) * vec2(offset, 1.0);\n"
    " scale += vec2(-1.0, 1.0);\n"
    " bias += vec2( 0.5, -0.5);\n"
    " scale *= 1.0 / vec2(64.0, 16.0);\n"
    " bias *= 1.0 / vec2(64.0, 16.0);\n"
    " return textureLod(searchTex, (scale * e + bias), 0.0).r;\n"
    "}\n"
    "float SMAASearchXLeft(sampler2D edgesTex, sampler2D searchTex, vec2 texcoord, float end) {\n"
    " vec2 e = vec2(0.0, 1.0);\n"
    " while (texcoord.x > end &&\n"
    "  e.g > 0.8281 &&\n"
    "  e.r == 0.0) {\n"
    "  e = textureLod(edgesTex, texcoord, 0.0).rg;\n"
    "  texcoord = (-vec2(2.0, 0.0) * u_metrics.xy + texcoord);\n"
    " }\n"
    " float offset = (-(255.0 / 127.0) * SMAASearchLength(searchTex, e, 0.0) + 3.25);\n"
    " return (u_metrics.x * offset + texcoord.x);\n"
    "}\n"
    "float SMAASearchXRight(sampler2D edgesTex, sampler2D searchTex, vec2 texcoord, float end) {\n"
    " vec2 e = vec2(0.0, 1.0);\n"
    " while (texcoord.x < end &&\n"
    "  e.g > 0.8281 &&\n"
    "  e.r == 0.0) {\n"
    "  e = textureLod(edgesTex, texcoord, 0.0).rg;\n"
    "  texcoord = (vec2(2.0, 0.0) * u_metrics.xy + texcoord);\n"
    " }\n"
    " float offset = (-(255.0 / 127.0) * SMAASearchLength(searchTex, e, 0.5) + 3.25);\n"
    " return (-u_metrics.x * offset + texcoord.x);\n"
    "}\n"
    "float SMAASearchYUp(sampler2D edgesTex, sampler2D searchTex, vec2 texcoord, float end) {\n"
    " vec2 e = vec2(1.0, 0.0);\n"
    " while (texcoord.y > end &&\n"
    "  e.r > 0.8281 &&\n"
    "  e.g == 0.0) {\n"
    "  e = textureLod(edgesTex, texcoord, 0.0).rg;\n"
    "  texcoord = (-vec2(0.0, 2.0) * u_metrics.xy + texcoord);\n"
    " }\n"
    " float offset = (-(255.0 / 127.0) * SMAASearchLength(searchTex, e.gr, 0.0) + 3.25);\n"
    " return (u_metrics.y * offset + texcoord.y);\n"
    "}\n"
    "float SMAASearchYDown(sampler2D edgesTex, sampler2D searchTex, vec2 texcoord, float end) {\n"
    " vec2 e = vec2(1.0, 0.0);\n"
    " while (texcoord.y < end &&\n"
    "  e.r > 0.8281 &&\n"
    "  e.g == 0.0) {\n"
    "  e = textureLod(edgesTex, texcoord, 0.0).rg;\n"
    "  texcoord = (vec2(0.0, 2.0) * u_metrics.xy + texcoord);\n"
    " }\n"
    " float offset = (-(255.0 / 127.0) * SMAASearchLength(searchTex, e.gr, 0.5) + 3.25);\n"
    " return (-u_metrics.y * offset + texcoord.y);\n"
    "}\n"
    "vec2 SMAAArea(sampler2D areaTex, vec2 dist, float e1, float e2, float offset) {\n"
    " vec2 texcoord = (vec2(16, 16) * round(4.0 * vec2(e1, e2)) + dist);\n"
    " texcoord = ((1.0 / vec2(160.0, 80.0)) * texcoord + 0.5 * (1.0 / vec2(160.0, 80.0)));\n"
    " texcoord.y = ((1.0 / 7.0) * offset + texcoord.y);\n"
    " return textureLod(areaTex, texcoord, 0.0).rg;\n"
    "}\n"
    "void SMAADetectHorizontalCornerPattern(sampler2D edgesTex, inout vec2 weights, vec4 texcoord, vec2 d) {\n"
    " vec2 leftRight = step(d.xy, d.yx);\n"
    " vec2 rounding = (1.0 - (float(25) / 100.0)) * leftRight;\n"
    " rounding /= leftRight.x + leftRight.y;\n"
    " vec2 factor = vec2(1.0, 1.0);\n"
    " factor.x -= rounding.x * textureLodOffset(edgesTex, texcoord.xy, 0.0, ivec2(0, 1)).r;\n"
    " factor.x -= rounding.y * textureLodOffset(edgesTex, texcoord.zw, 0.0, ivec2(1, 1)).r;\n"
    " factor.y -= rounding.x * textureLodOffset(edgesTex, texcoord.xy, 0.0, ivec2(0, -2)).r;\n"
    " factor.y -= rounding.y * textureLodOffset(edgesTex, texcoord.zw, 0.0, ivec2(1, -2)).r;\n"
    " weights *= clamp(factor, 0.0, 1.0);\n"
    "}\n"
    "void SMAADetectVerticalCornerPattern(sampler2D edgesTex, inout vec2 weights, vec4 texcoord, vec2 d) {\n"
    " vec2 leftRight = step(d.xy, d.yx);\n"
    " vec2 rounding = (1.0 - (float(25) / 100.0)) * leftRight;\n"
    " rounding /= leftRight.x + leftRight.y;\n"
    " vec2 factor = vec2(1.0, 1.0);\n"
    " factor.x -= rounding.x * textureLodOffset(edgesTex, texcoord.xy, 0.0, ivec2( 1, 0)).g;\n"
    " factor.x -= rounding.y * textureLodOffset(edgesTex, texcoord.zw, 0.0, ivec2( 1, 1)).g;\n"
    " factor.y -= rounding.x * textureLodOffset(edgesTex, texcoord.xy, 0.0, ivec2(-2, 0)).g;\n"
    " factor.y -= rounding.y * textureLodOffset(edgesTex, texcoord.zw, 0.0, ivec2(-2, 1)).g;\n"
    " weights *= clamp(factor, 0.0, 1.0);\n"
    "}\n"
    "vec4 SMAABlendingWeightCalculationPS(vec2 texcoord,\n"
    "         vec2 pixcoord,\n"
    "         vec4 offset[3],\n"
    "         sampler2D edgesTex,\n"
    "         sampler2D areaTex,\n"
    "         sampler2D searchTex,\n"
    "         vec4 subsampleIndices) {\n"
    " vec4 weights = vec4(0.0, 0.0, 0.0, 0.0);\n"
    " vec2 e = texture(edgesTex, texcoord).rg;\n"
    " if (e.g > 0.0) {\n"
    "  weights.rg = SMAACalculateDiagWeights(edgesTex, areaTex, texcoord, e, subsampleIndices);\n"
    "  if (weights.r == -weights.g) {\n"
    "  vec2 d;\n"
    "  vec3 coords;\n"
    "  coords.x = SMAASearchXLeft(edgesTex, searchTex, offset[0].xy, offset[2].x);\n"
    "  coords.y = offset[1].y;\n"
    "  d.x = coords.x;\n"
    "  float e1 = textureLod(edgesTex, coords.xy, 0.0).r;\n"
    "  coords.z = SMAASearchXRight(edgesTex, searchTex, offset[0].zw, offset[2].y);\n"
    "  d.y = coords.z;\n"
    "  d = abs(round((u_metrics.zz * d + -pixcoord.xx)));\n"
    "  vec2 sqrt_d = sqrt(d);\n"
    "  float e2 = textureLodOffset(edgesTex, coords.zy, 0.0, ivec2(1, 0)).r;\n"
    "  weights.rg = SMAAArea(areaTex, sqrt_d, e1, e2, subsampleIndices.y);\n"
    "  coords.y = texcoord.y;\n"
    "  SMAADetectHorizontalCornerPattern(edgesTex, weights.rg, coords.xyzy, d);\n"
    "  } else\n"
    "   e.r = 0.0;\n"
    " }\n"
    " if (e.r > 0.0) {\n"
    "  vec2 d;\n"
    "  vec3 coords;\n"
    "  coords.y = SMAASearchYUp(edgesTex, searchTex, offset[1].xy, offset[2].z);\n"
    "  coords.x = offset[0].x;\n"
    "  d.x = coords.y;\n"
    "  float e1 = textureLod(edgesTex, coords.xy, 0.0).g;\n"
    "  coords.z = SMAASearchYDown(edgesTex, searchTex, offset[1].zw, offset[2].w);\n"
    "  d.y = coords.z;\n"
    "  d = abs(round((u_metrics.ww * d + -pixcoord.yy)));\n"
    "  vec2 sqrt_d = sqrt(d);\n"
    "  float e2 = textureLodOffset(edgesTex, coords.xz, 0.0, ivec2(0, 1)).g;\n"
    "  weights.ba = SMAAArea(areaTex, sqrt_d, e1, e2, subsampleIndices.x);\n"
    "  coords.x = texcoord.x;\n"
    "  SMAADetectVerticalCornerPattern(edgesTex, weights.ba, coords.xyxz, d);\n"
    " }\n"
    " return weights;\n"
    "}\n"
    "vec4 SMAANeighborhoodBlendingPS(vec2 texcoord,\n"
    "        vec4 offset,\n"
    "        sampler2D colorTex,\n"
    "        sampler2D blendTex\n"
    "        ) {\n"
    " vec4 a;\n"
    " a.x = texture(blendTex, offset.xy).a;\n"
    " a.y = texture(blendTex, offset.zw).g;\n"
    " a.wz = texture(blendTex, texcoord).xz;\n"
    " if (dot(a, vec4(1.0, 1.0, 1.0, 1.0)) < 1e-5) {\n"
    "  vec4 color = textureLod(colorTex, texcoord, 0.0);\n"
    "  return color;\n"
    " } else {\n"
    "  bool h = max(a.x, a.z) > max(a.y, a.w);\n"
    "  vec4 blendingOffset = vec4(0.0, a.y, 0.0, a.w);\n"
    "  vec2 blendingWeight = a.yw;\n"
    "  SMAAMovc(bvec4(h, h, h, h), blendingOffset, vec4(a.x, 0.0, a.z, 0.0));\n"
    "  SMAAMovc(bvec2(h, h), blendingWeight, a.xz);\n"
    "  blendingWeight /= dot(blendingWeight, vec2(1.0, 1.0));\n"
    "  vec4 blendingCoord = (blendingOffset * vec4(u_metrics.xy, -u_metrics.xy) + texcoord.xyxy);\n"
    "  vec4 color = blendingWeight.x * textureLod(colorTex, blendingCoord.xy, 0.0);\n"
    "  color += blendingWeight.y * textureLod(colorTex, blendingCoord.zw, 0.0);\n"
    "  return color;\n"
    " }\n"
    "}\n"
    ;

static const char* smaa_edges_fs =
    "uniform sampler2D u_src;\n"
    "void main(){\n"
    "  vec2 uv = gl_FragCoord.xy * u_metrics.xy;\n"
    "  vec4 off[3];\n"   /* SMAAEdgeDetectionVS */
    "  off[0] = u_metrics.xyxy * vec4(-1.0, 0.0, 0.0, -1.0) + uv.xyxy;\n"
    "  off[1] = u_metrics.xyxy * vec4( 1.0, 0.0, 0.0, 1.0) + uv.xyxy;\n"
    "  off[2] = u_metrics.xyxy * vec4(-2.0, 0.0, 0.0, -2.0) + uv.xyxy;\n"
    "  o = vec4(SMAALumaEdgeDetectionPS(uv, off, u_src), 0.0, 0.0);\n"
    "}\n";
static const char* smaa_weights_fs =
    "uniform sampler2D u_edges, u_area, u_search;\n"
    "void main(){\n"
    "  vec2 uv = gl_FragCoord.xy * u_metrics.xy, pix = uv * u_metrics.zw;\n"
    "  vec4 off[3];\n"   /* SMAABlendingWeightCalculationVS, 32 search steps */
    "  off[0] = u_metrics.xyxy * vec4(-0.25, -0.125, 1.25, -0.125) + uv.xyxy;\n"
    "  off[1] = u_metrics.xyxy * vec4(-0.125, -0.25, -0.125, 1.25) + uv.xyxy;\n"
    "  off[2] = u_metrics.xxyy * vec4(-2.0, 2.0, -2.0, 2.0) * 32.0 + vec4(off[0].xz, off[1].yw);\n"
    "  o = SMAABlendingWeightCalculationPS(uv, pix, off, u_edges, u_area, u_search, vec4(0.0));\n"
    "}\n";
static const char* smaa_blend_fs =
    "uniform sampler2D u_src, u_blend;\n"
    "void main(){\n"
    "  vec2 uv = gl_FragCoord.xy * u_metrics.xy;\n"
    "  vec4 off = u_metrics.xyxy * vec4(1.0, 0.0, 0.0, 1.0) + uv.xyxy;\n"   /* SMAANeighborhoodBlendingVS */
    "  o = vec4(SMAANeighborhoodBlendingPS(uv, off, u_src, u_blend).rgb, texelFetch(u_src, ivec2(gl_FragCoord.xy), 0).a);\n"
    "}\n";

static GLuint vao, p_fxaa, p_edges, p_weights, p_blend, t_area, t_search;
static GLuint t_edges, f_edges, t_weights, f_weights;
static int tw, th;

static GLuint smaa_prog(const char* fs)
{
    static char buf[20480];   /* (smaa_lib is ~12 KB) */
    size_t n = 0;
    const char* part[3] = { AA_HEAD, smaa_lib, fs };
    for (int k = 0; k < 3; k++)
        for (const char* s = part[k]; *s && n < sizeof(buf) - 1; s++) buf[n++] = *s;
    buf[n] = 0;
    return gs_gl_compile(aa_vs, buf);
}

static GLuint tex(GLint fmt, int w, int h, GLenum f, const void* px, GLint filt)
{
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, f, GL_UNSIGNED_BYTE, px);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filt);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filt);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

static void target(GLuint* t, GLuint* f, GLint fmt, GLenum fm, int w, int h)
{
    if (*t) glDeleteTextures(1, t);
    *t = tex(fmt, w, h, fm, NULL, GL_LINEAR);
    if (!*f) glGenFramebuffers(1, f);
    glBindFramebuffer(GL_FRAMEBUFFER, *f);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *t, 0);
}

static void uni(GLuint p, const char* n, int unit) { glUniform1i(glGetUniformLocation(p, n), unit); }

static void pass(GLuint p, int w, int h)
{
    glUseProgram(p);
    glUniform4f(glGetUniformLocation(p, "u_metrics"), 1.0f / w, 1.0f / h, (float)w, (float)h);
    glDrawArrays(0x0005 /* GL_TRIANGLE_STRIP */, 0, 4);
}

/* mode 1 FXAA, 2 SMAA: src (w x h, linear filtering) into dst_fbo (as large). The caller turns off
   scissor, depth test, blending and clip distances; afterwards texture unit 0 is active and this
   file's VAO bound. */
void aa_apply(int mode, unsigned int src, unsigned int dst_fbo, int w, int h)
{
    if (!vao) {
        static unsigned char area[AA_AREA_W * AA_AREA_H * 2], search[AA_SEARCH_W * AA_SEARCH_H];
        glGenVertexArrays(1, &vao);
        p_fxaa = gs_gl_compile(aa_vs, fxaa_fs);
        p_edges = smaa_prog(smaa_edges_fs);
        p_weights = smaa_prog(smaa_weights_fs);
        p_blend = smaa_prog(smaa_blend_fs);
        aa_area_table(area);
        aa_search_table(search);
        t_area = tex(GL_RG8, AA_AREA_W, AA_AREA_H, GL_RG, area, GL_LINEAR);
        t_search = tex(GL_R8, AA_SEARCH_W, AA_SEARCH_H, GL_RED, search, GL_NEAREST);
        glUseProgram(p_fxaa); uni(p_fxaa, "u_src", 0);
        glUseProgram(p_edges); uni(p_edges, "u_src", 0);
        glUseProgram(p_weights); uni(p_weights, "u_edges", 1); uni(p_weights, "u_area", 2); uni(p_weights, "u_search", 3);
        glUseProgram(p_blend); uni(p_blend, "u_src", 0); uni(p_blend, "u_blend", 1);
    }
    glBindVertexArray(vao);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glViewport(0, 0, w, h);
    if (mode == 2 && (tw != w || th != h)) {
        target(&t_edges, &f_edges, GL_RG8, GL_RG, w, h);
        target(&t_weights, &f_weights, GL_RGBA8, GL_RGBA, w, h);
        tw = w; th = h;
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src);
    if (mode == 1) {
        glBindFramebuffer(GL_FRAMEBUFFER, dst_fbo);
        pass(p_fxaa, w, h);
        return;
    }
    glClearColor(0, 0, 0, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, f_edges);   /* the edge pass discards where there is no edge */
    glClear(GL_COLOR_BUFFER_BIT);
    pass(p_edges, w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, f_weights);
    glActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D, t_edges);
    glActiveTexture(GL_TEXTURE0 + 2); glBindTexture(GL_TEXTURE_2D, t_area);
    glActiveTexture(GL_TEXTURE0 + 3); glBindTexture(GL_TEXTURE_2D, t_search);
    pass(p_weights, w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, dst_fbo);
    glActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D, t_weights);
    pass(p_blend, w, h);
    glActiveTexture(GL_TEXTURE0);
}
