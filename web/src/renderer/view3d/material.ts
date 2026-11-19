// SPDX-License-Identifier: GPL-3.0-or-later
//
// The materials of a shape's faces: the colours and the light of the
// original's 3D view (voxelFrame_c::drawShape and draw() in
// src/gui/voxelframe.cpp), as a shader over the per-vertex attributes of
// mesh.ts, so the view's options change uniforms, not geometry.
//
// A face's colour, as the original chose it: in the colour view, a
// constraint colour (aColour > 0) unless the face is the variable marker;
// the variable marker (flag 4) black; the checker's light faces (flag 1)
// the piece colour lightened (lightPieceColor), when the style has the
// checker; every other face the piece colour darkened (darkPieceColor).
// Lit, as the original's fixed-function light: GL_COLOR_MATERIAL with the
// global ambient 0.2, a light of ambient 0.2 and diffuse 0.6 at (700, 200,
// 700) in eye space, no specular: colour * (0.4 + 0.6 * max(0, n . l)).
// Unlit: the colour itself. Colours are the original's numbers, written to
// the screen as they are (no colour management), as OpenGL did.
//
// A sliding piece's picture (decision 0038) takes the place of the piece
// colour on the faces looking up: the engine gives each of their vertices
// its place in the picture (the frame's imageUv, -1 elsewhere), so the
// picture turns with the piece in any orientation; uImageSrc picks the
// part shown out of the texture.

import {
  ClampToEdgeWrapping,
  DataTexture,
  DoubleSide,
  LinearFilter,
  LinearMipmapLinearFilter,
  NearestFilter,
  NoColorSpace,
  RGBAFormat,
  ShaderMaterial,
  Texture,
  UnsignedByteType,
  Vector3,
  Vector4,
} from 'three';
import type { IUniform } from 'three';

import type { Rgb3 } from '../shell/piece-colours.ts';
import type { PiecePicture } from '../shell/picture-math.ts';

/** Which faces a material draws, for x-ray's two passes. */
export type FacePass = 'normal' | 'inside' | 'outside';

const vertexShader = /* glsl */ `
attribute float aFlags;
attribute float aColour;
attribute vec3 aCell;
attribute vec2 aUv;
varying vec3 vNormal;
varying vec3 vViewPos;
varying float vFlags;
varying float vColour;
varying vec3 vCell;
varying vec2 vUv;
void main() {
  vec4 mv = modelViewMatrix * vec4(position, 1.0);
  vViewPos = mv.xyz;
  vNormal = normalize(normalMatrix * normal);
  vFlags = aFlags;
  vColour = aColour;
  vCell = aCell;
  vUv = aUv;
  gl_Position = projectionMatrix * mv;
}
`;

const fragmentShader = /* glsl */ `
uniform vec3 uPiece;
uniform float uChecker;
uniform float uColourView;
uniform sampler2D uPalette;
uniform float uLit;
uniform vec3 uLightPos;
uniform int uPass;
uniform int uDimAxis;
uniform float uLayer;
uniform float uAlpha;
uniform sampler2D uImage;
uniform float uImageOn;
uniform vec4 uImageSrc;
varying vec3 vNormal;
varying vec3 vViewPos;
varying float vFlags;
varying float vColour;
varying vec3 vCell;
varying vec2 vUv;

bool has(float flags, float bit) {
  return mod(floor(flags / bit), 2.0) > 0.5;
}

void main() {
  bool inside = has(vFlags, 128.0);
  // pass 0: the normal view, inside faces hidden; 1: x-ray's inside faces;
  // 2: x-ray's outer faces, faint
  if (uPass == 0 && inside) discard;
  if (uPass == 1 && !inside) discard;
  if (uPass == 2 && inside) discard;
  vec3 c;
  bool marker = has(vFlags, 4.0);
  if (uColourView > 0.5 && vColour > 0.5 && !marker) {
    c = texture2D(uPalette, vec2((vColour + 0.5) / 256.0, 0.5)).rgb;
  } else if (marker) {
    c = vec3(0.0);
  } else if (uImageOn > 0.5 && all(greaterThanEqual(vUv, vec2(0.0))) && all(lessThanEqual(vUv, vec2(1.0)))) {
    // the picture where the piece looks up, over the checker (outside its
    // box: the colour)
    c = texture2D(uImage, uImageSrc.xy + vUv * uImageSrc.zw).rgb;
  } else if (uChecker > 0.5 && has(vFlags, 1.0)) {
    c = 1.0 - 0.9 * (1.0 - uPiece);
  } else {
    c = 0.9 * uPiece;
  }
  if (uDimAxis >= 0 && abs(vCell[uDimAxis] - uLayer) > 0.5) {
    // another layer than the editor's: the original's dimmed piece
    c = 1.0 - (1.0 - c) * 0.2;
  }
  if (uLit > 0.5) {
    vec3 l = normalize(uLightPos - vViewPos);
    vec3 n = normalize(vNormal);
    c = c * (0.4 + 0.6 * max(0.0, dot(n, l)));
  }
  gl_FragColor = vec4(c, uPass == 2 ? 0.1 : uAlpha);
}
`;

