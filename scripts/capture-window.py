"""Capture a game window by PID with PrintWindow(PW_RENDERFULLCONTENT) and save it as PNG.
Source's VGUI and the game frame both appear (render.Capture misses VGUI).

python scripts/capture-window.py <pid> <out.png>
"""
import ctypes,sys
from ctypes import wintypes
from PIL import Image
pid=int(sys.argv[1]);out=sys.argv[2]
u=ctypes.WinDLL('user32',use_last_error=True);g=ctypes.WinDLL('gdi32')
u.SetProcessDPIAware()
found=[]
@ctypes.WINFUNCTYPE(wintypes.BOOL,wintypes.HWND,wintypes.LPARAM)
def cb(h,_):
    p=wintypes.DWORD();u.GetWindowThreadProcessId(h,ctypes.byref(p))
    if p.value==pid and u.IsWindowVisible(h):
        r=wintypes.RECT();u.GetClientRect(h,ctypes.byref(r))
        if r.right*r.bottom>0:found.append((r.right*r.bottom,h,r.right,r.bottom))
    return True
u.EnumWindows(cb,0)
assert found,'no window'
_,h,w,hgt=max(found)
wdc=u.GetWindowDC(h);dc=g.CreateCompatibleDC(wdc);bmp=g.CreateCompatibleBitmap(wdc,w,hgt);g.SelectObject(dc,bmp)
ok=u.PrintWindow(h,dc,3)
class BIH(ctypes.Structure):
    _fields_=[('biSize',wintypes.DWORD),('biWidth',wintypes.LONG),('biHeight',wintypes.LONG),('biPlanes',wintypes.WORD),('biBitCount',wintypes.WORD),('biCompression',wintypes.DWORD),('biSizeImage',wintypes.DWORD),('biXPelsPerMeter',wintypes.LONG),('biYPelsPerMeter',wintypes.LONG),('biClrUsed',wintypes.DWORD),('biClrImportant',wintypes.DWORD)]
bi=BIH();bi.biSize=ctypes.sizeof(BIH);bi.biWidth=w;bi.biHeight=-hgt;bi.biPlanes=1;bi.biBitCount=32
buf=ctypes.create_string_buffer(w*hgt*4)
g.GetDIBits(dc,bmp,0,hgt,buf,ctypes.byref(bi),0)
Image.frombuffer('RGBA',(w,hgt),buf,'raw','BGRA',0,1).convert('RGB').save(out)
g.DeleteObject(bmp);g.DeleteDC(dc);u.ReleaseDC(h,wdc)
print('saved',out,w,hgt,'printwindow',ok)
