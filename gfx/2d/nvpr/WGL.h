/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_NVPR_WGL_H_
#define MOZILLA_GFX_NVPR_WGL_H_

#include "GL.h"
#include "GL/wglext.h"

#include <windows.h>
// for IUnknown
#include <unknwn.h>

namespace mozilla {
namespace gfx {
namespace nvpr {

class WGL : public GL
{
public:
  WGL();
  virtual ~WGL();

  HMODULE GLLibrary() const { return mGLLibrary; }
  HDC DC() const { return mDC; }
  HGLRC GLContext() const{ return mGLContext; }

  enum WGLExtension {
    NV_copy_image,
    NV_DX_interop2,
    WGL_EXTENSION_COUNT
  };
  bool HasWGLExtension(WGLExtension aExtension) const
  {
    return mSupportedWGLExtensions[aExtension];
  }

#define FOR_ALL_WGL_ENTRY_POINTS(MACRO) \
  MACRO(CreateContext) \
  MACRO(MakeCurrent) \
  MACRO(GetProcAddress) \
  MACRO(DeleteContext) \
  MACRO(GetCurrentContext)

#define FOR_ALL_WGL_EXTENSIONS(MACRO) \
  MACRO(GetExtensionsStringARB) \
  MACRO(CopyImageSubDataNV) \
  MACRO(DXOpenDeviceNV) \
  MACRO(DXCloseDeviceNV) \
  MACRO(DXRegisterObjectNV) \
  MACRO(DXSetResourceShareHandleNV) \
  MACRO(DXUnregisterObjectNV) \
  MACRO(DXLockObjectsNV) \
  MACRO(DXUnlockObjectsNV)

#define DECLARE_WGL_METHOD(NAME) \
  decltype(&wgl##NAME) NAME;

  FOR_ALL_WGL_ENTRY_POINTS(DECLARE_WGL_METHOD);
  FOR_ALL_WGL_EXTENSIONS(DECLARE_WGL_METHOD);

#undef DECLARE_WGL_METHOD

private:
  template<typename T> bool LoadProcAddress(T WGL::*aProc, const char* aName);

  HMODULE mGLLibrary;
  HDC mDC;
  HGLRC mGLContext;
  bool mSupportedWGLExtensions[WGL_EXTENSION_COUNT];
};

}
}
}

#endif /* MOZILLA_GFX_NVPR_WGL_H_ */
