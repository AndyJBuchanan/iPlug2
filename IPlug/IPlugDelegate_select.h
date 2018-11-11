/*
 ==============================================================================
 
 This file is part of the iPlug 2 library
 
 Oli Larkin et al. 2018 - https://www.olilarkin.co.uk
 
 iPlug 2 is an open source library subject to commercial or open-source
 licensing.
 
 The code included in this file is provided under the terms of the WDL license
 - https://www.cockos.com/wdl/
 
 ==============================================================================
 */

//#if defined PUGL_EDITOR_DELEGATE
//  #include "PUGLEditorDelegate.h"
//  typedef PUGLEditorDelegate EDITOR_DELEGATE_CLASS;
//#elif defined UIKIT_EDITOR_DELEGATE
//  #include "UIKitEditorDelegate.h"
//  typedef UIKitEditorDelegate EDITOR_DELEGATE_CLASS;
#if defined NO_IGRAPHICS
  #include "IPlugEditorDelegate.h"
  typedef IEditorDelegate EDITOR_DELEGATE_CLASS;
#else
  #if defined WEBSOCKET_SERVER
    #include "IWebsocketEditorDelegate.h"
    typedef IWebsocketEditorDelegate EDITOR_DELEGATE_CLASS;
  #else
    #include "IGraphicsEditorDelegate.h"
    typedef IGEditorDelegate EDITOR_DELEGATE_CLASS;
  #endif
#endif
