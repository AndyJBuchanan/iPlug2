#include <cmath>

#include "png.h"

#include "IGraphicsCairo.h"

#ifdef OS_MAC
cairo_surface_t* LoadPNGResource(void*, const WDL_String& path)
{
  return cairo_image_surface_create_from_png(path.Get());
}
#elif defined OS_WIN
class PNGStreamReader
{
public:
  PNGStreamReader(HMODULE hInst, const WDL_String &path)
  : mData(nullptr), mSize(0), mCount(0)
  {
    HRSRC resInfo = FindResource(hInst, path.Get(), "PNG");
    if (resInfo)
    {
      HGLOBAL res = LoadResource(hInst, resInfo);
      if (res)
      {
        mData = (uint8_t *) LockResource(res);
        mSize = SizeofResource(hInst, resInfo);
      }
    }
  }

  cairo_status_t Read(uint8_t* data, uint32_t length)
  {
    mCount += length;
    if (mCount <= mSize)
    {
      memcpy(data, mData + mCount - length, length);
      return CAIRO_STATUS_SUCCESS;
    }

    return CAIRO_STATUS_READ_ERROR;
  }

  static cairo_status_t StaticRead(void *reader, uint8_t *data, uint32_t length)
  {
    return ((PNGStreamReader*)reader)->Read(data, length);
  }
  
private:
  const uint8_t* mData;
  size_t mCount;
  size_t mSize;
};

cairo_surface_t* LoadPNGResource(void* hInst, const WDL_String& path)
{
  PNGStreamReader reader((HMODULE) hInst, path);
  return cairo_image_surface_create_from_png_stream(&PNGStreamReader::StaticRead, &reader);
}
#else
  #error NOT IMPLEMENTED
#endif

CairoBitmap::CairoBitmap(cairo_surface_t* pSurface, int scale)
{
  cairo_surface_set_device_scale(pSurface, scale, scale);
  int width = cairo_image_surface_get_width(pSurface);
  int height = cairo_image_surface_get_height(pSurface);
  
  SetBitmap(pSurface, width, height, scale);
}
  
CairoBitmap::~CairoBitmap()
{
  cairo_surface_destroy((cairo_surface_t*) GetBitmap());
}

#pragma mark -

inline cairo_operator_t CairoBlendMode(const IBlend* pBlend)
{
  if (!pBlend)
  {
    return CAIRO_OPERATOR_OVER;
  }
  switch (pBlend->mMethod)
  {
    case kBlendClobber: return CAIRO_OPERATOR_OVER;
    case kBlendAdd: return CAIRO_OPERATOR_ADD;
    case kBlendColorDodge: return CAIRO_OPERATOR_COLOR_DODGE;
    case kBlendNone:
    default:
      return CAIRO_OPERATOR_OVER; // TODO: is this correct - same as clobber?
  }
}

#pragma mark -

IGraphicsCairo::IGraphicsCairo(IGEditorDelegate& dlg, int w, int h, int fps, float scale)
: IGraphicsPathBase(dlg, w, h, fps, scale)
, mSurface(nullptr)
, mContext(nullptr)
{
  DBGMSG("IGraphics Cairo @ %i FPS\n", fps);
}

IGraphicsCairo::~IGraphicsCairo() 
{
#if defined IGRAPHICS_FREETYPE
  if (mFTLibrary != nullptr)
  {
    for (auto i = 0; i < mCairoFTFaces.GetSize(); i++) {
      cairo_font_face_destroy(mCairoFTFaces.Get(i));
    }
    
    FT_Done_FreeType(mFTLibrary); // will do FT_Done_Face
  }
#endif
  
  if (mContext)
    cairo_destroy(mContext);
  
  if (mSurface)
    cairo_surface_destroy(mSurface);
}

APIBitmap* IGraphicsCairo::LoadAPIBitmap(const WDL_String& resourcePath, int scale)
{
  cairo_surface_t* pSurface = LoadPNGResource(GetPlatformInstance(), resourcePath);
    
  assert(cairo_surface_status(pSurface) == CAIRO_STATUS_SUCCESS); // Protect against typos in resource.h and .rc files.

  return new CairoBitmap(pSurface, scale);
}

