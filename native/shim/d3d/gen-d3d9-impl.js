// Generate the COM boilerplate for the D3D9 shim straight from the SDK header.
//
// IDirect3DDevice9 alone has 119 methods; hand-typing them would guarantee a
// signature typo somewhere, and a wrong signature here is a vtable mismatch that
// crashes at run time rather than at compile time. So the declarations are
// parsed out of d3d9.h and the bodies are emitted mechanically.
//
//   node gen-d3d9-impl.js            -> writes d3d9_gen.h
//
// Bodies default to "succeed and do nothing", which is exactly what phase 2
// (headless boot) needs. Phase 3 overrides the ones that must really draw by
// listing them in OVERRIDE below, so the generator keeps producing the rest.

const fs = require('fs');
const path = require('path');

const HEADER = path.join(__dirname, '../../../../SOURCE/Tik/DXInclude/d3d9.h');
const OUT = path.join(__dirname, 'd3d9_gen.h');

// Interfaces the shim implements. Order matters only for readability.
const INTERFACES = [
  'IDirect3D9', 'IDirect3DDevice9', 'IDirect3DResource9', 'IDirect3DBaseTexture9',
  'IDirect3DTexture9', 'IDirect3DCubeTexture9', 'IDirect3DVolumeTexture9',
  'IDirect3DSurface9', 'IDirect3DVolume9', 'IDirect3DVertexBuffer9',
  'IDirect3DIndexBuffer9', 'IDirect3DStateBlock9', 'IDirect3DSwapChain9',
  'IDirect3DVertexDeclaration9', 'IDirect3DVertexShader9', 'IDirect3DPixelShader9',
  'IDirect3DQuery9',
];

