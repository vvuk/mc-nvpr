/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CanvasLayerD3D10.h"

#include "../d3d9/Nv3DVUtils.h"
#include "gfxImageSurface.h"
#include "gfxWindowsSurface.h"
#include "gfxWindowsPlatform.h"
#include "SurfaceStream.h"
#include "SharedSurfaceANGLE.h"
#include "SharedSurfaceD3D10Interop.h"
#include "gfxContext.h"
#include "GLContext.h"

using namespace mozilla::gl;
using namespace mozilla::gfx;

namespace mozilla {
namespace layers {

CanvasLayerD3D10::CanvasLayerD3D10(LayerManagerD3D10 *aManager)
  : CanvasLayer(aManager, nullptr)
  , LayerD3D10(aManager)
  , mDataIsPremultiplied(false)
  , mNeedsYFlip(false)
  , mHasAlpha(true)
{
    mImplData = static_cast<LayerD3D10*>(this);
    mForceReadback = Preferences::GetBool("webgl.force-layers-readback", false);
}

CanvasLayerD3D10::~CanvasLayerD3D10()
{
}

void
CanvasLayerD3D10::Initialize(const Data& aData)
{
  NS_ASSERTION(mSurface == nullptr, "BasicCanvasLayer::Initialize called twice!");

  mBounds.SetRect(0, 0, aData.mSize.width, aData.mSize.height);

  if (aData.mSurface) {
    mSurface = aData.mSurface;
    NS_ASSERTION(!aData.mGLContext && !aData.mDrawTarget,
                 "CanvasLayer can't have both surface and WebGLContext/DrawTarget");
    mNeedsYFlip = false;
    mDataIsPremultiplied = true;
  } else if (aData.mGLContext) {
    mGLContext = aData.mGLContext;
    NS_ASSERTION(mGLContext->IsOffscreen(), "Canvas GLContext must be offscreen.");
    mDataIsPremultiplied = aData.mIsGLAlphaPremult;
    mNeedsYFlip = true;

    GLScreenBuffer* screen = mGLContext->Screen();
    SurfaceStreamType streamType =
        SurfaceStream::ChooseGLStreamType(SurfaceStream::MainThread,
                                          screen->PreserveBuffer());

    SurfaceFactory_GL* factory = nullptr;
    if (!mForceReadback) {
      if (mGLContext->IsANGLE()) {
        factory = SurfaceFactory_ANGLEShareHandle::Create(mGLContext,
                                                          device(),
                                                          screen->Caps());
      } else {
        // Instead of worrying about coming in and out of Lock, just
        // pretend we're OMTC for the purpose of buffering selection.
        streamType = SurfaceStream::ChooseGLStreamType(SurfaceStream::OffMainThread,
                                                       screen->PreserveBuffer());
        factory = SurfaceFactory_D3D10Interop::Create(mGLContext,
                                                      device(),
                                                      screen->Caps());
      }
    }

    if (factory) {
      screen->Morph(factory, streamType);
    }
  } else if (aData.mDrawTarget) {
    mDrawTarget = aData.mDrawTarget;
    mNeedsYFlip = false;
    mDataIsPremultiplied = true;
    void *texture = mDrawTarget->GetNativeSurface(NATIVE_SURFACE_D3D10_TEXTURE);

    if (texture) {
      mTexture = static_cast<ID3D10Texture2D*>(texture);

      NS_ASSERTION(!aData.mGLContext && !aData.mSurface,
                   "CanvasLayer can't have both surface and WebGLContext/Surface");

      device()->CreateShaderResourceView(mTexture, nullptr, getter_AddRefs(mSRView));
      return;
    } 

    if (mDrawTarget->GetType() != BACKEND_NVPR) {
      // XXX we should store mDrawTarget and use it directly in UpdateSurface,
      // bypassing Thebes
      mSurface = gfxPlatform::GetPlatform()->GetThebesSurfaceForDrawTarget(mDrawTarget);
    } else {
      mTextureInteropNVpr = DXTextureInteropNVpr::GetForDrawTarget(device(), mDrawTarget);
      if (mTextureInteropNVpr) {
        mTextureInteropNVpr->GetD3DShaderResourceView()->QueryInterface(__uuidof(ID3D10ShaderResourceView),
                                                                        getter_AddRefs(mSRView));
        return; // all done
      }

      mTextureInteropNVpr = nullptr;
      NS_WARNING("Failed to create texture or DXTextureInteropNVpr for NVPR D3D10 interop!");
    }
  } else {
    NS_ERROR("CanvasLayer created without mSurface, mDrawTarget or mGLContext?");
  }

  mBounds.SetRect(0, 0, aData.mSize.width, aData.mSize.height);

  if (mSurface && mSurface->GetType() == gfxSurfaceTypeD2D) {
    void *data = mSurface->GetData(&gKeyD3D10Texture);
    if (data) {
      mTexture = static_cast<ID3D10Texture2D*>(data);
      mIsD2DTexture = true;
      device()->CreateShaderResourceView(mTexture, nullptr, getter_AddRefs(mSRView));
      mHasAlpha =
        mSurface->GetContentType() == GFX_CONTENT_COLOR_ALPHA;
      return;
    }
  }

  mIsD2DTexture = false;

  // Create a texture in case we need to readback.
  CD3D10_TEXTURE2D_DESC desc(DXGI_FORMAT_B8G8R8A8_UNORM, mBounds.width, mBounds.height, 1, 1);
  desc.Usage = D3D10_USAGE_DYNAMIC;
  desc.CPUAccessFlags = D3D10_CPU_ACCESS_WRITE;

  HRESULT hr = device()->CreateTexture2D(&desc, nullptr, getter_AddRefs(mTexture));
  if (FAILED(hr)) {
    NS_WARNING("Failed to create texture for CanvasLayer!");
    return;
  }

  device()->CreateShaderResourceView(mTexture, nullptr, getter_AddRefs(mUploadSRView));
}

void
CanvasLayerD3D10::UpdateSurface()
{
  if (!IsDirty())
    return;
  Painted();

  if (mDrawTarget) {
    mDrawTarget->Flush();
  } else if (mIsD2DTexture) {
    mSurface->Flush();
    return;
  }

  if (mTextureInteropNVpr) {
    if (mTextureInteropNVpr->UpdateFrom(mDrawTarget)) {
      // all done
      return;
    }

    // need to try recreating
    mTextureInteropNVpr = DXTextureInteropNVpr::GetForDrawTarget(device(), mDrawTarget);
    if (mTextureInteropNVpr) {
      mTextureInteropNVpr->GetD3DShaderResourceView()->QueryInterface(__uuidof(ID3D10ShaderResourceView),
                                                                      getter_AddRefs(mSRView));
      return; // all done
    }

    // otherwise, we're doomed
    mSRView = nullptr;
    return;
  }

  if (mGLContext) {
    SharedSurface* surf = mGLContext->RequestFrame();
    if (!surf)
        return;

    switch (surf->Type()) {
      case SharedSurfaceType::EGLSurfaceANGLE: {
        SharedSurface_ANGLEShareHandle* shareSurf = SharedSurface_ANGLEShareHandle::Cast(surf);

        mSRView = shareSurf->GetSRV();
        return;
      }
      case SharedSurfaceType::DXGLInterop2: {
        printf_stderr("D3D10.UpdateSurface from DXGLInterop2.\n");
        SharedSurface_D3D10Interop* shareSurf = SharedSurface_D3D10Interop::Cast(surf);

        mSRView = shareSurf->GetSRV();
        return;
      }
      case SharedSurfaceType::Basic: {
        SharedSurface_Basic* shareSurf = SharedSurface_Basic::Cast(surf);
        // WebGL reads entire surface.
        D3D10_MAPPED_TEXTURE2D map;

        HRESULT hr = mTexture->Map(0, D3D10_MAP_WRITE_DISCARD, 0, &map);

        if (FAILED(hr)) {
          NS_WARNING("Failed to map CanvasLayer texture.");
          return;
        }

        gfxImageSurface* frameData = shareSurf->GetData();
        // Scope for gfxContext, so it's destroyed before Unmap.
        {
          nsRefPtr<gfxImageSurface> mapSurf = 
              new gfxImageSurface((uint8_t*)map.pData,
                                  shareSurf->Size(),
                                  map.RowPitch,
                                  gfxImageFormatARGB32);

          nsRefPtr<gfxContext> ctx = new gfxContext(mapSurf);
          ctx->SetOperator(gfxContext::OPERATOR_SOURCE);
          ctx->SetSource(frameData);
          ctx->Paint();

          mapSurf->Flush();
        }

        mTexture->Unmap(0);
        mSRView = mUploadSRView;
        break;
      }

      default:
        MOZ_CRASH("Unhandled SharedSurfaceType.");
    }
  } else if (mSurface || mDrawTarget) {
    RECT r;
    r.left = 0;
    r.top = 0;
    r.right = mBounds.width;
    r.bottom = mBounds.height;

    D3D10_MAPPED_TEXTURE2D map;
    HRESULT hr = mTexture->Map(0, D3D10_MAP_WRITE_DISCARD, 0, &map);

    if (FAILED(hr)) {
      NS_WARNING("Failed to lock CanvasLayer texture.");
      return;
    }

    nsRefPtr<gfxASurface> srcSurface = mSurface;
    if (!srcSurface) {
      srcSurface = gfxPlatform::GetPlatform()->GetThebesSurfaceForDrawTarget(mDrawTarget);
    }

    nsRefPtr<gfxImageSurface> dstSurface;
    dstSurface = new gfxImageSurface((unsigned char*)map.pData,
                                     gfxIntSize(mBounds.width, mBounds.height),
                                     map.RowPitch,
                                     gfxImageFormatARGB32);
    nsRefPtr<gfxContext> ctx = new gfxContext(dstSurface);
    ctx->SetOperator(gfxContext::OPERATOR_SOURCE);
    ctx->SetSource(srcSurface);
    ctx->Paint();
    
    mTexture->Unmap(0);
    mSRView = mUploadSRView;
  }
}

Layer*
CanvasLayerD3D10::GetLayer()
{
  return this;
}

void
CanvasLayerD3D10::RenderLayer()
{
  FirePreTransactionCallback();
  UpdateSurface();
  FireDidTransactionCallback();

  if (!mSRView)
    return;

  nsIntRect visibleRect = mVisibleRegion.GetBounds();

  SetEffectTransformAndOpacity();

  uint8_t shaderFlags = 0;
  shaderFlags |= LoadMaskTexture();
  shaderFlags |= mDataIsPremultiplied
                ? SHADER_PREMUL : SHADER_NON_PREMUL | SHADER_RGBA;
  shaderFlags |= mHasAlpha ? SHADER_RGBA : SHADER_RGB;
  shaderFlags |= mFilter == GraphicsFilter::FILTER_NEAREST
                ? SHADER_POINT : SHADER_LINEAR;
  ID3D10EffectTechnique* technique = SelectShader(shaderFlags);

  effect()->GetVariableByName("tRGB")->AsShaderResource()->SetResource(mSRView);

  effect()->GetVariableByName("vLayerQuad")->AsVector()->SetFloatVector(
    ShaderConstantRectD3D10(
      (float)mBounds.x,
      (float)mBounds.y,
      (float)mBounds.width,
      (float)mBounds.height)
    );

  if (mNeedsYFlip) {
    effect()->GetVariableByName("vTextureCoords")->AsVector()->SetFloatVector(
      ShaderConstantRectD3D10(
        0,
        1.0f,
        1.0f,
        -1.0f)
      );
  }

  technique->GetPassByIndex(0)->Apply(0);
  device()->Draw(4, 0);

  if (mNeedsYFlip) {
    effect()->GetVariableByName("vTextureCoords")->AsVector()->
      SetFloatVector(ShaderConstantRectD3D10(0, 0, 1.0f, 1.0f));
  }
}

} /* namespace layers */
} /* namespace mozilla */
