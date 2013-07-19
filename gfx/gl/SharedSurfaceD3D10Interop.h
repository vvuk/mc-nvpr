/* -*- Mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 40; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SHARED_SURFACE_D3D10_INTEROP_H_
#define SHARED_SURFACE_D3D10_INTEROP_H_

#include "SharedSurfaceGL.h"
#include "SurfaceFactory.h"
#include "SurfaceTypes.h"
#include <windows.h>
#include "GLDefs.h"

struct ID3D10Device1;
struct ID3D10ShaderResourceView;
namespace mozilla {
namespace gl {
class GLContext;
class WGLLibrary;

class SharedSurface_D3D10Interop
    : public SharedSurface_GL
{
public:
    static SharedSurface* Create(GLContext* gl,
                                 WGLLibrary* wgl,
                                 HANDLE mWGLD3DDevice,
                                 ID3D10Device1* d3d,
                                 const gfxIntSize& size,
                                 bool hasAlpha);

    static SharedSurface_D3D10Interop* Cast(SharedSurface* surf) {
        MOZ_ASSERT(surf->Type() == SharedSurfaceType::DXGLInterop2);

        return (SharedSurface_D3D10Interop*)surf;
    }

protected:
    GLuint mTextureGL;
    WGLLibrary* const mWGL;
    HANDLE mWGLD3DDevice;
    HANDLE mTextureWGL;
    nsRefPtr<ID3D10ShaderResourceView> mSRV;

    SharedSurface_D3D10Interop(GLContext* gl,
                               GLuint textureGL,
                               WGLLibrary* wgl,
                               HANDLE wglD3DDevice,
                               HANDLE textureWGL,
                               ID3D10ShaderResourceView* srv,
                               const gfxIntSize& size,
                               bool hasAlpha)
        : SharedSurface_GL(SharedSurfaceType::DXGLInterop2,
                           AttachmentType::GLTexture,
                           gl,
                           size,
                           hasAlpha)
        , mTextureGL(textureGL)
        , mWGL(wgl)
        , mWGLD3DDevice(wglD3DDevice)
        , mTextureWGL(textureWGL)
        , mSRV(srv)
    {}

public:
    virtual ~SharedSurface_D3D10Interop();

    virtual void LockProdImpl();
    virtual void UnlockProdImpl();

    virtual void Fence();
    virtual bool WaitSync();

    // Implementation-specific functions below:
    ID3D10ShaderResourceView* GetSRV() {
        return mSRV;
    }
};


class SurfaceFactory_D3D10Interop
    : public SurfaceFactory_GL
{
protected:
    WGLLibrary* const mWGL;
    const HANDLE mWGLD3DDevice;
    nsRefPtr<ID3D10Device1> mD3D;

public:
    static SurfaceFactory_D3D10Interop* Create(GLContext* gl,
                                               ID3D10Device1* d3d,
                                               const SurfaceCaps& caps);

protected:
    SurfaceFactory_D3D10Interop(GLContext* gl,
                                WGLLibrary* wgl,
                                HANDLE wglD3DDevice,
                                ID3D10Device1* d3d,
                                const SurfaceCaps& caps)
        : SurfaceFactory_GL(gl, SharedSurfaceType::DXGLInterop2, caps)
        , mWGL(wgl)
        , mWGLD3DDevice(wglD3DDevice)
        , mD3D(d3d)
    {}

    virtual SharedSurface* CreateShared(const gfxIntSize& size) {
        bool hasAlpha = mReadCaps.alpha;
        return SharedSurface_D3D10Interop::Create(mGL, mWGL, mWGLD3DDevice, mD3D, size, hasAlpha);
    }

public:
    virtual ~SurfaceFactory_D3D10Interop();
};

} // namespace gl
} // namespace mozilla

#endif // SHARED_SURFACE_D3D10_INTEROP_H_
