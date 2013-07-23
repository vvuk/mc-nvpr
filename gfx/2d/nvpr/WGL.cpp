/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WGL.h"

#include "Logging.h"
#include <Windows.h>
#include "GL/wglext.h"
#include <iterator>
#include <iostream>
#include <sstream>
#include <string>

using namespace std;

namespace mozilla {
namespace gfx {
namespace nvpr {

void
InitializeGLIfNeeded()
{
  if (gl) {
    return;
  }
  gl = new WGL();
}

template<typename T> bool
WGL::LoadProcAddress(T WGL::*aProc, const char* aName)
{
  PROC proc = GetProcAddress(aName);
  if (!proc) {
    // The GL 1.1 functions have to be loaded directly from the dll.
    proc = ::GetProcAddress(mGLLibrary, aName);
  }
  if (!proc) {
    gfxWarning() << "Failed to load GL function " << aName << ".";
    this->*aProc = nullptr;
    return false;
  }
  this->*aProc = reinterpret_cast<T>(proc);
  return true;
}

WGL::WGL()
{
  memset(mSupportedWGLExtensions, 0, sizeof(mSupportedWGLExtensions));

  mGLLibrary = ::LoadLibrary("opengl32.dll");
  if (!mGLLibrary) {
    return;
  }

#define LOAD_WGL_METHOD(NAME) \
  NAME = reinterpret_cast<decltype(NAME)>(::GetProcAddress(mGLLibrary, "wgl"#NAME)); \
  if (!NAME) { \
    gfxWarning() << "Failed to find WGL function " #NAME "."; \
    return; \
  }

  FOR_ALL_WGL_ENTRY_POINTS(LOAD_WGL_METHOD);

#undef LOAD_WGL_METHOD
  
  HINSTANCE inst = (HINSTANCE)GetModuleHandle(NULL);

  ATOM wclass = 0;
  HWND hwnd = 0;

  WNDCLASS wc;
  memset(&wc, 0, sizeof(wc));
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
  wc.hInstance = inst;
  wc.lpfnWndProc = (WNDPROC) DefWindowProc;
  wc.lpszClassName = TEXT("DummyWindow");
  wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;

  wclass = RegisterClass(&wc);
  if (!wclass) {
    return;
  }

  if (!(hwnd = CreateWindow(TEXT("DummyWindow"),
                            TEXT("Dummy OGL Window"),
                            WS_OVERLAPPEDWINDOW,
                            0, 0, 1, 1,
                            NULL, NULL,
                            inst, NULL))) {
    return;
  }

  mDC = ::GetDC(hwnd);

  PIXELFORMATDESCRIPTOR pfd;
  
  memset(&pfd, 0, sizeof(pfd));
  pfd.nSize = sizeof(pfd);
  pfd.dwFlags = PFD_SUPPORT_OPENGL;
  pfd.iPixelType = PFD_TYPE_RGBA;
  pfd.cColorBits = 32;
  pfd.cDepthBits = 0;
  pfd.cStencilBits = 0;
  pfd.iLayerType = PFD_MAIN_PLANE;

  // get the best available match of pixel format for the device context   
  int iPixelFormat = ChoosePixelFormat(mDC, &pfd); 
 
  // make that the pixel format of the device context  
  SetPixelFormat(mDC, iPixelFormat, &pfd);

  mGLContext = CreateContext(mDC);

  DWORD lastError = ::GetLastError();

  MakeCurrent(mDC, mGLContext);

  if (!LoadProcAddress(&WGL::GetExtensionsStringARB, "wglGetExtensionsStringARB")) {
    return;
  }

  stringstream extensions(GetExtensionsStringARB(mDC));
  istream_iterator<string> iter(extensions);
  istream_iterator<string> end;

  for (; iter != end; iter++) {
    const string& extension = *iter;

    if (*iter == "WGL_NV_copy_image") {
      if (LoadProcAddress(&WGL::CopyImageSubDataNV, "wglCopyImageSubDataNV")) {
        mSupportedWGLExtensions[NV_copy_image] = true;
      }
      continue;
    }

    if (*iter == "WGL_NV_DX_interop2") {
      if (LoadProcAddress(&WGL::DXOpenDeviceNV, "wglDXOpenDeviceNV")
          && LoadProcAddress(&WGL::DXCloseDeviceNV, "wglDXCloseDeviceNV")
          && LoadProcAddress(&WGL::DXRegisterObjectNV, "wglDXRegisterObjectNV")
          && LoadProcAddress(&WGL::DXUnregisterObjectNV, "wglDXUnegisterObjectNV")
          && LoadProcAddress(&WGL::DXLockObjectsNV, "wglDXLockObjectsNV")
          && LoadProcAddress(&WGL::DXUnlockObjectsNV, "wglDXUnlockObjectsNV")) {
        mSupportedWGLExtensions[NV_DX_interop2] = true;
      }
      continue;
    }
  }

#define LOAD_GL_METHOD(NAME) \
  if (!LoadProcAddress(&WGL::NAME, "gl"#NAME)) { \
    return; \
  }

  FOR_ALL_PUBLIC_GL_ENTRY_POINTS(LOAD_GL_METHOD);
  FOR_ALL_PRIVATE_GL_ENTRY_POINTS(LOAD_GL_METHOD);

#undef LOAD_GL_METHOD

  Initialize();
}

WGL::~WGL()
{
  MakeCurrent(mDC, nullptr);

  if (mGLContext) {
    DeleteContext(mGLContext);
  }
}

bool GL::IsCurrent() const
{
  const WGL* wgl = static_cast<const WGL*>(this);
  return wgl->GetCurrentContext() == wgl->GLContext();
}

void GL::MakeCurrent() const
{
  const WGL* wgl = static_cast<const WGL*>(this);

  if (wgl->IsCurrent()) {
    return;
  }

  wgl->MakeCurrent(wgl->DC(), wgl->GLContext());
}

bool
GL::BlitTextureToForeignTexture(const IntSize& aSize, GLuint aSourceTextureId,
                                PlatformGLContext aForeignContext,
                                GLuint aForeignTextureId)
{
  WGL* wgl = static_cast<WGL*>(this);

  if (!wgl->HasWGLExtension(WGL::NV_copy_image)) {
    return false;
  }

  wgl->CopyImageSubDataNV(wgl->GLContext(), aSourceTextureId, GL_TEXTURE_2D, 0, 0, 0, 0,
                          static_cast<HGLRC>(aForeignContext), aForeignTextureId,
                          GL_TEXTURE_2D, 0, 0, 0, 0, aSize.width, aSize.height, 1);

  return true;
}

bool
GL::BlitFramebufferToDXTexture(const IntSize& aSize, void* aDX, void* aDXTexture)
{
  MOZ_ASSERT(IsCurrent());

  WGL* wgl = static_cast<WGL*>(this);

  if (!wgl->HasWGLExtension(WGL::NV_DX_interop2)) {
    return false;
  }

  HANDLE interopHandle = wgl->DXOpenDeviceNV(aDX);
  if (!interopHandle) {
    return false;
  }

  GLuint textureId;
  GenTextures(1, &textureId);
  HANDLE textureHandle = wgl->DXRegisterObjectNV(interopHandle,
                                                aDXTexture, textureId,
                                                GL_TEXTURE_2D,
                                                WGL_ACCESS_WRITE_DISCARD_NV);

  wgl->DXLockObjectsNV(interopHandle, 1, &textureHandle);

  SetFramebufferToTexture(GL_DRAW_FRAMEBUFFER, GL_TEXTURE_2D, textureId);
  BlitFramebuffer(0, 0, aSize.width, aSize.height,
                  0, 0, aSize.width, aSize.height,
                  GL_COLOR_BUFFER_BIT, GL_NEAREST);

  wgl->DXUnlockObjectsNV(interopHandle, 1, &textureHandle);

  DeleteTexture(textureId);
  wgl->DXUnregisterObjectNV(interopHandle, textureHandle);
  wgl->DXCloseDeviceNV(interopHandle);

  return true;
}

}
}
}