// Methods hand-written elsewhere (declared here, defined in d3d9_impl.cpp).
const OVERRIDE = new Set([
  //  Cube textures have a real implementation now: the specular passes bind
  //  one to stage 1 and sample it with the camera-space normal.
  'IDirect3DCubeTexture9::QueryInterface',
  'IDirect3DCubeTexture9::AddRef',
  'IDirect3DCubeTexture9::Release',
  'IDirect3DCubeTexture9::GetLevelCount',
  'IDirect3DCubeTexture9::GetType',
  'IDirect3DCubeTexture9::GetLevelDesc',
  'IDirect3D9::QueryInterface', 'IDirect3D9::AddRef', 'IDirect3D9::Release',
  'IDirect3D9::GetDeviceCaps', 'IDirect3D9::GetAdapterCount',
  'IDirect3D9::GetAdapterIdentifier', 'IDirect3D9::GetAdapterModeCount',
  'IDirect3D9::EnumAdapterModes', 'IDirect3D9::GetAdapterDisplayMode',
  'IDirect3D9::CheckDeviceType', 'IDirect3D9::CheckDeviceFormat',
  'IDirect3D9::CheckDeviceMultiSampleType', 'IDirect3D9::CheckDepthStencilMatch',
  'IDirect3D9::CreateDevice',

  //  StretchRect drives the engine's whole off-screen chain and is gated on a
  //  cap, so it has a real body; GetCreationParameters is read from the stack by
  //  DxSurfaceTex and must not be left uninitialised.
  'IDirect3DDevice9::StretchRect', 'IDirect3DDevice9::GetCreationParameters',
  'IDirect3DDevice9::SetClipPlane',
  'IDirect3DDevice9::CreateAdditionalSwapChain',
  'IDirect3DDevice9::QueryInterface', 'IDirect3DDevice9::AddRef', 'IDirect3DDevice9::Release',
  'IDirect3DDevice9::GetDirect3D', 'IDirect3DDevice9::GetDeviceCaps',
  'IDirect3DDevice9::Present', 'IDirect3DDevice9::Clear',
  'IDirect3DDevice9::BeginScene', 'IDirect3DDevice9::EndScene',
  'IDirect3DDevice9::SetRenderState', 'IDirect3DDevice9::GetRenderState',
  'IDirect3DDevice9::SetTextureStageState', 'IDirect3DDevice9::GetTextureStageState',
  'IDirect3DDevice9::SetSamplerState', 'IDirect3DDevice9::GetSamplerState',
  'IDirect3DDevice9::SetTransform', 'IDirect3DDevice9::GetTransform',
  'IDirect3DDevice9::MultiplyTransform',
  'IDirect3DDevice9::SetTexture', 'IDirect3DDevice9::GetTexture',
  'IDirect3DDevice9::SetFVF', 'IDirect3DDevice9::GetFVF',
  'IDirect3DDevice9::SetStreamSource', 'IDirect3DDevice9::SetIndices',
  'IDirect3DDevice9::DrawPrimitive', 'IDirect3DDevice9::DrawIndexedPrimitive',
  'IDirect3DDevice9::DrawPrimitiveUP', 'IDirect3DDevice9::DrawIndexedPrimitiveUP',
  'IDirect3DDevice9::CreateTexture', 'IDirect3DDevice9::CreateVertexBuffer',
  'IDirect3DDevice9::CreateIndexBuffer',
  'IDirect3DDevice9::SetViewport', 'IDirect3DDevice9::GetViewport',
  'IDirect3DDevice9::SetLight', 'IDirect3DDevice9::GetLight',
  'IDirect3DDevice9::LightEnable', 'IDirect3DDevice9::GetLightEnable',
  'IDirect3DDevice9::SetMaterial', 'IDirect3DDevice9::GetMaterial',
  'IDirect3DDevice9::BeginStateBlock', 'IDirect3DDevice9::EndStateBlock',
  'IDirect3DDevice9::Reset', 'IDirect3DDevice9::TestCooperativeLevel',
  'IDirect3DDevice9::GetBackBuffer', 'IDirect3DDevice9::GetRenderTarget',
  'IDirect3DDevice9::SetRenderTarget', 'IDirect3DDevice9::GetDepthStencilSurface',
  'IDirect3DDevice9::SetDepthStencilSurface', 'IDirect3DDevice9::CreateRenderTarget',
  'IDirect3DDevice9::CreateDepthStencilSurface',
  'IDirect3DDevice9::CreateOffscreenPlainSurface', 'IDirect3DDevice9::GetDisplayMode',

  'IDirect3DTexture9::QueryInterface', 'IDirect3DTexture9::AddRef', 'IDirect3DTexture9::Release',
  'IDirect3DTexture9::LockRect', 'IDirect3DTexture9::UnlockRect',
  'IDirect3DTexture9::GetLevelDesc', 'IDirect3DTexture9::GetSurfaceLevel',
  'IDirect3DTexture9::GetLevelCount',

  'IDirect3DVertexBuffer9::QueryInterface', 'IDirect3DVertexBuffer9::AddRef',
  'IDirect3DVertexBuffer9::Release', 'IDirect3DVertexBuffer9::Lock',
  'IDirect3DVertexBuffer9::Unlock', 'IDirect3DVertexBuffer9::GetDesc',

  'IDirect3DIndexBuffer9::QueryInterface', 'IDirect3DIndexBuffer9::AddRef',
  'IDirect3DIndexBuffer9::Release', 'IDirect3DIndexBuffer9::Lock',
  'IDirect3DIndexBuffer9::Unlock', 'IDirect3DIndexBuffer9::GetDesc',

  'IDirect3DSurface9::QueryInterface', 'IDirect3DSurface9::AddRef',
  'IDirect3DSurface9::Release', 'IDirect3DSurface9::LockRect',
  'IDirect3DSurface9::UnlockRect', 'IDirect3DSurface9::GetDesc',

  'IDirect3DStateBlock9::QueryInterface', 'IDirect3DStateBlock9::AddRef',
  'IDirect3DStateBlock9::Release', 'IDirect3DStateBlock9::Capture',
  'IDirect3DStateBlock9::Apply',
]);

const src = fs.readFileSync(HEADER, 'utf8').replace(/\r/g, '');

function block(name) {
  const start = src.indexOf(`DECLARE_INTERFACE_(${name},`);
  if (start < 0) throw new Error(`interface not found: ${name}`);
  const end = src.indexOf('\n};', start);
  return src.slice(start, end);
}

