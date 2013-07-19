/* -*- Mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 40; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedSurfaceD3D10Interop.h"

#include <d3d10_1.h>
#include "GLContext.h"
#include "WGLLibrary.h"

using namespace mozilla::gfx;
using namespace mozilla::gl;


SurfaceFactory_D3D10Interop*
SurfaceFactory_D3D10Interop::Create(GLContext* gl,
                                    ID3D10Device1* d3d,
                                    const SurfaceCaps& caps)
{
    WGLLibrary* wgl = (WGLLibrary*)gl->GetWGLLibrary();
    if (!wgl || !wgl->HasDXInterop2())
        return nullptr;

    HANDLE wglD3DDevice = wgl->fDXOpenDevice(d3d);
    if (!wglD3DDevice) {
        NS_WARNING("Failed to open D3D device for use by WGL.");
        return nullptr;
    }

    return new SurfaceFactory_D3D10Interop(gl, wgl, wglD3DDevice, d3d, caps);
}

SurfaceFactory_D3D10Interop::~SurfaceFactory_D3D10Interop()
{
    mWGL->fDXCloseDevice(mWGLD3DDevice);
}

SharedSurface*
SharedSurface_D3D10Interop::Create(GLContext* gl,
                                   WGLLibrary* wgl,
                                   HANDLE mWGLD3DDevice,
                                   ID3D10Device1* d3d,
                                   const gfxIntSize& size,
                                   bool hasAlpha)
{
    // Create a texture in case we need to readback.
    DXGI_FORMAT format = hasAlpha ? DXGI_FORMAT_B8G8R8A8_UNORM
                                  : DXGI_FORMAT_B8G8R8X8_UNORM;
    CD3D10_TEXTURE2D_DESC desc(format, size.width, size.height, 1, 1);
    nsRefPtr<ID3D10Texture2D> textureD3D;
    HRESULT hr = d3d->CreateTexture2D(&desc, nullptr, getter_AddRefs(textureD3D));
    if (FAILED(hr)) {
        NS_WARNING("Failed to create texture for CanvasLayer!");
        return nullptr;
    }

    nsRefPtr<ID3D10ShaderResourceView> srv;
    d3d->CreateShaderResourceView(textureD3D, nullptr, getter_AddRefs(srv));
    MOZ_ASSERT(srv);

    GLuint textureGL = 0;

    gl->MakeCurrent();
    gl->fGenTextures(1, &textureGL);
    HANDLE textureWGL = wgl->fDXRegisterObject(mWGLD3DDevice,
                                               textureD3D,
                                               textureGL,
                                               LOCAL_GL_TEXTURE_2D,
                                               LOCAL_WGL_ACCESS_READ_WRITE);
    if (!textureWGL) {
        NS_WARNING("Failed to register D3D object with WGL.");
        return nullptr;
    }

    return new SharedSurface_D3D10Interop(gl, textureGL,
                                          wgl, mWGLD3DDevice, textureWGL,
                                          srv,
                                          size, hasAlpha);
}

SharedSurface_D3D10Interop::~SharedSurface_D3D10Interop()
{
    mGL->MakeCurrent();
    mWGL->fDXUnlockObjects(mWGLD3DDevice, 1, &mTextureWGL);
    mWGL->fDXUnregisterObject(mWGLD3DDevice, mTextureWGL);
}

void
SharedSurface_D3D10Interop::LockProdImpl()
{
    printf_stderr("0x%08x<D3D10Interop>::LockProd\n", this);
    mGL->MakeCurrent();
    MOZ_ALWAYS_TRUE( mWGL->fDXLockObjects(mWGLD3DDevice, 1, &mTextureWGL) );
}

void
SharedSurface_D3D10Interop::UnlockProdImpl()
{
    printf_stderr("0x%08x<D3D10Interop>::UnlockProd\n", this);
    mGL->MakeCurrent();
    MOZ_ALWAYS_TRUE( mWGL->fDXUnlockObjects(mWGLD3DDevice, 1, &mTextureWGL) );
}


void
SharedSurface_D3D10Interop::Fence()
{
    // mGL->MakeCurrent();
    // mGL->fFinish();
}

bool
SharedSurface_D3D10Interop::WaitSync()
{
    // Since we glFinish in Fence(), we're always going to be resolved here.
    return true;
}
