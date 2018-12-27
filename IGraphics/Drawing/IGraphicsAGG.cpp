/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#include <cmath>

#include "IGraphicsAGG.h"

static StaticStorage<agg::font> s_fontCache;

// Utility

inline const agg::rgba8 AGGColor(const IColor& color, const IBlend* pBlend = nullptr)
{
  return agg::rgba8(color.R, color.G, color.B, (BlendWeight(pBlend) * color.A));
}

inline agg::comp_op_e AGGBlendMode(const IBlend* pBlend)
{
  if (!pBlend)
    return agg::comp_op_src_over;
  
  switch (pBlend->mMethod)
  {
    case kBlendClobber:         return agg::comp_op_src_over;
    case kBlendAdd:             return agg::comp_op_plus;
    case kBlendColorDodge:      return agg::comp_op_color_dodge;
    case kBlendNone:
    default:
      return agg::comp_op_src_over;
  }
}

inline agg::cover_type AGGCover(const IBlend* pBlend = nullptr)
{
  return std::max(agg::cover_type(0), std::min(agg::cover_type(roundf(BlendWeight(pBlend) * 255.f)), agg::cover_type(255)));
}

agg::pixel_map* CreatePixmap(int w, int h)
{
  agg::pixel_map* pPixelMap = new IGraphicsAGG::PixelMapType();
  
  pPixelMap->create(w, h, 0);
  
  return pPixelMap;
}
 
// Rasterizing

template <typename FuncType, typename ColorArrayType>
void GradientRasterize(IGraphicsAGG::Rasterizer& rasterizer, const FuncType& gradientFunc, agg::trans_affine& xform, ColorArrayType& colorArray, agg::comp_op_e op)
{
  IGraphicsAGG::SpanAllocatorType spanAllocator;
  IGraphicsAGG::InterpolatorType spanInterpolator(xform);
  
  // Gradient types
  
  typedef agg::span_gradient<agg::rgba8, IGraphicsAGG::InterpolatorType, FuncType, ColorArrayType> SpanGradientType;
  typedef agg::renderer_scanline_aa<IGraphicsAGG::RenbaseType, IGraphicsAGG::SpanAllocatorType, SpanGradientType> RendererGradientType;
  
  // Gradient objects
  
  SpanGradientType spanGradient(spanInterpolator, gradientFunc, colorArray, 0, 512);
  RendererGradientType renderer(rasterizer.GetBase(), spanAllocator, spanGradient);
  
  rasterizer.Rasterize(renderer, op);
}

template <typename FuncType, typename ColorArrayType>
void GradientRasterizeAdapt(IGraphicsAGG::Rasterizer& rasterizer, EPatternExtend extend, const FuncType& gradientFunc, agg::trans_affine& xform, ColorArrayType& colorArray, agg::comp_op_e op)
{
  // FIX extend none
  
  switch (extend)
  {
    case kExtendNone:
    case kExtendPad:
      GradientRasterize(rasterizer, gradientFunc, xform, colorArray, op);
      break;
    case kExtendReflect:
      GradientRasterize(rasterizer, agg::gradient_reflect_adaptor<FuncType>(gradientFunc), xform, colorArray, op);
      break;
    case kExtendRepeat:
      GradientRasterize(rasterizer, agg::gradient_repeat_adaptor<FuncType>(gradientFunc), xform, colorArray, op);
      break;
  }
}

