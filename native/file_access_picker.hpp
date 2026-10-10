#pragma once
#ifdef _WIN32
// The file access picker's arming (file_access_dialog.cpp, in the worker): its Allow reading
// button wakes up a moment after the window appears and after it takes the foreground, so a
// stray Enter or click meant for the game cannot choose anything, such as the folder the picker
// opens in. The dialog refuses OK until then; the button looks disabled. In a header of its own
// so the file_access CTest checks it without showing a window.
#include <windows.h>
#include <shobjidl.h>
#include <atomic>
#include <stdexcept>
namespace mmd {
constexpr ULONGLONG PickerArmingMs=1200;
class PickerArming final : public IFileDialogEvents {
public:
 std::atomic<ULONGLONG> armedAt{GetTickCount64()+PickerArmingMs};
 // Counted again from now (never shortened): the window appeared or was raised.
 void restart(){auto at=GetTickCount64()+PickerArmingMs;for(auto was=armedAt.load();was<at&&!armedAt.compare_exchange_weak(was,at);){}}
 IFACEMETHODIMP QueryInterface(REFIID id,void** out) override{
  if(!out)return E_POINTER;
  if(id==__uuidof(IUnknown)||id==__uuidof(IFileDialogEvents)){*out=static_cast<IFileDialogEvents*>(this);AddRef();return S_OK;}
  *out=nullptr;return E_NOINTERFACE;
 }
 // It lives on pick()'s stack, unadvised before it goes.
 IFACEMETHODIMP_(ULONG) AddRef() override{return 2;}
 IFACEMETHODIMP_(ULONG) Release() override{return 1;}
 IFACEMETHODIMP OnFileOk(IFileDialog*) override{return GetTickCount64()>=armedAt.load()?S_OK:S_FALSE;}
 // The first folder it shows is the moment the window appears.
 IFACEMETHODIMP OnFolderChange(IFileDialog* dialog) override{
  if(shown)return S_OK;shown=true;restart();
  IOleWindow* ole=nullptr;HWND window=nullptr;if(dialog&&SUCCEEDED(dialog->QueryInterface(IID_PPV_ARGS(&ole)))){ole->GetWindow(&window);ole->Release();}
  if(HWND ok=window?GetDlgItem(window,IDOK):nullptr;ok&&IsWindowEnabled(ok)){EnableWindow(ok,FALSE);SetTimer(window,reinterpret_cast<UINT_PTR>(this),100,wake);}
  return S_OK;
 }
 IFACEMETHODIMP OnFolderChanging(IFileDialog*,IShellItem*) override{return S_OK;}
 IFACEMETHODIMP OnSelectionChange(IFileDialog*) override{return S_OK;}
 IFACEMETHODIMP OnShareViolation(IFileDialog*,IShellItem*,FDE_SHAREVIOLATION_RESPONSE*) override{return S_OK;}
 IFACEMETHODIMP OnTypeChange(IFileDialog*) override{return S_OK;}
 IFACEMETHODIMP OnOverwrite(IFileDialog*,IShellItem*,FDE_OVERWRITE_RESPONSE*) override{return S_OK;}
private:
 bool shown=false;
 // The timer's id is the arming (it outlives the window: pick() waits for the dialog).
 static void CALLBACK wake(HWND window,UINT,UINT_PTR id,DWORD){if(GetTickCount64()<reinterpret_cast<PickerArming*>(id)->armedAt.load())return;KillTimer(window,id);if(HWND ok=GetDlgItem(window,IDOK))EnableWindow(ok,TRUE);}
};
// Hands the dialog its arming and returns the cookie to unadvise it. A dialog that does not take
// it would accept OK at once: it is never shown, and the picker fails ("dialog_failed") instead.
template<class Dialog> DWORD armPicker(Dialog& dialog,PickerArming& arming){
 DWORD cookie=0;if(FAILED(dialog.Advise(&arming,&cookie))||!cookie)throw std::runtime_error("Cannot arm the file picker");
 return cookie;
}
}
#endif