APIBitmap* IGraphicsCairo::ScaleAPIBitmap(const APIBitmap* pBitmap, int scale)
{
  cairo_surface_t* pInSurface = (cairo_surface_t*) pBitmap->GetBitmap();
  
  int destW = (pBitmap->GetWidth() / pBitmap->GetScale()) * scale;
  int destH = (pBitmap->GetHeight() / pBitmap->GetScale()) * scale;
    
  // Create resources to redraw
    
  cairo_surface_t* pOutSurface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, destW, destH);
  cairo_t* pOutContext = cairo_create(pOutSurface);
    
  // Scale and paint (destroying the context / the surface is retained)
    
  cairo_scale(pOutContext, scale, scale);
  cairo_set_source_surface(pOutContext, pInSurface, 0, 0);
  cairo_paint(pOutContext);
  cairo_destroy(pOutContext);
    
  return new CairoBitmap(pOutSurface, scale);
}

void IGraphicsCairo::DrawBitmap(IBitmap& bitmap, const IRECT& dest, int srcX, int srcY, const IBlend* pBlend)
{
  cairo_save(mContext);
  cairo_rectangle(mContext, dest.L, dest.T, dest.W(), dest.H());
  cairo_clip(mContext);
  cairo_surface_t* surface = (cairo_surface_t*) bitmap.GetAPIBitmap()->GetBitmap();
  cairo_set_source_surface(mContext, surface, std::round(dest.L) - srcX, (int) std::round(dest.T) - srcY);
  cairo_set_operator(mContext, CairoBlendMode(pBlend));
  cairo_paint_with_alpha(mContext, BlendWeight(pBlend));
  cairo_restore(mContext);
}

void IGraphicsCairo::PathStroke(const IPattern& pattern, float thickness, const IStrokeOptions& options, const IBlend* pBlend)
{
  double dashArray[8];
  
  // First set options
  
  switch (options.mCapOption)
  {
    case kCapButt:   cairo_set_line_cap(mContext, CAIRO_LINE_CAP_BUTT);     break;
    case kCapRound:  cairo_set_line_cap(mContext, CAIRO_LINE_CAP_ROUND);    break;
    case kCapSquare: cairo_set_line_cap(mContext, CAIRO_LINE_CAP_SQUARE);   break;
  }
  
  switch (options.mJoinOption)
  {
    case kJoinMiter:   cairo_set_line_join(mContext, CAIRO_LINE_JOIN_MITER);   break;
    case kJoinRound:   cairo_set_line_join(mContext, CAIRO_LINE_JOIN_ROUND);   break;
    case kJoinBevel:   cairo_set_line_join(mContext, CAIRO_LINE_JOIN_BEVEL);   break;
  }
  
  cairo_set_miter_limit(mContext, options.mMiterLimit);
  
  for (int i = 0; i < options.mDash.GetCount(); i++)
    dashArray[i] = *(options.mDash.GetArray() + i);
  
  cairo_set_dash(mContext, dashArray, options.mDash.GetCount(), options.mDash.GetOffset());
  cairo_set_line_width(mContext, thickness);

  SetCairoSourcePattern(pattern, pBlend);
  if (options.mPreserve)
    cairo_stroke_preserve(mContext);
  else
    cairo_stroke(mContext);
}

void IGraphicsCairo::PathFill(const IPattern& pattern, const IFillOptions& options, const IBlend* pBlend) 
{
  cairo_set_fill_rule(mContext, options.mFillRule == kFillEvenOdd ? CAIRO_FILL_RULE_EVEN_ODD : CAIRO_FILL_RULE_WINDING);
  SetCairoSourcePattern(pattern, pBlend);
  if (options.mPreserve)
    cairo_fill_preserve(mContext);
  else
    cairo_fill(mContext);
}