void IGraphicsAGG::Rasterizer::RasterizePattern(const IPattern& pattern, const IBlend* pBlend, EFillRule rule)
{
  mRasterizer.filling_rule(rule == kFillWinding ? agg::fill_non_zero : agg::fill_even_odd );
  
  switch (pattern.mType)
  {
    case kSolidPattern:
    {
      RendererSolid renderer(mRenBase);
      
      const IColor &color = pattern.GetStop(0).mColor;
      renderer.color(AGGColor(color, pBlend));
      
      // Rasterize
      
      Rasterize(renderer, AGGBlendMode(pBlend));
    }
      break;
      
    case kLinearPattern:
    case kRadialPattern:
    {
      // Common gradient objects
      
      const IMatrix& m = pattern.mTransform;
      
      agg::trans_affine gradientMTX(m.mXX, m.mYX , m.mXY, m.mYY, m.mTX, m.mTY);
      agg::gradient_lut<agg::color_interpolator<agg::rgba8>, 512> colorArray;
      
      // Scaling
      
      gradientMTX = (agg::trans_affine() / mGraphics.mTransform) * gradientMTX * agg::trans_affine_scaling(512.0);
      
      // Make gradient lut
      
      colorArray.remove_all();
      
      for (int i = 0; i < pattern.NStops(); i++)
      {
        const IColorStop& stop = pattern.GetStop(i);
        float offset = stop.mOffset;
        colorArray.add_color(offset, AGGColor(stop.mColor, pBlend));
      }
      
      colorArray.build_lut();
      
      // Rasterize
      
      if (pattern.mType == kLinearPattern)
      {
        GradientRasterizeAdapt(*this, pattern.mExtend, agg::gradient_y(), gradientMTX, colorArray, AGGBlendMode(pBlend));
      }
      else
      {
        GradientRasterizeAdapt(*this, pattern.mExtend, agg::gradient_radial_d(), gradientMTX, colorArray, AGGBlendMode(pBlend));
      }
    }
    break;
  }
}

#pragma mark -

IGraphicsAGG::IGraphicsAGG(IGEditorDelegate& dlg, int w, int h, int fps, float scale)
: IGraphicsPathBase(dlg, w, h, fps, scale)
, mRasterizer(*this)
, mFontEngine()
, mFontManager(mFontEngine)
, mFontCurves(mFontManager.path_adaptor())
, mFontContour(mFontCurves)
{
  DBGMSG("IGraphics AGG @ %i FPS\n", fps);
}

IGraphicsAGG::~IGraphicsAGG()
{
}

void IGraphicsAGG::DrawResize()
{
  mPixelMap.create(WindowWidth() * GetScreenScale(), WindowHeight() * GetScreenScale());
  UpdateLayer();
  mRasterizer.SetOutput(mRenBuf);
  mRasterizer.ClearWhite();
    
  mTransform = agg::trans_affine_scaling(GetBackingPixelScale(), GetBackingPixelScale());
}

void IGraphicsAGG::UpdateLayer()
{
  agg::pixel_map* pPixelMap = mLayers.empty() ? &mPixelMap : mLayers.top()->GetAPIBitmap()->GetBitmap();
  mRenBuf.attach(pPixelMap->buf(), pPixelMap->width(), pPixelMap->height(), pPixelMap->row_bytes());
}

//IFontData IGraphicsAGG::LoadFont(const char* name, const int size)
//{
//  WDL_String cacheName(name);
//  char buf [6] = {0};
//  sprintf(buf, "-%dpt", size);
//  cacheName.Append(buf);
//
//  agg::font* font_buf = s_fontCache.Find(cacheName.Get());
//  if (!font_buf)
//  {
//    font_buf = (agg::font*) OSLoadFont(name, size);
//#ifndef NDEBUG
//    bool fontResourceFound = font_buf;
//#endif
//    assert(fontResourceFound); // Protect against typos in resource.h and .rc files.
//
//    s_fontCache.Add(font_buf, cacheName.Get());
//  }
//  return IFontData(font_buf);
//}

bool CheckTransform(const agg::trans_affine& mtx)
{
  if (!agg::is_equal_eps(mtx.tx - std::round(mtx.tx), 0.0, 1e-3))
    return false;
  if (!agg::is_equal_eps(mtx.ty - std::round(mtx.ty), 0.0, 1e-3))
    return false;

  agg::trans_affine mtx_without_translate(mtx);
  mtx_without_translate.tx = mtx_without_translate.ty = 0.0;
  
  return mtx_without_translate.is_identity(1e-3);
}