/** The uniforms every face material of a view shares. */
export interface FaceUniforms {
  [name: string]: IUniform;
  uPiece: IUniform<Vector3>;
  uChecker: IUniform<number>;
  uColourView: IUniform<number>;
  uPalette: IUniform<DataTexture>;
  uLit: IUniform<number>;
  uLightPos: IUniform<Vector3>;
  uDimAxis: IUniform<number>;
  uLayer: IUniform<number>;
  uAlpha: IUniform<number>;
  uImage: IUniform<Texture | null>;
  uImageOn: IUniform<number>;
  uImageSrc: IUniform<Vector4>;
}

/** Fresh uniforms: a white piece, lit, no palette. */
export function faceUniforms(): FaceUniforms {
  const data = new Uint8Array(256 * 4).fill(255);
  const palette = new DataTexture(data, 256, 1, RGBAFormat, UnsignedByteType);
  palette.magFilter = NearestFilter;
  palette.minFilter = NearestFilter;
  palette.needsUpdate = true;
  return {
    uPiece: { value: new Vector3(1, 1, 1) },
    uChecker: { value: 1 },
    uColourView: { value: 0 },
    uPalette: { value: palette },
    uLit: { value: 1 },
    uLightPos: { value: new Vector3(700, 200, 700) },
    uDimAxis: { value: -1 },
    uLayer: { value: 0 },
    uAlpha: { value: 1 },
    uImage: { value: null },
    uImageOn: { value: 0 },
    uImageSrc: { value: new Vector4(0, 0, 1, 1) },
  };
}

/** Sets the piece colour. */
export function setPiece(u: FaceUniforms, c: Rgb3): void {
  u.uPiece.value.set(c[0], c[1], c[2]);
}

/** The textures of the pictures, by their hash: one for every view. */
const textures = new Map<string, Texture>();

function pictureTexture(p: PiecePicture): Texture {
  const have = textures.get(p.picture.hash);
  if (have !== undefined) {
    return have;
  }
  const t = new Texture(p.picture.bitmap);
  // the bytes as they are, as the colours (no colour management); an
  // ImageBitmap's first row is the picture's top, at v 0
  t.colorSpace = NoColorSpace;
  t.flipY = false;
  t.wrapS = ClampToEdgeWrapping;
  t.wrapT = ClampToEdgeWrapping;
  t.magFilter = LinearFilter;
  t.minFilter = LinearMipmapLinearFilter;
  t.needsUpdate = true;
  textures.set(p.picture.hash, t);
  return t;
}

/** Sets the piece's picture (null: none, or not decoded yet: the piece colour). */
export function setPicture(u: FaceUniforms, p: PiecePicture | null): void {
  if (p === null) {
    u.uImageOn.value = 0;
    u.uImage.value = null;
    return;
  }
  const [sx, sy, sw, sh] = p.image.src;
  u.uImage.value = pictureTexture(p);
  u.uImageSrc.value.set(
    sx / p.picture.width,
    sy / p.picture.height,
    sw / p.picture.width,
    sh / p.picture.height,
  );
  u.uImageOn.value = 1;
}

/** Sets the constraint colours: `colours[n - 1]` is colour number n, `#rrggbb`. */
export function setPalette(u: FaceUniforms, colours: readonly string[]): void {
  const tex = u.uPalette.value;
  const data = tex.image.data as Uint8Array;
  colours.slice(0, 255).forEach((h, i) => {
    const n = Number.parseInt(h.slice(1), 16);
    const o = 4 * (i + 1);
    data[o] = (n >> 16) & 255;
    data[o + 1] = (n >> 8) & 255;
    data[o + 2] = n & 255;
  });
  tex.needsUpdate = true;
}

/** A material of the faces over shared uniforms, for one pass. */
export function faceMaterial(u: FaceUniforms, pass: FacePass): ShaderMaterial {
  const m = new ShaderMaterial({
    uniforms: { ...u, uPass: { value: pass === 'normal' ? 0 : pass === 'inside' ? 1 : 2 } },
    vertexShader,
    fragmentShader,
    side: DoubleSide,
    transparent: pass === 'outside',
    depthTest: pass !== 'outside',
    depthWrite: pass !== 'outside',
  });
  // the flat style's edge lines are drawn over the faces: push the faces back
  m.polygonOffset = true;
  m.polygonOffsetFactor = 1;
  m.polygonOffsetUnits = 1;
  return m;
}