// STDMETHOD(Name)(THIS_ args) PURE;  or  STDMETHOD_(Ret,Name)(THIS_ args) PURE;
function methods(text) {
  const out = [];
  const re = /STDMETHOD(_)?\(\s*([^)]*?)\s*\)\s*\(\s*THIS(_)?\s*([\s\S]*?)\)\s*PURE\s*;/g;
  let m;
  while ((m = re.exec(text)) !== null) {
    const hasRet = !!m[1];
    let ret = 'HRESULT', name;
    if (hasRet) {
      const i = m[2].lastIndexOf(',');
      ret = m[2].slice(0, i).trim();
      name = m[2].slice(i + 1).trim();
    } else {
      name = m[2].trim();
    }
    const args = m[4].replace(/\s+/g, ' ').trim();
    out.push({ ret, name, args });
  }
  return out;
}

// An out-parameter that returns an INTERFACE is the dangerous case: reporting
// D3D_OK while leaving the caller's pointer untouched hands it garbage, and the
// crash then lands somewhere far from the cause. Those null the pointer and
// report failure, so an unimplemented call fails exactly where it happens.
function outInterfaceParam(args) {
  const last = (args || '').split(',').pop() || '';
  const m = /(\w+)\s*\*\*\s*(\w+)\s*$/.exec(last.trim());
  if (!m) return null;
  return /^(IDirect3D|ID3DX|IUnknown)/.test(m[1]) ? m[2] : null;
}

// A body that satisfies the contract without doing anything.
function body(iface, m) {
  const outp = outInterfaceParam(m.args);
  if (outp && m.ret === 'HRESULT')
    return `{ if (${outp}) *${outp} = NULL; return D3DERR_NOTAVAILABLE; }`;
  if (m.ret === 'void') return '{ }';
  if (m.ret === 'ULONG') return '{ return 1; }';
  if (m.ret === 'UINT') return '{ return 0; }';
  if (m.ret === 'HRESULT') return '{ return D3D_OK; }';
  if (m.ret === 'BOOL') return '{ return FALSE; }';
  if (m.ret === 'float' || m.ret === 'FLOAT') return '{ return 0.0f; }';
  if (m.ret === 'DWORD') return '{ return 0; }';
  if (m.ret.includes('*')) return '{ return NULL; }';
  return `{ return (${m.ret})0; }`;
}

// Parameter names are dropped for unimplemented methods to avoid -Wunused noise.
function stripNames(args) {
  if (!args) return '';
  const parts = [];
  let depth = 0, cur = '';
  for (const ch of args) {
    if (ch === '(' || ch === '<') depth++;
    if (ch === ')' || ch === '>') depth--;
    if (ch === ',' && depth === 0) { parts.push(cur); cur = ''; } else cur += ch;
  }
  if (cur.trim()) parts.push(cur);
  return parts.map(p => {
    p = p.trim();
    if (!p) return p;
    if (/\[\s*\]\s*$/.test(p)) return p;               // array params: keep as-is
    return p.replace(/\s+[A-Za-z_][A-Za-z0-9_]*\s*$/, '');
  }).join(', ');
}

let out = `// GENERATED by gen-d3d9-impl.js from SOURCE/Tik/DXInclude/d3d9.h — do not edit.
// Re-run:  node shim/d3d/gen-d3d9-impl.js
//
// One macro per interface, expanding to every method the interface declares.
// A shim class uses it and then overrides only what it really implements; the
// rest compile to harmless success. This keeps the vtables exactly in step with
// the SDK header no matter which methods get real bodies later.
#pragma once

`;

for (const iface of INTERFACES) {
  let ms;
  try { ms = methods(block(iface)); } catch (e) { out += `// ${iface}: ${e.message}\n`; continue; }
  const macro = 'RAN_D3D9_STUBS_' + iface.toUpperCase();
  out += `// ---- ${iface} (${ms.length} methods) ----\n#define ${macro} \\\n`;
  const lines = [];
  for (const m of ms) {
    const key = `${iface}::${m.name}`;
    if (OVERRIDE.has(key)) continue;                     // hand-written elsewhere
    // Keep parameter names only where the body actually uses one.
    const sig = outInterfaceParam(m.args) ? m.args : stripNames(m.args);
    lines.push(`    ${m.ret} ${m.name}(${sig}) override ${body(iface, m)}`);
  }
  out += lines.join(' \\\n') + '\n\n';
}

fs.writeFileSync(OUT, out);
const total = INTERFACES.reduce((n, i) => { try { return n + methods(block(i)).length; } catch { return n; } }, 0);
console.log(`wrote ${path.basename(OUT)} — ${INTERFACES.length} interfaces, ${total} methods parsed, ${OVERRIDE.size} hand-written`);