void IGraphicsAGG::DrawBitmap(IBitmap& bitmap, const IRECT& dest, int srcX, int srcY, const IBlend* pBlend)
{
  IRECT bounds = mClipRECT.Empty() ? dest : mClipRECT.Intersect(dest);
  bounds.Scale(GetBackingPixelScale());

  APIBitmap* pAPIBitmap = bitmap.GetAPIBitmap();
  agg::pixel_map* pSource = pAPIBitmap->GetBitmap();
  agg::rendering_buffer src(pSource->buf(), pSource->width(), pSource->height(), pSource->row_bytes());;
  const double scale = GetScreenScale() / (pAPIBitmap->GetScale() * pAPIBitmap->GetDrawScale());

  agg::trans_affine srcMtx;
  srcMtx /= mTransform;
  srcMtx *= agg::trans_affine_translation((srcX * scale) - dest.L, (srcY * scale) - dest.T);
  srcMtx *= agg::trans_affine_scaling(bitmap.GetScale() * bitmap.GetDrawScale());
      
  // TODO - fix clipping of bitmaps in one-to-one mode
    
  if (bounds.IsPixelAligned() && CheckTransform(srcMtx))
  {
    double offsetScale = scale * GetScreenScale();
    
    bounds.Translate(mTransform.tx, mTransform.ty);
    srcX = std::round(srcX * offsetScale);
    srcY = std::round(srcX * offsetScale);
      
    mRasterizer.BlendFrom(src, bounds, srcX, srcY, AGGBlendMode(pBlend), AGGCover(pBlend));
  }
  else
  {
    PixfmtType fmtType(src);
    imgSourceType imgSrc(fmtType);
    InterpolatorType interpolator(srcMtx);
    SpanAllocatorType spanAllocator;
    alpha_span_generator<SpanGeneratorType> spanGenerator(imgSrc, interpolator, AGGCover(pBlend));
    BitmapAlphaRenderType renderer(mRasterizer.GetBase(), spanAllocator, spanGenerator);
    agg::rounded_rect rect(dest.L, dest.T, dest.R, dest.B, 0);
    agg::conv_transform<agg::rounded_rect> tr(rect, mTransform);

    mRasterizer.Rasterize(tr, renderer, AGGBlendMode(pBlend));
  }
}
/*
void IGraphicsAGG::DrawRotatedMask(IBitmap& base, IBitmap& mask, IBitmap& top, float x, float y, double angle, const IBlend* pBlend)
{
  x *= GetScreenScale();
  y *= GetScreenScale();

  agg::pixel_map* pm_base = base.GetAPIBitmap()->GetBitmap();
  agg::pixel_map* pm_mask = mask.GetAPIBitmap()->GetBitmap();
  agg::pixel_map* pm_top = top.GetAPIBitmap()->GetBitmap();
  
  agg::rendering_buffer rbuf_base(pm_base->buf(), pm_base->width(), pm_base->height(), pm_base->row_bytes());
  agg::rendering_buffer rbuf_mask(pm_mask->buf(), pm_mask->width(), pm_mask->height(), pm_mask->row_bytes());
  agg::rendering_buffer rbuf_top(pm_top->buf(), pm_top->width(), pm_top->height(), pm_top->row_bytes());

  PixfmtType img_base(rbuf_base);
  PixfmtType img_mask(rbuf_mask);
  PixfmtType img_top(rbuf_top);

  RenbaseType ren_base(img_base);
  
  ren_base.clear(agg::rgba8(255, 255, 255, 0));
  
  ren_base.blend_from(img_mask, 0, 0, agg::cover_mask);
  ren_base.copy_from(img_top);
  
  const int width = base.W() * GetScreenScale();
  const int height = base.H() * GetScreenScale();
  
  agg::trans_affine srcMatrix;
  srcMatrix *= agg::trans_affine_translation(-(width / 2), -(height / 2));
  srcMatrix *= agg::trans_affine_rotation(angle);
  srcMatrix *= agg::trans_affine_translation(x + (width / 2), y + (height / 2));
  
  agg::trans_affine imgMtx = srcMatrix;
  imgMtx.invert();
  
  InterpolatorType interpolator(imgMtx);
  
  imgSourceType imgSrc(img_base);
  
  SpanGeneratorType spanGenerator(imgSrc, interpolator);
  SpanAllocatorType spanAllocator;
  BitmapRenderType renderer(mRasterizer.GetBase(), spanAllocator, spanGenerator);
  
  agg::rounded_rect bounds(0, 0, width, height, 0);
  agg::conv_transform<agg::rounded_rect> tr(bounds, srcMatrix);
  
  mRasterizer.SetPath(tr);
  mRasterizer.Rasterize(renderer, AGGBlendMode(pBlend));
}*/