void IGraphicsCairo::SetCairoSourcePattern(const IPattern& pattern, const IBlend* pBlend)
{
  cairo_set_operator(mContext, CairoBlendMode(pBlend));
  
  switch (pattern.mType)
  {
    case kSolidPattern:
    {
      const IColor &color = pattern.GetStop(0).mColor;
      cairo_set_source_rgba(mContext, color.R / 255.0, color.G / 255.0, color.B / 255.0, (BlendWeight(pBlend) * color.A) / 255.0);
    }
    break;
      
    case kLinearPattern:
    case kRadialPattern:
    {
      cairo_pattern_t *cairoPattern;
      cairo_matrix_t matrix;
      const float *xform = pattern.mTransform;
      
      if (pattern.mType == kLinearPattern)
        cairoPattern = cairo_pattern_create_linear(0.0, 0.0, 1.0, 0.0);
      else
        cairoPattern = cairo_pattern_create_radial(0.0, 0.0, 0.0, 0.0, 0.0, 1.0);
      
      switch (pattern.mExtend)
      {
        case kExtendNone:      cairo_pattern_set_extend(cairoPattern, CAIRO_EXTEND_NONE);      break;
        case kExtendPad:       cairo_pattern_set_extend(cairoPattern, CAIRO_EXTEND_PAD);       break;
        case kExtendReflect:   cairo_pattern_set_extend(cairoPattern, CAIRO_EXTEND_REFLECT);   break;
        case kExtendRepeat:    cairo_pattern_set_extend(cairoPattern, CAIRO_EXTEND_REPEAT);    break;
      }
      
      for (int i = 0; i < pattern.NStops(); i++)
      {
        const IColorStop& stop = pattern.GetStop(i);
        cairo_pattern_add_color_stop_rgba(cairoPattern, stop.mOffset, stop.mColor.R / 255.0, stop.mColor.G / 255.0, stop.mColor.B / 255.0, (BlendWeight(pBlend) * stop.mColor.A) / 255.0);
      }
      
      cairo_matrix_init(&matrix, xform[0], xform[1], xform[2], xform[3], xform[4], xform[5]);
      cairo_pattern_set_matrix(cairoPattern, &matrix);
      cairo_set_source(mContext, cairoPattern);
      cairo_pattern_destroy(cairoPattern);
    }
    break;
  }
}

IColor IGraphicsCairo::GetPoint(int x, int y)
{
  // Convert suface to cairo image surface of one pixel (avoid copying the whole surface)
    
  cairo_surface_t* pOutSurface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
  cairo_t* pOutContext = cairo_create(pOutSurface);
  cairo_set_source_surface(pOutContext, mSurface, -x, -y);
  cairo_paint(pOutContext);
  cairo_surface_flush(pOutSurface);

  uint8_t* pData = cairo_image_surface_get_data(pOutSurface);
  uint32_t px = *((uint32_t*)(pData));
  
  cairo_surface_destroy(pOutSurface);
  cairo_destroy(pOutContext);
    
  int A = (px >> 0) & 0xFF;
  int R = (px >> 8) & 0xFF;
  int G = (px >> 16) & 0xFF;
  int B = (px >> 24) & 0xFF;
    
  return IColor(A, R, G, B);
}

#define FONT_SIZE 36
#define MARGIN (FONT_SIZE * .5)

