/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ScaledFontBase.h"
#include "ScaledFontNVpr.h"
#include "PathBuilderNVpr.h"

#include <map>
#include <sstream>
#include <fstream>

#define MAX_UNICODE_INDEX 0x10ffff

using namespace mozilla::gfx::nvpr;
using namespace std;

namespace mozilla {
namespace gfx {

bool operator <(const FontOptions& aLeft, const FontOptions& aRight)
{
  if (aLeft.mStyle != aRight.mStyle) {
    return aLeft.mStyle < aRight.mStyle;
  }
  return aLeft.mName < aRight.mName;
}

static map<FontOptions, RefPtr<FontNVpr> > fontCache;

TemporaryRef<ScaledFontNVpr>
ScaledFontNVpr::Create(const FontOptions* aFont, GLfloat aSize, ScaledFontBase* aForeignFont)
{
  RefPtr<FontNVpr>& font = fontCache[*aFont];
  if (!font) {
    font = FontNVpr::Create(aFont, aForeignFont);
  }

  return new ScaledFontNVpr(font, aSize);
}

TemporaryRef<ScaledFontNVpr> 
ScaledFontNVpr::Create(const uint8_t* aData, uint32_t aFileSize,
                       uint32_t aIndex, GLfloat aSize)
{
  ostringstream tempFontFile;
#ifdef WIN32
  // XXX - Terrible hacking time! We need a way to get this into a file for
  // the API to be able to accept this.
  CHAR buf [MAX_PATH];
  ::GetTempPathA(MAX_PATH, buf);
  tempFontFile << buf << "nvpr-font-" << rand() << rand() << rand() << rand();
#else
  tempFontFile << "/tmp/nvpr-font-" << rand() << rand() << rand() << rand();
#endif

  ofstream outputFile(tempFontFile.str(), ofstream::binary);
  outputFile.write((const char*)aData, aFileSize);

  FontOptions fontOptions;
  fontOptions.mName = tempFontFile.str();
  fontOptions.mStyle = FONT_STYLE_NORMAL;

  return Create(&fontOptions, aSize);
}

FontNVpr::FontNVpr(const FontOptions* aFont, ScaledFontBase* aForeignFont)
  : mGlyphPaths(0), mNumGlyphs(0)
{
  if (aForeignFont) {
    InitFromForeignFont(aForeignFont);
  } else {
    gl->MakeCurrent();

    GLenum fontStyle;
    switch (aFont->mStyle) {
    default:
      MOZ_ASSERT(!"Invalid font style");
    case FONT_STYLE_NORMAL:
      fontStyle = 0;
      break;
    case FONT_STYLE_ITALIC:
      fontStyle = GL_ITALIC_BIT_NV;
      break;
    case FONT_STYLE_BOLD:
      fontStyle = GL_BOLD_BIT_NV;
      break;
    case FONT_STYLE_BOLD_ITALIC:
      fontStyle = GL_BOLD_BIT_NV | GL_ITALIC_BIT_NV;
      break;
    }

    // XXX templatePath is leaked
    GLuint templatePath = gl->GenPathsNV(1);
    gl->PathCommandsNV(templatePath, 0, nullptr, 0, GL_FLOAT, nullptr);

    // XXX all the glyph paths are leaked; destructor doesn't do anything!
    // (Destructor won't be called until late anyway, we keep everything in
    // a global static cache. This should be fixed at some point.)
    mGlyphPaths = gl->GenPathsNV(MAX_UNICODE_INDEX + 1);
    mNumGlyphs = MAX_UNICODE_INDEX + 1;

    gl->PathGlyphRangeNV(mGlyphPaths, GL_FILE_NAME_NV, aFont->mName.c_str(),
                         fontStyle, 0, mNumGlyphs,
                         GL_SKIP_MISSING_GLYPH_NV, templatePath, 1);
  }

  // XXX the driver has a bug where it currently returns -1.0f for all values
  // also seems to crash if mNumGlyphs > 1 (or maybe if a path doesn't have any
  // commands)
  struct {GLfloat x1, y1, x2, y2;} bounds;
#if 0
  gl->GetPathMetricRangeNV(GL_FONT_X_MIN_BOUNDS_BIT_NV | GL_FONT_Y_MIN_BOUNDS_BIT_NV
                           | GL_FONT_X_MAX_BOUNDS_BIT_NV | GL_FONT_Y_MAX_BOUNDS_BIT_NV,
                           mGlyphPaths, mNumGlyphs, 0, &bounds.x1);

  if (bounds.x1 == -1.0f &&
      bounds.y1 == -1.0f &&
      bounds.x2 == -1.0f &&
      bounds.y2 == -1.0f)
  {
    bounds.x1 = -.25;
    bounds.y1 = -1.25;
    bounds.x2 = 1;
    bounds.y2 = .5;
  }
#else
  bounds.x1 = -.25;
  bounds.y1 = -1.25;
  bounds.x2 = 1;
  bounds.y2 = .5;
#endif

  mGlyphsBoundingBox = Rect(bounds.x1, bounds.y1,
                            bounds.x2 - bounds.x1, bounds.y2 - bounds.y1);

}

FontNVpr::~FontNVpr()
{
}

bool
FontNVpr::InitFromForeignFont(ScaledFontBase* aForeignFont)
{
  mNumGlyphs = aForeignFont->GetGlyphCount();
  if (mNumGlyphs == 0)
    return false;

  gl->MakeCurrent();

  mGlyphPaths = gl->GenPathsNV(mNumGlyphs);

  RefPtr<PathBuilderSequenceNVpr> nvbuilder = new PathBuilderSequenceNVpr(FILL_WINDING, mGlyphPaths, mNumGlyphs);
  for (uint32_t i = 0; i < mNumGlyphs; ++i) {
    aForeignFont->CopyGlyphToBuilder(i, 1.0f, nvbuilder);
    nvbuilder->NextPath();
  }

  return true;
}

ScaledFontNVpr::ScaledFontNVpr(TemporaryRef<FontNVpr> aFont, GLfloat aSize)
  : mFont(aFont)
  , mSize(aSize)
  , mInverseSize(1 / aSize)
{
  mGlyphsBoundingBox = mFont->GlyphsBoundingBox();
  mGlyphsBoundingBox.Scale(aSize, aSize);
}

}
}