void IGraphicsAGG::PathArc(float cx, float cy, float r, float aMin, float aMax)
{
  agg::path_storage transformedPath;
    
  agg::arc arc(cx, cy, r, r, DegToRad(aMin - 90.f), DegToRad(aMax - 90.f));
  arc.approximation_scale(mTransform.scale());
    
  transformedPath.join_path(arc);
  transformedPath.transform(mTransform);
  
  mPath.join_path(transformedPath);
}

void IGraphicsAGG::PathMoveTo(float x, float y)
{
  double xd = x;
  double yd = y;
  
  mTransform.transform(&xd, &yd);
  mPath.move_to(xd, yd);
}

void IGraphicsAGG::PathLineTo(float x, float y)
{
  double xd = x;
  double yd = y;

  mTransform.transform(&xd, &yd);
  mPath.line_to(xd, yd);
}

void IGraphicsAGG::PathCurveTo(float x1, float y1, float x2, float y2, float x3, float y3)
{
  double x1d = x1;
  double y1d = y1;
  double x2d = x2;
  double y2d = y2;
  double x3d = x3;
  double y3d = y3;
  
  mTransform.transform(&x1d, &y1d);
  mTransform.transform(&x2d, &y2d);
  mTransform.transform(&x3d, &y3d);

  mPath.curve4(x1d, y1d, x2d, y2d, x3d, y3d);
}

template<typename StrokeType>
void StrokeOptions(StrokeType& strokes, double thickness, const IStrokeOptions& options)
{
  // Set stroke options
  
  strokes.width(thickness);
  
  switch (options.mCapOption)
  {
    case kCapButt:   strokes.line_cap(agg::butt_cap);     break;
    case kCapRound:  strokes.line_cap(agg::round_cap);    break;
    case kCapSquare: strokes.line_cap(agg::square_cap);   break;
  }
  
  switch (options.mJoinOption)
  {
    case kJoinMiter:   strokes.line_join(agg::miter_join);   break;
    case kJoinRound:   strokes.line_join(agg::round_join);   break;
    case kJoinBevel:   strokes.line_join(agg::bevel_join);   break;
  }
  
  strokes.miter_limit(options.mMiterLimit);
}

