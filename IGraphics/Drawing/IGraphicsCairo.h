#pragma once

#include "IPlugPlatform.h"

#ifdef OS_MAC
  #include "cairo/cairo.h"
  #define __QUICKDRAW__
  #define __HISERVICES__
  #include "cairo/cairo-quartz.h"
#elif defined OS_WIN
  #define CAIRO_WIN32_STATIC_BUILD

  #pragma comment(lib, "cairo.lib")
  #pragma comment(lib, "pixman.lib")
  #pragma comment(lib, "freetype.lib")
  #pragma comment(lib, "libpng.lib")
  #pragma comment(lib, "zlib.lib")

  #include "cairo/src/cairo.h"
  #include "cairo/src/cairo-win32.h"
#else
  #error NOT IMPLEMENTED
#endif

#ifdef IGRAPHICS_FREETYPE
#include "ft2build.h"
#include FT_FREETYPE_H
#include "cairo/cairo-ft.h"
#include "hb.h"
#include "hb-ft.h"
#endif

#include "IGraphicsPathBase.h"

class CairoBitmap : public APIBitmap
{
public:
  CairoBitmap(cairo_surface_t* pSurface, int scale);
  virtual ~CairoBitmap();
};

/** IGraphics draw class using Cairo
*   @ingroup DrawClasses
*/
class IGraphicsCairo : public IGraphicsPathBase
{
public:
  const char* GetDrawingAPIStr() override { return "CAIRO"; }

  IGraphicsCairo(IGEditorDelegate& dlg, int w, int h, int fps, float scale);
  ~IGraphicsCairo();

  void DrawBitmap(IBitmap& bitmap, const IRECT& dest, int srcX, int srcY, const IBlend* pBlend) override;
      
  void PathClear() override { cairo_new_path(mContext); }
  void PathClose() override { cairo_close_path(mContext); }

  void PathArc(float cx, float cy, float r, float aMin, float aMax) override { cairo_arc(mContext, cx, cy, r, DegToRad(aMin - 90.f), DegToRad(aMax - 90.f)); }
    
  void PathMoveTo(float x, float y) override { cairo_move_to(mContext, x, y); }
  void PathLineTo(float x, float y) override { cairo_line_to(mContext, x, y); }
  void PathCurveTo(float x1, float y1, float x2, float y2, float x3, float y3) override { cairo_curve_to(mContext, x1, y1, x2, y2, x3, y3); }

  void PathStroke(const IPattern& pattern, float thickness, const IStrokeOptions& options, const IBlend* pBlend) override;
  void PathFill(const IPattern& pattern, const IFillOptions& options, const IBlend* pBlend) override;
  
  IColor GetPoint(int x, int y) override;
  void* GetDrawContext() override { return (void*) mContext; }

  bool DrawText(const IText& text, const char* str, IRECT& bounds, const IBlend* pBlend, bool measure) override;
  bool MeasureText(const IText& text, const char* str, IRECT& bounds) override;

  void EndFrame() override;

  void SetPlatformContext(void* pContext) override;

  void DrawResize() override
  {
    SetPlatformContext(nullptr);
#ifdef OS_WIN
    HWND window = static_cast<HWND>(GetWindow());
    if (window)
    {
      HDC dc = GetDC(window);
      SetPlatformContext(dc);
      ReleaseDC(window, dc);
    }
#endif
  }

  void LoadFont(const char* fileName) override;
protected:

  APIBitmap* LoadAPIBitmap(const WDL_String& resourcePath, int scale) override;
  APIBitmap* ScaleAPIBitmap(const APIBitmap* pBitmap, int scale) override;

  void SetCairoSourcePattern(const IPattern& pattern, const IBlend* pBlend);
  
private:
  
  void PathTransformSetMatrix(const IMatrix& m) override;
  void SetClipRegion(const IRECT& r) override;
  
  cairo_t* mContext;
  cairo_surface_t* mSurface;
  
#if defined IGRAPHICS_FREETYPE
  FT_Library mFTLibrary = nullptr;
  WDL_PtrList<FT_FaceRec_> mFTFaces;
  WDL_PtrList<cairo_font_face_t> mCairoFTFaces;
#endif
};
