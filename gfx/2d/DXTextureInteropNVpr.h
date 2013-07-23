/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_DXTEXTUREINTEROP_H_
#define MOZILLA_GFX_DXTEXTUREINTEROP_H_

#include "Types.h"
#include <mozilla/RefPtr.h>

#include <windows.h>
#include <unknwn.h>

typedef unsigned int GLuint;

namespace mozilla {
namespace gfx {

class DrawTarget;

class GFX2D_API DXTextureInteropNVpr : public RefCounted<DXTextureInteropNVpr> {
public:
  // Creates or updates as needed.  Note that this assumes that there will only
  // be one D3D device in use!
  static RefPtr<DXTextureInteropNVpr> GetForDrawTarget(void* aDX, DrawTarget* aForDrawTarget);

  ~DXTextureInteropNVpr();

  GLuint Lock();
  void Unlock();

  const IntSize& GetSize() { return mSize; }
  IUnknown* GetD3DTexture() { return mTextureD3D; }
  IUnknown* GetD3DShaderResourceView() { return mShaderResourceViewD3D; }

  bool UpdateFrom(DrawTarget* aDT);
private:
  friend class DrawTargetNVpr;

  DXTextureInteropNVpr(void* aDX, const IntSize& aSize, bool& aSuccess);

  HANDLE mDXInterop;
  HANDLE mTextureInterop;
  GLuint mTextureId;
  IntSize mSize;
  RefPtr<IUnknown> mTextureD3D;
  RefPtr<IUnknown> mShaderResourceViewD3D;
};

}
}

#endif /* MOZILLA_GFX_DXTEXTUREINTEROP_H_ */
