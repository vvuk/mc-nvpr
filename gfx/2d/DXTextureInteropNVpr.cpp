/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Logging.h"
#include "DXTextureInteropNVpr.h"
#include "DrawTargetNVpr.h"

#include "nvpr/WGL.h"
#include "nvpr/GL.h"

#include <d3d11.h>
#include <d3d10_1.h>
#include <d3d9.h>

using namespace mozilla::gfx::nvpr;
using namespace std;

namespace mozilla {
namespace gfx {

DXTextureInteropNVpr::DXTextureInteropNVpr(void* aDXDevice, const IntSize& aSize, bool& aSuccess)
  : mDXInterop(nullptr)
  , mTextureInterop(nullptr)
  , mTextureId(0)
  , mSize(aSize)
{
  WGL* const wgl = static_cast<WGL*>(gl);

  aSuccess = false;

  if (!wgl->HasWGLExtension(WGL::NV_DX_interop2)) {
    return;
  }

  mDXInterop = wgl->DXOpenDeviceNV(aDXDevice);
  if (!mDXInterop) {
    int err = GetLastError();
    gfxWarning() << "DXOpenDeviceNV failed with " << err << "\n";
    return;
  }

  gl->MakeCurrent();

  IUnknown *aDX = (IUnknown*)aDXDevice;

  // create the D3D texture
  do {
    HRESULT hr;

    ID3D11Device* d3d11 = nullptr;
    hr = aDX->QueryInterface(__uuidof(ID3D11Device), (void **)&d3d11);
    if (SUCCEEDED(hr) && d3d11) {
      RefPtr<ID3D11Texture2D> tex;
      CD3D11_TEXTURE2D_DESC desc(DXGI_FORMAT_B8G8R8A8_UNORM, mSize.width, mSize.height, 1, 1);
      hr = d3d11->CreateTexture2D(&desc, nullptr, byRef(tex));
      if (SUCCEEDED(hr)) {
        RefPtr<ID3D11ShaderResourceView> srv;
        d3d11->CreateShaderResourceView(tex, nullptr, byRef(srv));
        mTextureD3D = tex;
        mShaderResourceViewD3D = srv;
      }
      break;
    }

    ID3D10Device* d3d10 = nullptr;
    hr = aDX->QueryInterface(__uuidof(ID3D10Device), (void **)&d3d10);
    if (SUCCEEDED(hr) && d3d10) {
      RefPtr<ID3D10Texture2D> tex;
      CD3D10_TEXTURE2D_DESC desc(DXGI_FORMAT_B8G8R8A8_UNORM, mSize.width, mSize.height, 1, 1);
      hr = d3d10->CreateTexture2D(&desc, nullptr, byRef(tex));
      if (SUCCEEDED(hr)) {
        RefPtr<ID3D10ShaderResourceView> srv;
        d3d10->CreateShaderResourceView(tex, nullptr, byRef(srv));
        mTextureD3D = tex;
        mShaderResourceViewD3D = srv;
      }
      break;
    }

    // d3d9ex implements d3d9, it's identical for the call we need. I think.
    IDirect3DDevice9* d3d9 = nullptr;
    hr = aDX->QueryInterface(__uuidof(IDirect3DDevice9), (void **)&d3d9);
    if (SUCCEEDED(hr) && d3d9) {
      // TODO D3D9 impl
      gfxWarning() << "Missing D3D9 implementation\n";
      break;
    }

    // failed
    return;
  } while(0);

  if (!mTextureD3D) {
    return;
  }

  // create and register the GL texture
  gl->GenTextures(1, &mTextureId);
  mTextureInterop = wgl->DXRegisterObjectNV(mDXInterop, mTextureD3D, 
                                            mTextureId, GL_TEXTURE_2D,
                                            WGL_ACCESS_WRITE_DISCARD_NV);
  if (!mTextureInterop) {
    return;
  }

  aSuccess = true;
}

DXTextureInteropNVpr::~DXTextureInteropNVpr()
{
  WGL* const wgl = static_cast<WGL*>(gl);

  if (mTextureInterop) {
    wgl->DXUnregisterObjectNV(mDXInterop, mTextureInterop);
  }

  if (mTextureId) {
    gl->MakeCurrent();
    gl->DeleteTexture(mTextureId);
  }

  if (mDXInterop) {
    wgl->DXCloseDeviceNV(mDXInterop);
  }
}

GLuint
DXTextureInteropNVpr::Lock()
{
  WGL* const wgl = static_cast<WGL*>(gl);

  wgl->DXLockObjectsNV(mDXInterop, 1, &mTextureInterop);

  return mTextureId;
}

void
DXTextureInteropNVpr::Unlock()
{
  WGL* const wgl = static_cast<WGL*>(gl);

  wgl->DXUnlockObjectsNV(mDXInterop, 1, &mTextureInterop);
}

bool
DXTextureInteropNVpr::UpdateFrom(DrawTarget* aDrawTarget)
{
  MOZ_ASSERT(aDrawTarget->GetType() == BACKEND_NVPR);
  DrawTargetNVpr *dtnv = static_cast<DrawTargetNVpr*>(aDrawTarget);
  
  if (dtnv->GetSize() != mSize)
    return false;

  dtnv->BlitToDXTexture(this);
  return true;
}

RefPtr<DXTextureInteropNVpr>
DXTextureInteropNVpr::GetForDrawTarget(void* aDX, DrawTarget* aDrawTarget)
{
  MOZ_ASSERT(aDrawTarget->GetType() == BACKEND_NVPR);
  return static_cast<DrawTargetNVpr*>(aDrawTarget)->UpdateInteropTexture(aDX);
}

}
}
