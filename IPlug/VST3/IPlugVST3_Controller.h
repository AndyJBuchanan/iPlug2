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

#ifndef _IPLUGAPI_
#define _IPLUGAPI_

/**
 * @file
 * @copydoc IPlugVST3Controller
 */

#undef stricmp
#undef strnicmp
#include "public.sdk/source/vst/vsteditcontroller.h"

#include "IPlugAPIBase.h"

using namespace Steinberg;
using namespace Vst;

class IPlugVST3View;

/**  VST3 Controller base class for a distributed IPlug VST3 plug-in
 *   @ingroup APIClasses */
class IPlugVST3Controller : public EditControllerEx1
                          , public IMidiMapping
                          , public IPlugAPIBase
{
public:
  struct IPlugInstanceInfo
  {
    Steinberg::FUID mOtherGUID;
  };
  
  IPlugVST3Controller(IPlugInstanceInfo instanceInfo, IPlugConfig c);
  virtual ~IPlugVST3Controller();

  // IEditController
  tresult PLUGIN_API initialize (FUnknown* context) override;
  IPlugView* PLUGIN_API createView (FIDString name) override;
  tresult PLUGIN_API setComponentState (IBStream* state) override; // receives the processor's state
  tresult PLUGIN_API setState (IBStream* state) override;
  tresult PLUGIN_API getState (IBStream* state) override;
  
  tresult PLUGIN_API setParamNormalized (ParamID tag, ParamValue value) override;
  ParamValue PLUGIN_API getParamNormalized(ParamID tag) override;
  ParamValue PLUGIN_API plainParamToNormalized(ParamID tag, ParamValue plainValue) override;
  ParamValue PLUGIN_API normalizedParamToPlain (ParamID tag, ParamValue valueNormalized) override;
  tresult PLUGIN_API getParamStringByValue (ParamID tag, ParamValue valueNormalized, String128 string) override;
  tresult PLUGIN_API getParamValueByString (ParamID tag, TChar* string, ParamValue& valueNormalized) override;

  //ComponentBase
  tresult PLUGIN_API notify (IMessage* message) override;

  //IMidiMapping
  tresult PLUGIN_API getMidiControllerAssignment(int32 busIndex, int16 channel, CtrlNumber midiControllerNumber, ParamID& tag) override;

  //IEditControllerEx
	tresult PLUGIN_API getProgramName (ProgramListID listId, int32 programIndex, String128 name /*out*/) override;
  
  DELEGATE_REFCOUNT (EditControllerEx1)
  tresult PLUGIN_API queryInterface (const char* iid, void** obj) override;
  
  //IPlugAPIBase
  void BeginInformHostOfParamChange(int idx) override { beginEdit(idx); }
  void InformHostOfParamChange(int idx, double normalizedValue) override  { performEdit(idx, normalizedValue); }
  void EndInformHostOfParamChange(int idx) override  { endEdit(idx); }
  void InformHostOfProgramChange() override  { /* TODO: */}
  void ResizeGraphics(int viewWidth, int viewHeight, float scale) override;

  //IEditorDelegate
  void SendMidiMsgFromUI(const IMidiMsg& msg) override;
  void SendSysexMsgFromUI(const ISysEx& msg) override { /* TODO */ };
  void SendArbitraryMsgFromUI(int messageTag, int controlTag = kNoTag, int dataSize = 0, const void* pData = nullptr) override;

  Vst::IComponentHandler* GetComponentHandler() const { return componentHandler; }
  IPlugVST3View* GetView() const { return mView; }

private:
  IPlugVST3View* mView = nullptr;
  Steinberg::FUID mProcessorGUID;
};

IPlugVST3Controller* MakeController();

#endif // _IPLUGAPI_