void IGraphicsAGG::PathStroke(const IPattern& pattern, float thickness, const IStrokeOptions& options, const IBlend* pBlend)
{
  typedef agg::conv_curve<agg::path_storage>    CPType;
  typedef agg::conv_transform<CPType>           S1Type;
  typedef agg::conv_stroke<S1Type>              S2Type;
  typedef agg::conv_transform<S2Type>           S3Type;
  typedef agg::conv_dash<S1Type>                D2Type;
  typedef agg::conv_stroke<D2Type>              D3Type;
  typedef agg::conv_transform<D3Type>           D4Type;

  agg::trans_affine tranform(mTransform);
  CPType curvedPath(mPath);
  S1Type basePath(curvedPath, tranform.invert());

  if (options.mDash.GetCount())
  {
    D2Type dashedPath(basePath);
    D3Type strokedDashedPath(dashedPath);
    D4Type finalPath(strokedDashedPath, mTransform);
      
    // Set the dashes (N.B. - for odd counts the array is read twice)

    int dashCount = options.mDash.GetCount();
    int dashMax = dashCount & 1 ? dashCount * 2 : dashCount;
    const float* dashArray = options.mDash.GetArray();
    
    dashedPath.remove_all_dashes();
    dashedPath.dash_start(options.mDash.GetOffset());
    
    for (int i = 0; i < dashMax; i += 2)
      dashedPath.add_dash(dashArray[i % dashCount], dashArray[(i + 1) % dashCount]);
    
    StrokeOptions(strokedDashedPath, thickness, options);
    mRasterizer.Rasterize(finalPath, pattern, pBlend);
  }
  else
  {
    S2Type strokedPath(basePath);
    S3Type finalPath(strokedPath, mTransform);
      
    StrokeOptions(strokedPath, thickness, options);
    mRasterizer.Rasterize(finalPath, pattern, pBlend);
  }
  
  if (!options.mPreserve)
    mPath.remove_all();
}

void IGraphicsAGG::PathFill(const IPattern& pattern, const IFillOptions& options, const IBlend* pBlend)
{
  agg::conv_curve<agg::path_storage> curvedPath(mPath);
  mRasterizer.Rasterize(curvedPath, pattern, pBlend, options.mFillRule);
  if (!options.mPreserve)
    mPath.remove_all();
}

IColor IGraphicsAGG::GetPoint(int x, int y)
{
  agg::rgba8 point = mRasterizer.GetPixel(x, y);
  IColor color(point.a, point.r, point.g, point.b);
  return color;
}

APIBitmap* IGraphicsAGG::LoadAPIBitmap(const WDL_String& resourcePath, int scale)
{
  const char *path = resourcePath.Get();

  if (CStringHasContents(path))
  {
    const char* ext = path+strlen(path)-1;
    while (ext >= path && *ext != '.') --ext;
    ++ext;
    
    bool ispng = !stricmp(ext, "png");
#ifndef IPLUG_JPEG_SUPPORT
    if (!ispng) return 0;
#else
    bool isjpg = !stricmp(ext, "jpg");
    if (!isjpg && !ispng) return 0;
#endif
    
    PixelMapType* pPixelMap = new PixelMapType();
#ifdef OS_MAC
    if (pPixelMap->load_img(path, ispng ? agg::pixel_map::format_png : agg::pixel_map::format_jpg))
#elif defined OS_WIN
    if (pPixelMap->load_img((HINSTANCE) GetPlatformInstance(), path, ispng ? agg::pixel_map::format_png : agg::pixel_map::format_jpg))
#else
#error NOT IMPLEMENTED!
#endif
      return new AGGBitmap(pPixelMap, scale, 1.f);
    else
      delete pPixelMap;
  }
  
  return new APIBitmap();
}

APIBitmap* IGraphicsAGG::ScaleAPIBitmap(const APIBitmap* pBitmap, int scale)
{
  int destW = (pBitmap->GetWidth() / pBitmap->GetScale()) * scale;
  int destH = (pBitmap->GetHeight() / pBitmap->GetScale()) * scale;
    
  agg::pixel_map* pSource = pBitmap->GetBitmap();
  agg::pixel_map* pCopy = CreatePixmap(destW, destH);
  agg::rendering_buffer src(pSource->buf(), pSource->width(), pSource->height(), pSource->row_bytes());;
  agg::rendering_buffer dest(pCopy->buf(), pCopy->width(), pCopy->height(), pCopy->row_bytes());
  PixfmtType imgPixfSrc(src);
  PixfmtType imgPixfDest(dest);
  
  RenbaseType renBase(imgPixfDest);
  renBase.clear(agg::rgba(0, 0, 0, 0));
  
  agg::rasterizer_scanline_aa<> rasterizer;
  agg::trans_affine_scaling srcMtx(pBitmap->GetScale() / (double) scale);
  InterpolatorType interpolator(srcMtx);
  imgSourceType imgSrc(imgPixfSrc);
  SpanAllocatorType spanAllocator;
  agg::scanline_u8 scanline;
  SpanGeneratorType spanGenerator(imgSrc, interpolator);
  BitmapRenderType renderer(renBase, spanAllocator, spanGenerator);
  
  agg::rounded_rect bounds(0, 0, destW, destH, 0);
  rasterizer.add_path(bounds);
  agg::render_scanlines(rasterizer, scanline, renderer);
  
  return new AGGBitmap(pCopy, scale, pBitmap->GetDrawScale());
}