bool IGraphicsCairo::DrawText(const IText& text, const char* str, IRECT& bounds, const IBlend* pBlend, bool measure)
{
#if defined IGRAPHICS_FREETYPE
  FT_Face ft_face;

  FT_New_Face(mFTLibrary, "/Users/oli/Applications/IGraphicsTest.app/Contents/Resources/ProFontWindows.ttf", 0, &ft_face);

  FT_Set_Char_Size(ft_face, FONT_SIZE * 64, FONT_SIZE * 64, 0, 0 );

  /* Create hb-ft font. */
  hb_font_t *hb_font;
  hb_font = hb_ft_font_create (ft_face, NULL);

  /* Create hb-buffer and populate. */
  hb_buffer_t *hb_buffer;
  hb_buffer = hb_buffer_create ();
  hb_buffer_add_utf8 (hb_buffer, str, -1, 0, -1);
  hb_buffer_guess_segment_properties (hb_buffer);

  /* Shape it! */
  hb_shape (hb_font, hb_buffer, NULL, 0);

  /* Get glyph information and positions out of the buffer. */
  unsigned int len = hb_buffer_get_length (hb_buffer);
  hb_glyph_info_t *info = hb_buffer_get_glyph_infos (hb_buffer, NULL);
  hb_glyph_position_t *pos = hb_buffer_get_glyph_positions (hb_buffer, NULL);

  /* Draw, using cairo. */
  double width = 2 * MARGIN;
  double height = 2 * MARGIN;
  for (unsigned int i = 0; i < len; i++)
  {
    width  += pos[i].x_advance / 64.;
    height -= pos[i].y_advance / 64.;
  }
  if (HB_DIRECTION_IS_HORIZONTAL (hb_buffer_get_direction(hb_buffer)))
    height += FONT_SIZE;
  else
    width  += FONT_SIZE;

  cairo_set_source_rgba (mContext, 1., 1., 1., 1.);
  cairo_paint (mContext);
  cairo_set_source_rgba (mContext, 0., 0., 0., 1.);
  cairo_translate (mContext, MARGIN, MARGIN);

  /* Set up cairo font face. */
  cairo_font_face_t *cairo_face;
  cairo_face = cairo_ft_font_face_create_for_ft_face (ft_face, 0);
  cairo_set_font_face (mContext, cairo_face);
  cairo_set_font_size (mContext, FONT_SIZE);

  /* Set up baseline. */
  if (HB_DIRECTION_IS_HORIZONTAL (hb_buffer_get_direction(hb_buffer)))
  {
    cairo_font_extents_t font_extents;
    cairo_font_extents (mContext, &font_extents);
    double baseline = (FONT_SIZE - font_extents.height) * .5 + font_extents.ascent;
    cairo_translate (mContext, 0, baseline);
  }
  else
  {
    cairo_translate (mContext, FONT_SIZE * .5, 0);
  }

  cairo_glyph_t *cairo_glyphs = cairo_glyph_allocate (len);
  double current_x = 0;
  double current_y = 0;

  for (auto i = 0; i < len; i++)
  {
    cairo_glyphs[i].index = info[i].codepoint;
    cairo_glyphs[i].x = current_x + pos[i].x_offset / 64.;
    cairo_glyphs[i].y = -(current_y + pos[i].y_offset / 64.);
    current_x += pos[i].x_advance / 64.;
    current_y += pos[i].y_advance / 64.;
  }
  cairo_show_glyphs (mContext, cairo_glyphs, len);
  cairo_glyph_free (cairo_glyphs);
#else // TOY text
  cairo_set_source_rgba(mContext, text.mFGColor.R / 255.0, text.mFGColor.G / 255.0, text.mFGColor.B / 255.0, (BlendWeight(pBlend) * text.mFGColor.A) / 255.0);
  cairo_select_font_face(mContext, text.mFont, CAIRO_FONT_SLANT_NORMAL, text.mStyle == IText::kStyleBold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(mContext, text.mSize);
//  cairo_font_options_t* font_options = cairo_font_options_create ();
//  cairo_font_options_set_antialias (font_options, CAIRO_ANTIALIAS_BEST);
//  cairo_set_font_options (mContext, font_options);
  cairo_text_extents_t textExtents;
  cairo_font_extents_t fontExtents;
  cairo_font_extents(mContext, &fontExtents);
  cairo_text_extents(mContext, str, &textExtents);
//  cairo_font_options_destroy(font_options);
  
  double x = 0., y = 0.;

  switch (text.mAlign)
  {
    case IText::EAlign::kAlignNear: x = bounds.L; break;
    case IText::EAlign::kAlignFar: x = bounds.R - textExtents.width - textExtents.x_bearing; break;
    case IText::EAlign::kAlignCenter: x = bounds.L + ((bounds.W() - textExtents.width - textExtents.x_bearing) / 2.0); break;
    default: break;
  }
  
  switch (text.mVAlign)
  {
    case IText::EVAlign::kVAlignTop: y = bounds.T + fontExtents.ascent; break;
    case IText::EVAlign::kVAlignMiddle: y = bounds.MH() + (fontExtents.ascent/2.); break;
    case IText::EVAlign::kVAlignBottom: y = bounds.B - fontExtents.descent; break;
    default: break;
  }
  
  if (measure)
  {
    bounds = IRECT(0, 0, textExtents.width, fontExtents.height);
    return true;
  }

  cairo_move_to(mContext, x, y);
  cairo_show_text(mContext, str);
#endif
  return true;
}

bool IGraphicsCairo::MeasureText(const IText& text, const char* str, IRECT& bounds)
{
  return DrawText(text, str, bounds, 0, true);
}

void IGraphicsCairo::SetPlatformContext(void* pContext)
{
  if (!pContext)
  {
    if (mContext)
      cairo_destroy(mContext);
    if (mSurface)
      cairo_surface_destroy(mSurface);
      
    mContext = nullptr;
    mSurface = nullptr;
  }
  else if(!mSurface)
  {
#ifdef OS_MAC
    mSurface = cairo_quartz_surface_create_for_cg_context(CGContextRef(pContext), WindowWidth(), WindowHeight());
    mContext = cairo_create(mSurface);
    cairo_surface_set_device_scale(mSurface, GetScale(), GetScale());
#elif defined OS_WIN
    HDC dc = (HDC) pContext;
    mSurface = cairo_win32_surface_create_with_ddb(dc, CAIRO_FORMAT_ARGB32, Width(), Height());
    mContext = cairo_create(mSurface);
    cairo_surface_set_device_scale(mSurface, GetDisplayScale(), GetDisplayScale());
#else
  #error NOT IMPLEMENTED
#endif
    //cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    //cairo_set_antialias(mContext, CAIRO_ANTIALIAS_FAST);
    //cairo_set_antialias(cr, CAIRO_ANTIALIAS_GOOD);
    
    if (mContext)
    {
      cairo_set_source_rgba(mContext, 1.0, 1.0, 1.0, 1.0);
      cairo_rectangle(mContext, 0, 0, Width(), Height());
      cairo_fill(mContext);
    }
  }

  IGraphics::SetPlatformContext(pContext);
}

void IGraphicsCairo::EndFrame()
{
#ifdef OS_MAC
  //cairo_surface_flush(mSurface);
#elif defined OS_WIN
  PAINTSTRUCT ps;
  HWND hWnd = (HWND) GetWindow();
  HDC dc = BeginPaint(hWnd, &ps);
  HDC cdc = cairo_win32_surface_get_dc(mSurface);
  
  if (GetScale() == 1.f)
    BitBlt(dc, 0, 0, Width(), Height(), cdc, 0, 0, SRCCOPY);
  else
    StretchBlt(dc, 0, 0, WindowWidth(), WindowHeight(), cdc, 0, 0, Width(), Height(), SRCCOPY);

  EndPaint(hWnd, &ps);
#else
#error NOT IMPLEMENTED
#endif
}

void IGraphicsCairo::LoadFont(const char* name)
{
#ifdef IGRAPHICS_FREETYPE
  if(!mFTLibrary)
    FT_Init_FreeType(&mFTLibrary);

  WDL_String fontNameWithoutExt(name, (int) strlen(name));
  fontNameWithoutExt.remove_fileext();
  WDL_String fullPath;
  OSFindResource(name, "ttf", fullPath);

  FT_Face ftFace;
  FT_Error ftError;
  
  if (fullPath.GetLength())
  {
    ftError = FT_New_Face(mFTLibrary, fullPath.Get(), 0 /* TODO: some font files can contain multiple faces, but we don't do this*/, &ftFace);
    //TODO: error check

    mFTFaces.Add(ftFace);

    ftError = FT_Set_Char_Size(ftFace, FONT_SIZE * 64, FONT_SIZE * 64, 0, 0 ); // 72 DPI
    //TODO: error check
    cairo_font_face_t* pCairoFace = cairo_ft_font_face_create_for_ft_face(ftFace, 0);
    mCairoFTFaces.Add(pCairoFace);
  }
#endif
}
