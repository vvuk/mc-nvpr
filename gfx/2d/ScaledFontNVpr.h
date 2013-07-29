/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_SCALEDFONTNVPR_H_
#define MOZILLA_GFX_SCALEDFONTNVPR_H_

#include "2D.h"
#include "nvpr/GL.h"
#include <mozilla/RefPtr.h>

namespace mozilla {
namespace gfx {

class FontCacheNVpr;
class FontNVpr;
class ScaledFontBase;

class FontNVpr : public RefCounted<FontNVpr> {
public:
  static TemporaryRef<FontNVpr> Create(const FontOptions* aFont, ScaledFontBase* aForeignFont = nullptr)
  {
    return new FontNVpr(aFont, aForeignFont);
  }

  ~FontNVpr();

  const Rect& GlyphsBoundingBox() const { return mGlyphsBoundingBox; }
  operator GLuint() const { return mGlyphPaths; }

private:
  FontNVpr(const FontOptions* aFont, ScaledFontBase* aForeignFont);

  bool InitFromForeignFont(ScaledFontBase* aForeignFont);
  void InitFromSystemFont(const FontOptions* aFont);

  GLuint mGlyphPaths;
  uint32_t mNumGlyphs;
  Rect mGlyphsBoundingBox;
  RefPtr<ScaledFontBase> mBaseFont;
};


class ScaledFontNVpr : public ScaledFont {
public:
  static TemporaryRef<ScaledFontNVpr> Create(const FontOptions* aFont, GLfloat aSize,
                                             ScaledFontBase* aForeignFont = nullptr);

  static TemporaryRef<ScaledFontNVpr> Create(const uint8_t* aData, uint32_t aFileSize,
                                             uint32_t aIndex, GLfloat aSize);

  GLfloat Size() const { return mSize; }
  GLfloat InverseSize() const { return mInverseSize; }
  const Rect& GlyphsBoundingBox() const { return mGlyphsBoundingBox; }
  operator GLuint() const { return *mFont; }

  virtual FontType GetType() const { return FONT_NVPR; }

  virtual TemporaryRef<Path> GetPathForGlyphs(const GlyphBuffer &aBuffer,
                                              const DrawTarget *aTarget)
  {
    return nullptr;
  }

  virtual void CopyGlyphsToBuilder(const GlyphBuffer &aBuffer,
                                   PathBuilder *aBuilder)
  {}

private:
  ScaledFontNVpr(TemporaryRef<FontNVpr> aFont, GLfloat aSize);

  const RefPtr<FontNVpr> mFont;
  const GLfloat mSize;
  const GLfloat mInverseSize;
  Rect mGlyphsBoundingBox;
};

}
}

#endif /* MOZILLA_GFX_SCALEDFONTNVPR_H_ */