APIBitmap* IGraphicsAGG::CreateAPIBitmap(int width, int height)
{
  const double scale = GetBackingPixelScale();
  return new AGGBitmap(CreatePixmap(std::round(width * scale), std::round(height * scale)), GetScreenScale(), GetDrawScale());
}

void IGraphicsAGG::EndFrame()
{
#ifdef OS_MAC
  CGContextSaveGState((CGContext*) GetPlatformContext());
  CGContextTranslateCTM((CGContext*) GetPlatformContext(), 0.0, WindowHeight());
  CGContextScaleCTM((CGContext*) GetPlatformContext(), 1.0, -1.0);
  mPixelMap.draw((CGContext*) GetPlatformContext(), GetScreenScale());
  CGContextRestoreGState((CGContext*) GetPlatformContext());
#else
  PAINTSTRUCT ps;
  HWND hWnd = (HWND) GetWindow();
  HDC dc = BeginPaint(hWnd, &ps);
  mPixelMap.draw(dc, 1.0);
  EndPaint(hWnd, &ps);
#endif
}

void IGraphicsAGG::CalculateTextLines(WDL_TypedBuf<LineInfo>* pLines, const IRECT& bounds, const char* str, FontManagerType& manager)
{
  LineInfo info;
  info.mStartChar = 0;
  info.mEndChar = (int) strlen(str);
  pLines->Add(info);
  
  LineInfo* pLine = pLines->Get();
  
  size_t lineStart = 0;
  size_t lineWidth = 0;
  size_t linePos = 0;
  double xCount = 0.0;
  
  const char* cstr = str;
  
  while (*cstr)
  {
    const agg::glyph_cache* pGlyph = manager.glyph(*cstr);
    
    if (pGlyph)
    {
      xCount += pGlyph->advance_x;
    }

    cstr++;
    linePos++;
    
    if (*cstr == ' ' || *cstr == 0)
    {
      pLine->mStartChar = (int) lineStart;
      pLine->mEndChar = (int)  linePos;
      pLine->mWidth = xCount;
    }
    
    if (bounds.W() > 0 && xCount >= bounds.W())
    {
      assert(pLine);
      
      cstr = &str[pLine->mEndChar];
      lineStart = pLine->mEndChar + 1;
      linePos = pLine->mEndChar;
      
      LineInfo info;
      pLines->Add(info);
      pLine++;
      
      assert(pLines);
      
      xCount = 0;
      lineWidth = 0;
    }
    
  }
}

bool IGraphicsAGG::DoDrawMeasureText(const IText& text, const char* str, IRECT& destBounds, const IBlend* pBlend, bool measure)
{
//  if (!str || str[0] == '\0')
//  {
//    return true;
//  }
//
//  IRECT bounds = destBounds;
//  bounds.Scale(GetScreenScale());
//
//  RendererSolid renSolid(mRenBase);
//  RendererBin renBin(mRenBase);
//
//  agg::scanline_u8 sl;
//  agg::rasterizer_scanline_aa<> ras;
//
//  agg::glyph_rendering gren = agg::glyph_ren_agg_gray8;
//  //agg::glyph_rendering gren = agg::glyph_ren_outline;
//  //agg::glyph_rendering gren = agg::glyph_ren_agg_mono;
//  //agg::glyph_rendering gren = agg::glyph_ren_native_gray8;
//  //agg::glyph_rendering gren = agg::glyph_ren_native_mono;
//
//  float weight = 0.0;
//  bool kerning = false;
//  bool hinting = false;
//
//  if (gren == agg::glyph_ren_agg_mono)
//  {
//    mFontEngine.gamma(agg::gamma_threshold(0.5));
//  }
//  else
//  {
//    mFontEngine.gamma(agg::gamma_power(1.0));
//  }
//
//  if (gren == agg::glyph_ren_outline)
//  {
//    //for outline cache set gamma for the rasterizer
//    ras.gamma(agg::gamma_power(1.0));
//  }
//
//  mFontContour.width(-weight * (text.mSize * 0.05) * GetScreenScale());
//
//  IFontData font = LoadFont(text.mFont, text.mSize);
//  agg::font* pFontData = (agg::font *)font.mData;
//
//  if (pFontData != 0 && mFontEngine.load_font("", 0, gren, pFontData->buf(), pFontData->size()))
//  {
//    mFontEngine.hinting(hinting);
//    mFontEngine.height(text.mSize * GetScreenScale());
//    mFontEngine.width(text.mSize * GetScreenScale());
//    mFontEngine.flip_y(true);
//
//    double x = bounds.L;
//    double y = bounds.T + (text.mSize * GetScreenScale());
//
//    WDL_TypedBuf<LineInfo> lines;
//
//    CalculateTextLines(&lines, bounds, str, mFontManager);
//
//    LineInfo * pLines = lines.Get();
//
//    for (int i=0; i<lines.GetSize(); ++i, ++pLines)
//    {
//      switch (text.mAlign)
//      {
//        case IText::kAlignNear:
//          x = bounds.L;
//          break;
//        case IText::kAlignCenter:
//          x = bounds.L + ((bounds.W() - pLines->mWidth) / 2);
//          break;
//        case IText::kAlignFar:
//          x = bounds.L + (bounds.W() - pLines->mWidth);
//          break;
//      }
//
//      for (size_t c=pLines->mStartChar; c<pLines->mEndChar; c++)
//      {
//        const agg::glyph_cache* pGlyph = mFontManager.glyph(str[c]);
//
//        if (pGlyph)
//        {
//          if (kerning)
//          {
//            mFontManager.add_kerning(&x, &y);
//          }
//
//          mFontManager.init_embedded_adaptors(pGlyph, x, y);
//
//          switch (pGlyph->data_type)
//          {
//            case agg::glyph_data_mono:
//
//              renBin.color(IColorToAggColor(text.mColor));
//              agg::render_scanlines(mFontManager.mono_adaptor(),
//                                    mFontManager.mono_scanline(),
//                                    renBin);
//              break;
//
//            case agg::glyph_data_gray8:
//
//              renSolid.color(IColorToAggColor(text.mColor));
//              agg::render_scanlines(mFontManager.gray8_adaptor(),
//                                    mFontManager.gray8_scanline(),
//                                    renSolid);
//              break;
//
//            case agg::glyph_data_outline:
//
//              ras.reset();
//
//              if (fabs(weight) <= 0.01)
//              {
//                //for the sake of efficiency skip the
//                //contour converter if the weight is about zero.
//                ras.add_path(mFontCurves);
//              }
//              else
//              {
//                ras.add_path(mFontContour);
//              }
//
//              renSolid.color(IColorToAggColor(text.mColor));
//              agg::render_scanlines(ras, sl, renSolid);
//
//              break;
//
//            default: break;
//          }
//
//          //increment pen position
//          x += pGlyph->advance_x;
//          y += pGlyph->advance_y;
//        }
//      }
//      y += text.mSize * GetScreenScale();
//    }
//  }
  return false;
}

/*
agg::pixel_map* IGraphicsAGG::load_image(const char* filename)
{
  IBitmap bitmap = LoadBitmap(filename, 1, 1.0);
  return (agg::pixel_map*) bitmap.mData;
}

*/

#include "IGraphicsAGG_src.cpp"
