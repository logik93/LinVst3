/*  dssi-vst: a DSSI plugin wrapper for VST effects and instruments
    Copyright 2004-2007 Chris Cannam

    This file is part of linvst.

    linvst is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>

#include <sched.h>

#define stricmp strcasecmp
#define strnicmp strncasecmp

//#define WIN32_LEAN_AND_MEAN

//#include <windows.h>

#include "remotepluginserver.h"

#include "paths.h"

#include "public.sdk/source/vst/hosting/module.h"
#include "vst2wrapper.h"
#include "vst2wrapper.sdk.cpp"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

extern "C" {
typedef bool(PLUGIN_API *InitModuleProc)();
typedef bool(PLUGIN_API *ExitModuleProc)();
}
static const Steinberg::FIDString kInitModuleProcName = "InitDll";
static const Steinberg::FIDString kExitModuleProcName = "ExitDll";

#define APPLICATION_CLASS_NAME "dssi_vst"
#ifdef TRACKTIONWM
#define APPLICATION_CLASS_NAME2 "dssi_vst2"
#endif
#define OLD_PLUGIN_ENTRY_POINT "main"
#define NEW_PLUGIN_ENTRY_POINT "VSTPluginMain"

#if VST_FORCE_DEPRECATED
#define DEPRECATED_VST_SYMBOL(x) __##x##Deprecated
#else
#define DEPRECATED_VST_SYMBOL(x) x
#endif

#ifdef VESTIGE
typedef AEffect *(VESTIGECALLBACK *VstEntry)(audioMasterCallback audioMaster);
#else
typedef AEffect *(VSTCALLBACK *VstEntry)(audioMasterCallback audioMaster);
#endif

RemotePluginDebugLevel debugLevel = RemotePluginDebugNone;

#define disconnectserver 32143215

//#define hidegui2 77775634

using namespace std;

class RemoteVSTServer : public RemotePluginServer {
public:
  RemoteVSTServer(std::string fileIdentifiers, std::string fallbackName);
  virtual ~RemoteVSTServer();

  // virtual std::string getName() { return m_name; }
  // virtual std::string getMaker() { return m_maker; }
  virtual std::string getName();
  virtual std::string getMaker();

  virtual void setBufferSize(int);
  virtual void setSampleRate(int);
  virtual void reset();
  virtual void terminate();

  virtual int getInputCount() { return vst2wrap->numinputs; }
  virtual int getOutputCount() { return vst2wrap->numoutputs; }
  virtual int getFlags() {
    if (vst2wrap->synth == true)
      mpluginptr->flags |= effFlagsIsSynth;
#ifdef DOUBLEP
    if (vst2wrap->doublereplacing == true)
      mpluginptr->flags |= effFlagsCanDoubleReplacing;
#endif
    mpluginptr->flags |= effFlagsCanReplacing;
    mpluginptr->flags |= effFlagsProgramChunks;
    return mpluginptr->flags;
  }
  virtual int getinitialDelay() { return vst2wrap->initialdelay; }
  virtual int getUID() { return vst2uid; }
  virtual int getParameterCount() { 
#ifdef PCACHE
      int val = vst2wrap->numparams;
      
      numpars = val;
      
      ParamState p;
  
      for (int i = 0; i < val; ++i)
      {
      if(i >= 10000)
      break;
      p.changed = 0;   
      float value = getParameter(i);      
      p.value = value;   
      p.valueupdate = value;        
      memcpy(&m_shm5[i * sizeof(ParamState)], &p, sizeof(ParamState));      
      }   
       
      return val;                   
#else        
  return vst2wrap->numparams; 
#endif    
  }
  virtual std::string getParameterName(int);
  virtual std::string getParameterLabel(int);
  virtual std::string getParameterDisplay(int);
  virtual void setParameter(int, float);
  virtual float getParameter(int);
  virtual void getParameters(int, int, float *);

  virtual int getProgramCount() { return vst2wrap->numprograms; }
  virtual int getProgramNameIndexed(int, char *name);
  virtual std::string getProgramName();

  virtual void setCurrentProgram(int);

  virtual void showGUI(ShmControl *m_shmControlptr);
  virtual void hideGUI();
  virtual void hideGUI2();
#ifdef EMBED
  virtual void openGUI();
#endif
  virtual void guiUpdate();
  virtual void finisherror();

  virtual int getEffInt(int opcode, int value);
  virtual std::string getEffString(int opcode, int index);
  virtual void effDoVoid(int opcode);
  virtual int effDoVoid2(int opcode, int index, int value, float opt);

  //    virtual int         getInitialDelay() {return m_plugin->initialDelay;}
  //    virtual int         getUniqueID() { return m_plugin->uniqueID;}
  //    virtual int         getVersion() { return m_plugin->version;}

  virtual int processVstEvents();

  virtual void getChunk(ShmControl *m_shmControlptr);
  virtual void setChunk(ShmControl *m_shmControlptr);
  virtual void canBeAutomated(ShmControl *m_shmControlptr);
  virtual void getProgram(ShmControl *m_shmControlptr);
  virtual void EffectOpen(ShmControl *m_shmControlptr);
  //    virtual void        eff_mainsChanged(int v);

  virtual void process(float **inputs, float **outputs, int sampleFrames);
#ifdef DOUBLEP
  virtual void processdouble(double **inputs, double **outputs,
                             int sampleFrames);
  virtual bool setPrecision(int);
#endif

#ifndef VESTIGE
  virtual bool getOutProp(int);
  virtual bool getInProp(int);
#endif

#ifdef MIDIEFF
  virtual bool getMidiKey(int, ShmControl *m_shmControlptr);
  virtual bool getMidiProgName(int, ShmControl *m_shmControlptr);
  virtual bool getMidiCurProg(int, ShmControl *m_shmControlptr);
  virtual bool getMidiProgCat(int, ShmControl *m_shmControlptr);
  virtual bool getMidiProgCh(int, ShmControl *m_shmControlptr);
  virtual bool setSpeaker(ShmControl *m_shmControlptr);
  virtual bool getSpeaker(ShmControl *m_shmControlptr);
#endif
#ifdef CANDOEFF
  virtual bool getEffCanDo(std::string);
#endif

  virtual void setDebugLevel(RemotePluginDebugLevel level) {
    debugLevel = level;
  }

  virtual bool warn(std::string);

  virtual void waitForServer(ShmControl *m_shmControlptr);
  virtual void waitForServerexit();
  
#ifdef DRAGWIN   
  virtual Window findxdndwindow(Display* display, Window child);
#endif    

  HWND hWnd;
  WNDCLASSEX wclass;
#ifdef DRAGWIN   
  HWINEVENTHOOK g_hook;
  HCURSOR hCurs1;
  HCURSOR hCurs2;
  Display *display;
  Window drag_win;
  int dodragwin;   
  Window window;
  HWND winehwnd;		
  int xdndversion;				
  unsigned char *data;
  unsigned char *data2;
  Window proxyptr;				
  int prevx;
  int prevy;
  int windowok;
  int dndfinish;
  int dndaccept;
  int dropdone;
  string dragfilelist;
  Atom atomlist;
  Atom atomplain;
  Atom atomstring;
  Window pwindow;        
  Atom XdndSelection;
  Atom XdndAware;
  Atom XdndProxy;
  Atom XdndTypeList;
  Atom XdndActionCopy;   
  Atom XdndPosition; 
  Atom XdndStatus; 
  Atom XdndEnter;
  Atom XdndDrop;
  Atom XdndLeave;
  Atom XdndFinished; 
#endif  
#ifdef TRACKTIONWM
  WNDCLASSEX wclass2;
  POINT offset;
#endif
  bool haveGui;
#ifdef EMBED
  HANDLE handlewin;
  struct winmessage {
    int handle;
    int width;
    int height;
    int winerror;
  } winm2;
  winmessage *winm;
#endif
  int guiupdate;
  int guiupdatecount;
  int guiresizewidth;
  int guiresizeheight;
  ERect *rect;
  int setprogrammiss;
  int hostreaper;
  int melda;
  int wavesthread;
  /*
#ifdef EMBED
#ifdef TRACKTIONWM
  int                 hosttracktion;
#endif

#endif
* */
  AEffect mplugin;
  AEffect *mpluginptr;
  VstEvents vstev[VSTSIZE];
  bool exiting;
  bool effectrun;
  bool inProcessThread;
  bool guiVisible;
  int parfin;
  int audfin;
  int getfin;
  int confin;  
  int hidegui;
  
#ifdef PCACHE
  int numpars;
#endif    

  std::string deviceName2;

  Steinberg::Vst::Vst2Wrapper *vst2wrap;
  int vst2uid;
  Steinberg::IPluginFactory *factory;

private:
  std::string m_name;
  std::string m_maker;
};

RemoteVSTServer *remoteVSTServerInstance = 0;

LRESULT WINAPI MainProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_CLOSE:
#ifndef EMBED
    if (remoteVSTServerInstance) {
      if (!remoteVSTServerInstance->exiting &&
          remoteVSTServerInstance->guiVisible) {
        //remoteVSTServerInstance->hidegui = 1;
          remoteVSTServerInstance->hideGUI();
        return 0;
      }
    }
#endif
    break;

  case WM_TIMER:
    break;

  default:
    return DefWindowProc(hWnd, msg, wParam, lParam);
  }
  return 0;
}

#ifdef TRACKTIONWM
LRESULT WINAPI MainProc2(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_CLOSE:
    break;

  case WM_TIMER:
    break;

  default:
    return DefWindowProc(hWnd, msg, wParam, lParam);
  }
  return 0;
}
#endif

DWORD WINAPI AudioThreadMain(LPVOID parameter) {
  /*
      struct sched_param param;
      param.sched_priority = 1;

      int result = sched_setscheduler(0, SCHED_FIFO, &param);

      if (result < 0)
      {
          perror("Failed to set realtime priority for audio thread");
      }
  */
  while (!remoteVSTServerInstance->exiting) {
    remoteVSTServerInstance->dispatchProcess(5);
  }
  // param.sched_priority = 0;
  // (void)sched_setscheduler(0, SCHED_OTHER, &param);
  remoteVSTServerInstance->audfin = 1;
  ExitThread(0);
  return 0;
}

#ifdef DRAGWIN 
Window RemoteVSTServer::findxdndwindow(Display* display, Window child)
{
int gchildx;
int gchildy;
unsigned int gmask;
Atom type;
int fmt;
unsigned long nitems; 
unsigned long remaining;
Window groot;
Window gchild;
int grootx;
int grooty;
unsigned char *data3;
int x, y;

  data3 = 0;
  type = None;
  fmt = 0;
  nitems = 0;
 
  if (child == None) 
  return None;
  	  
  if(XGetWindowProperty(display, child, XdndAware, 0, 1, False, AnyPropertyType, &type, &fmt, &nitems, &remaining, &data3) == Success)
  { 
  if(data3 && (type != None) && (fmt == 32) && (nitems == 1))	
  {
  if(data3)
  {
  xdndversion = *data3;
  XFree(data3);
  data3 = 0;
  return child;
  }
  }
  }
  
  XQueryPointer(display, child, &groot, &gchild, &x, &y, &gchildx, &gchildx, &gmask);
 	
  if(gchild == None) 
  return None;
 		
  return findxdndwindow(display, gchild);
  }
#endif

#ifdef DRAGWIN   
DWORD WINAPI GetSetThreadMain(LPVOID parameter) {
POINT point;
Window groot;
Window gchild;
int grootx;
int grooty;
int gchildx;
int gchildy;
unsigned int gmask;
Atom type;
int fmt;
unsigned long nitems; 
unsigned long remaining;
int x, y;
XEvent e;
XClientMessageEvent xdndclient;
XEvent xdndselect;  
   
  /*
      struct sched_param param;
      param.sched_priority = 1;

      int result = sched_setscheduler(0, SCHED_FIFO, &param);

      if (result < 0)
      {
          perror("Failed to set realtime priority for audio thread");
      }
  */

  while (!remoteVSTServerInstance->exiting) {
  
  if(remoteVSTServerInstance->dodragwin == 1)
  {  
  usleep(1000);
  
  if(IsWindow(FindWindow(NULL , TEXT("TrackerWindow"))))
  remoteVSTServerInstance->windowok = 1;
  else
  remoteVSTServerInstance->windowok = 0;
  
   //     GetCursorPos(&point); 
  XQueryPointer(remoteVSTServerInstance->display, DefaultRootWindow(remoteVSTServerInstance->display), &groot, &gchild, &x, &y, &gchildx, &gchildx, &gmask);
  XFlush(remoteVSTServerInstance->display);
  
  point.x = x; 
  point.y = y;
 
  remoteVSTServerInstance->winehwnd = WindowFromPoint(point);
  if(remoteVSTServerInstance->winehwnd && (remoteVSTServerInstance->winehwnd != GetDesktopWindow()))
  {
  if(remoteVSTServerInstance->windowok == 0)
  {
  remoteVSTServerInstance->dodragwin = 0;
  continue;
  }
  }
  else
  {		    
  SetCursor(remoteVSTServerInstance->hCurs1);
 
  if((x == remoteVSTServerInstance->prevx) && (y == remoteVSTServerInstance->prevy))
  ;
  else
  {
  remoteVSTServerInstance->prevx = x;
  remoteVSTServerInstance->prevy = y;
				
  remoteVSTServerInstance->xdndversion = -1;			
    
  remoteVSTServerInstance->window = remoteVSTServerInstance->findxdndwindow(remoteVSTServerInstance->display, DefaultRootWindow(remoteVSTServerInstance->display));
 
  if(remoteVSTServerInstance->window != None)
  { 	
  if((remoteVSTServerInstance->window != remoteVSTServerInstance->pwindow) && (remoteVSTServerInstance->xdndversion != -1))
  {			
  if(remoteVSTServerInstance->xdndversion > 3)
  {
  remoteVSTServerInstance->proxyptr = 0;
  remoteVSTServerInstance->data2 = 0;
  type = None;
  fmt = 0;
  nitems = 0;
				 
  if(XGetWindowProperty(remoteVSTServerInstance->display, remoteVSTServerInstance->window, remoteVSTServerInstance->XdndProxy, 0, 1, False, AnyPropertyType, &type, &fmt, &nitems, &remaining, &remoteVSTServerInstance->data2) == Success)
  {
  if (remoteVSTServerInstance->data2 && (type != None) && (fmt == 32) && (nitems == 1))
  {	
  Window *ptemp = (Window *)remoteVSTServerInstance->data2;
  if(ptemp)
  remoteVSTServerInstance->proxyptr = *ptemp;
  XFree (remoteVSTServerInstance->data2);
  }
  }
  }
  
  if(remoteVSTServerInstance->pwindow)
  {
  memset(&xdndclient, sizeof(xdndclient), 0);
  xdndclient.type = ClientMessage;
  xdndclient.display = remoteVSTServerInstance->display;
  xdndclient.window = remoteVSTServerInstance->pwindow;
  xdndclient.message_type = remoteVSTServerInstance->XdndLeave;
  xdndclient.format=32;
  xdndclient.data.l[0] = remoteVSTServerInstance->drag_win;
  xdndclient.data.l[1] = 0;
  xdndclient.data.l[2] = 0;
  xdndclient.data.l[3] = 0;
  xdndclient.data.l[4] = 0;

  if(remoteVSTServerInstance->proxyptr)
  XSendEvent(remoteVSTServerInstance->display, remoteVSTServerInstance->proxyptr, False, NoEventMask, (XEvent*)&xdndclient);
  else
  XSendEvent(remoteVSTServerInstance->display, remoteVSTServerInstance->pwindow, False, NoEventMask, (XEvent*)&xdndclient);
  XFlush(remoteVSTServerInstance->display);
  }
							
  memset(&xdndclient, sizeof(xdndclient), 0);
  xdndclient.type = ClientMessage;
  xdndclient.display = remoteVSTServerInstance->display;
  xdndclient.window = remoteVSTServerInstance->window;
  xdndclient.message_type = remoteVSTServerInstance->XdndEnter;
  xdndclient.format=32;
  xdndclient.data.l[0] = remoteVSTServerInstance->drag_win;
  xdndclient.data.l[1] = min(5, remoteVSTServerInstance->xdndversion) << 24;
  xdndclient.data.l[2] = remoteVSTServerInstance->atomlist;
  xdndclient.data.l[3] = remoteVSTServerInstance->atomplain;
  xdndclient.data.l[4] = remoteVSTServerInstance->atomstring;

  if(remoteVSTServerInstance->proxyptr)
  XSendEvent(remoteVSTServerInstance->display, remoteVSTServerInstance->proxyptr, False, NoEventMask, (XEvent*)&xdndclient);
  else
  XSendEvent(remoteVSTServerInstance->display, remoteVSTServerInstance->window, False, NoEventMask, (XEvent*)&xdndclient);
  XFlush(remoteVSTServerInstance->display);
  remoteVSTServerInstance->dndaccept = 0;
  }
			
  if(remoteVSTServerInstance->xdndversion != -1)
  {
  memset(&xdndclient, sizeof(xdndclient), 0);
  xdndclient.type = ClientMessage;
  xdndclient.display = remoteVSTServerInstance->display;
  xdndclient.window = remoteVSTServerInstance->window;
  xdndclient.message_type = remoteVSTServerInstance->XdndPosition;
  xdndclient.format=32;
  xdndclient.data.l[0] = remoteVSTServerInstance->drag_win;
  xdndclient.data.l[1] = 0;
  xdndclient.data.l[2] = (x <<16) | y;
  xdndclient.data.l[3] = CurrentTime;
  xdndclient.data.l[4] = remoteVSTServerInstance->XdndActionCopy;
  
  if(remoteVSTServerInstance->proxyptr)
  XSendEvent(remoteVSTServerInstance->display, remoteVSTServerInstance->proxyptr, False, NoEventMask, (XEvent*)&xdndclient);
  else
  XSendEvent(remoteVSTServerInstance->display, remoteVSTServerInstance->window, False, NoEventMask, (XEvent*)&xdndclient);
  XFlush(remoteVSTServerInstance->display);
			}			
  remoteVSTServerInstance->pwindow = remoteVSTServerInstance->window;	
  }
  }
                
  while(XPending(remoteVSTServerInstance->display)) 
  {
  XNextEvent(remoteVSTServerInstance->display, &e);
      
  switch (e.type) 
  {
  case SelectionRequest:
  {
  memset(&xdndselect, sizeof(xdndselect), 0); 
  xdndselect.xselection.type = SelectionNotify;
  xdndselect.xselection.requestor = e.xselectionrequest.requestor;
  xdndselect.xselection.selection = e.xselectionrequest.selection;
  xdndselect.xselection.target = e.xselectionrequest.target; 
  xdndselect.xselection.property = e.xselectionrequest.property;   
  xdndselect.xselection.time = e.xselectionrequest.time;   
 
  XChangeProperty(remoteVSTServerInstance->display, e.xselectionrequest.requestor, e.xselectionrequest.property, e.xselectionrequest.target, 8, PropModeReplace, (unsigned char*)remoteVSTServerInstance->dragfilelist.c_str(), strlen(remoteVSTServerInstance->dragfilelist.c_str()));

  XSendEvent(remoteVSTServerInstance->display, e.xselectionrequest.requestor, False, NoEventMask, &xdndselect);
  XFlush (remoteVSTServerInstance->display);      
  }
  break;
            
  case ClientMessage:
  if(e.xclient.message_type == remoteVSTServerInstance->XdndStatus)
  {
  remoteVSTServerInstance->dndaccept = e.xclient.data.l[1] & 1;
  }
  else if(e.xclient.message_type == remoteVSTServerInstance->XdndFinished)
  {
  remoteVSTServerInstance->dndfinish = 1;
  }
  break;
       
  default:
  break;
  }   
  }   

  if(remoteVSTServerInstance->dndfinish == 1)
  {
  XFlush(remoteVSTServerInstance->display); 
  remoteVSTServerInstance->dodragwin = 0;
  }
  else
  {
  if((remoteVSTServerInstance->windowok == 0) && (remoteVSTServerInstance->dropdone == 0) && !GetAsyncKeyState(VK_ESCAPE) && remoteVSTServerInstance->pwindow && (remoteVSTServerInstance->xdndversion != -1))
  {
  if(remoteVSTServerInstance->dndaccept == 1) 
  {
  memset(&xdndclient, sizeof(xdndclient), 0);
  xdndclient.type = ClientMessage;
  xdndclient.display = remoteVSTServerInstance->display;
  xdndclient.window = remoteVSTServerInstance->pwindow;
  xdndclient.message_type = remoteVSTServerInstance->XdndDrop;
  xdndclient.format=32;
  xdndclient.data.l[0] = remoteVSTServerInstance->drag_win;
  xdndclient.data.l[1] = 0;
  xdndclient.data.l[2] = CurrentTime;
  xdndclient.data.l[3] = 0;
  xdndclient.data.l[4] = 0;

  if(remoteVSTServerInstance->proxyptr)
  XSendEvent(remoteVSTServerInstance->display, remoteVSTServerInstance->proxyptr, False, NoEventMask, (XEvent*)&xdndclient);
  else
  XSendEvent(remoteVSTServerInstance->display, remoteVSTServerInstance->pwindow, False, NoEventMask, (XEvent*)&xdndclient);
  XFlush(remoteVSTServerInstance->display);
 
  memset(&xdndclient, sizeof(xdndclient), 0);
  xdndclient.type = ClientMessage;
  xdndclient.display = remoteVSTServerInstance->display;
  xdndclient.window = remoteVSTServerInstance->pwindow;
  xdndclient.message_type = remoteVSTServerInstance->XdndLeave;
  xdndclient.format=32;
  xdndclient.data.l[0] = remoteVSTServerInstance->drag_win;
  xdndclient.data.l[1] = 0;
  xdndclient.data.l[2] = 0;
  xdndclient.data.l[3] = 0;
  xdndclient.data.l[4] = 0;

  if(remoteVSTServerInstance->proxyptr)
  XSendEvent(remoteVSTServerInstance->display, remoteVSTServerInstance->proxyptr, False, NoEventMask, (XEvent*)&xdndclient);
  else
  XSendEvent(remoteVSTServerInstance->display, remoteVSTServerInstance->pwindow, False, NoEventMask, (XEvent*)&xdndclient);
  XFlush(remoteVSTServerInstance->display); 

  remoteVSTServerInstance->dropdone = 1;
  }
  else
  {
  if(remoteVSTServerInstance->pwindow)
  {                 
  memset(&xdndclient, sizeof(xdndclient), 0);
  xdndclient.type = ClientMessage;
  xdndclient.display = remoteVSTServerInstance->display;
  xdndclient.window = remoteVSTServerInstance->pwindow;
  xdndclient.message_type = remoteVSTServerInstance->XdndLeave;
  xdndclient.format=32;
  xdndclient.data.l[0] = remoteVSTServerInstance->drag_win;
  xdndclient.data.l[1] = 0;
  xdndclient.data.l[2] = 0;
  xdndclient.data.l[3] = 0;
  xdndclient.data.l[4] = 0;

  if(remoteVSTServerInstance->proxyptr)
  XSendEvent(remoteVSTServerInstance->display, remoteVSTServerInstance->proxyptr, False, NoEventMask, (XEvent*)&xdndclient);
  else
  XSendEvent(remoteVSTServerInstance->display, remoteVSTServerInstance->pwindow, False, NoEventMask, (XEvent*)&xdndclient);
  XFlush(remoteVSTServerInstance->display); 
				
  remoteVSTServerInstance->dodragwin = 0;                   
  }     
  }
  }
  else if((remoteVSTServerInstance->windowok == 0) && GetAsyncKeyState(VK_ESCAPE) && remoteVSTServerInstance->pwindow && (remoteVSTServerInstance->xdndversion != -1))                   
  {
  SetCursor(remoteVSTServerInstance->hCurs2);

  memset(&xdndclient, sizeof(xdndclient), 0);
  xdndclient.type = ClientMessage;
  xdndclient.display = remoteVSTServerInstance->display;
  xdndclient.window = remoteVSTServerInstance->pwindow;
  xdndclient.message_type = remoteVSTServerInstance->XdndLeave;
  xdndclient.format=32;
  xdndclient.data.l[0] = remoteVSTServerInstance->drag_win;
  xdndclient.data.l[1] = 0;
  xdndclient.data.l[2] = 0;
  xdndclient.data.l[3] = 0;
  xdndclient.data.l[4] = 0;

  if(remoteVSTServerInstance->proxyptr)
  XSendEvent(remoteVSTServerInstance->display, remoteVSTServerInstance->proxyptr, False, NoEventMask, (XEvent*)&xdndclient);
  else
  XSendEvent(remoteVSTServerInstance->display, remoteVSTServerInstance->pwindow, False, NoEventMask, (XEvent*)&xdndclient);
  XFlush(remoteVSTServerInstance->display); 

  remoteVSTServerInstance->dodragwin = 0;
  }
  }   
  }
  }
  
   usleep(1000);
  }
 
  // param.sched_priority = 0;
  // (void)sched_setscheduler(0, SCHED_OTHER, &param);
  remoteVSTServerInstance->getfin = 1;
  ExitThread(0);
  return 0;
}
#endif

DWORD WINAPI ParThreadMain(LPVOID parameter) {
  /*
      struct sched_param param;
      param.sched_priority = 1;

      int result = sched_setscheduler(0, SCHED_FIFO, &param);

      if (result < 0)
      {
          perror("Failed to set realtime priority for audio thread");
      }
  */
  while (!remoteVSTServerInstance->exiting) {
    remoteVSTServerInstance->dispatchPar(5);
  }
  // param.sched_priority = 0;
  // (void)sched_setscheduler(0, SCHED_OTHER, &param);
  remoteVSTServerInstance->parfin = 1;
  ExitThread(0);
  return 0;
}

DWORD WINAPI ControlThreadMain(LPVOID parameter) {
  /*
      struct sched_param param;
      param.sched_priority = 1;

      int result = sched_setscheduler(0, SCHED_FIFO, &param);

      if (result < 0)
      {
          perror("Failed to set realtime priority for audio thread");
      }
  */
  while (!remoteVSTServerInstance->exiting) {
    remoteVSTServerInstance->dispatchControl2(5);
  }
  // param.sched_priority = 0;
  // (void)sched_setscheduler(0, SCHED_OTHER, &param);
  remoteVSTServerInstance->confin = 1;
  ExitThread(0);
  return 0;
}

RemoteVSTServer::RemoteVSTServer(std::string fileIdentifiers,
                                 std::string fallbackName)
    : RemotePluginServer(fileIdentifiers), m_name(fallbackName), m_maker(""),
      setprogrammiss(0), hostreaper(0), wavesthread(1),
#ifdef EMBED
      winm(0),
/*
#ifdef TRACKTIONWM
hosttracktion(0),
#endif
* */
#endif
      haveGui(true), exiting(false), effectrun(false), inProcessThread(false),
      guiVisible(false), parfin(0), audfin(0), getfin(0), confin(0), guiupdate(0),
      guiupdatecount(0), guiresizewidth(500), guiresizeheight(200), melda(0),
      hWnd(0), vst2wrap(0), factory(0), mpluginptr(&mplugin), vst2uid(0),
#ifdef DRAGWIN
dodragwin(0), drag_win(0), pwindow(0), data(0), data2(0), proxyptr(0), prevx(-1), prevy(-1), winehwnd(0),
#endif      
#ifdef PCACHE
      numpars(0),
#endif                  
      hidegui(0) {
#ifdef EMBED
  /*
  winm = new winmessage;
  if(!winm)
  starterror = 1;
  */
  winm = &winm2;
#endif
}

std::string RemoteVSTServer::getName() {
  char buffer[512];
  memset(buffer, 0, sizeof(buffer));
  vst2wrap->getEffectName(buffer);
  if (buffer[0]) {
    m_name = buffer;
    if (strlen(buffer) < (kVstMaxEffectNameLen - 7))
      m_name = m_name + " [vst3]";
  }
  return m_name;
}

std::string RemoteVSTServer::getMaker() {
  char buffer[512];
  memset(buffer, 0, sizeof(buffer));
  vst2wrap->getVendorString(buffer);
  if (buffer[0])
    m_maker = buffer;
  return m_maker;
}

void RemoteVSTServer::EffectOpen(ShmControl *m_shmControlptr) {
  if (debugLevel > 0)
    cerr << "dssi-vst-server[1]: opening plugin" << endl;

  vst2wrap->suspend();

  vst2wrap->setBlockSize(bufferSize);
  vst2wrap->setSampleRate(sampleRate);

  char buffer[512];
  memset(buffer, 0, sizeof(buffer));

  string buffer2 = getMaker();
  strcpy(buffer, buffer2.c_str());

  /*
      if (strncmp(buffer, "Guitar Rig 5", 12) == 0)
          setprogrammiss = 1;
      if (strncmp(buffer, "T-Rack", 6) == 0)
          setprogrammiss = 1;
  */

  if (strcmp("MeldaProduction", buffer) == 0) {
    melda = 1;
  }

  /*
      if (strncmp(buffer, "IK", 2) == 0)
          setprogrammiss = 1;
  */

#ifdef TRACKTIONWM

  offset.x = 0;
  offset.y = 0;

  memset(&wclass2, 0, sizeof(WNDCLASSEX));
  wclass2.cbSize = sizeof(WNDCLASSEX);
  wclass2.style = 0;
  // CS_HREDRAW | CS_VREDRAW;
  wclass2.lpfnWndProc = MainProc2;
  wclass2.cbClsExtra = 0;
  wclass2.cbWndExtra = 0;
  wclass2.hInstance = GetModuleHandle(0);
  wclass2.hIcon = LoadIcon(GetModuleHandle(0), APPLICATION_CLASS_NAME2);
  wclass2.hCursor = LoadCursor(0, IDI_APPLICATION);
  // wclass2.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
  wclass2.lpszMenuName = "MENU_DSSI_VST2";
  wclass2.lpszClassName = APPLICATION_CLASS_NAME2;
  wclass2.hIconSm = 0;

  if (!RegisterClassEx(&wclass2)) {
    cerr << "dssi-vst-server: ERROR: Failed to register Windows application "
            "class!\n"
         << endl;
    haveGui = false;
  }

  RECT offsetcl, offsetwin;

  HWND hWnd2 = CreateWindow(APPLICATION_CLASS_NAME2, "LinVst", WS_CAPTION, 0, 0,
                            200, 200, 0, 0, GetModuleHandle(0), 0);
  if (hWnd2)
    GetClientRect(hWnd2, &offsetcl);
  GetWindowRect(hWnd2, &offsetwin);
  DestroyWindow(hWnd2);

  offset.x = (offsetwin.right - offsetwin.left) - offsetcl.right;
  offset.y = (offsetwin.bottom - offsetwin.top) - offsetcl.bottom;

  UnregisterClassA(APPLICATION_CLASS_NAME2, GetModuleHandle(0));
#endif

  struct amessage am;

  //      am.pcount = m_plugin->numPrograms;
  //     am.parcount = m_plugin->numParams;
  am.incount = getInputCount();
  am.outcount = getOutputCount();
  am.delay = getinitialDelay();
  /*
  #ifndef DOUBLEP
          am.flags = m_plugin->flags;
          am.flags &= ~effFlagsCanDoubleReplacing;
  #else
          am.flags = m_plugin->flags;
  #endif
  */

  if ((am.incount != m_numInputs) || (am.outcount != m_numOutputs) ||
      (am.delay != m_delay)) {
    memcpy(remoteVSTServerInstance->m_shmControl->amptr, &am, sizeof(am));
    remoteVSTServerInstance->m_shmControl->ropcode =
        (RemotePluginOpcode)audioMasterIOChanged;
    remoteVSTServerInstance->waitForServer(
        remoteVSTServerInstance->m_shmControl);
  }

  vst2wrap->resume();

  effectrun = true;
}

RemoteVSTServer::~RemoteVSTServer() {
  if (effectrun == true) {
    vst2wrap->suspend();
  }

  if (vst2wrap)
    delete vst2wrap;

  if (factory)
    factory->release();
}

void RemoteVSTServer::process(float **inputs, float **outputs,
                              int sampleFrames) {
#ifdef PCACHE
/*
  struct ParamState {
  float value;
  float valueupdate;
  int changed;
  };
*/
   
   ParamState *pstate = (ParamState*)remoteVSTServerInstance->m_shm5; 
        
   if(numpars > 0)
   {
   for(int idx=0;idx<numpars;idx++)
   {
   //sched_yield();
   if(pstate[idx].changed == 1)
   {
   setParameter(idx, pstate[idx].valueupdate);
   pstate[idx].value = pstate[idx].valueupdate; 
   pstate[idx].changed = 0; 
   }    
   } 
   }            
#endif

  inProcessThread = true;
  vst2wrap->processReplacing(inputs, outputs, sampleFrames);
  inProcessThread = false;
}

#ifdef DOUBLEP
void RemoteVSTServer::processdouble(double **inputs, double **outputs,
                                    int sampleFrames) {
#ifdef PCACHE
/*
  struct ParamState {
  float value;
  float valueupdate;
  int changed;
  };
*/
   
   ParamState *pstate = (ParamState*)remoteVSTServerInstance->m_shm5; 
        
   if(numpars > 0)
   {
   for(int idx=0;idx<numpars;idx++)
   {
   //sched_yield();
   if(pstate[idx].changed == 1)
   {
   setParameter(idx, pstate[idx].valueupdate);
   pstate[idx].value = pstate[idx].valueupdate; 
   pstate[idx].changed = 0; 
   }    
   } 
   }            
#endif

  inProcessThread = true;
  vst2wrap->processDoubleReplacing(inputs, outputs, sampleFrames);
  inProcessThread = false;
}

bool RemoteVSTServer::setPrecision(int value) {
  bool retval;

  retval = vst2wrap->setProcessPrecision(value);

  return retval;
}
#endif

/*
#ifdef VESTIGE
bool RemoteVSTServer::getOutProp(int index, ShmControl *m_shmControlptr)
{
char ptr[sizeof(vinfo)];
bool retval;

        retval = vst2wrap->getOutputProperties(index, &ptr);
        memcpy(m_shmControlptr->vret, ptr, sizeof(vinfo));
        return retval;
}

bool RemoteVSTServer::getInProp(int index, ShmControl *m_shmControlptr)
{
char ptr[sizeof(vinfo)];
bool retval;

        retval = vst2wrap->getInputProperties(index, &ptr);
        memcpy(m_shmControlptr->vret, ptr, sizeof(vinfo));
        return retval;
}
#else
bool RemoteVSTServer::getInProp(int index, ShmControl *m_shmControlptr)
{
VstPinProperties ptr;
bool retval;

        retval = vst2wrap->getInputProperties(index, &ptr);
        memcpy(m_shmControlptr->vpin, &ptr, sizeof(VstPinProperties));
        return retval;
}

bool RemoteVSTServer::getOutProp(int index, ShmControl *m_shmControlptr)
{
VstPinProperties ptr;
bool retval;

        retval = vst2wrap->getOutputProperties(index, &ptr);
        memcpy(m_shmControlptr->vpin, &ptr, sizeof(VstPinProperties));
        return retval;
}
#endif
*/

#ifndef VESTIGE
bool RemoteVSTServer::getInProp(int index, ShmControl *m_shmControlptr) {
  VstPinProperties ptr;
  bool retval;

  retval = vst2wrap->getInputProperties(index, &ptr);
  memcpy(m_shmControlptr->vpin, &ptr, sizeof(VstPinProperties));
  return retval;
}

bool RemoteVSTServer::getOutProp(int index, ShmControl *m_shmControlptr) {
  VstPinProperties ptr;
  bool retval;

  retval = vst2wrap->getOutputProperties(index, &ptr);
  memcpy(m_shmControlptr->vpin, &ptr, sizeof(VstPinProperties));
  return retval;
}
#endif

#ifdef MIDIEFF
bool RemoteVSTServer::getMidiKey(int index, ShmControl *m_shmControlptr) {
  MidiKeyName ptr;
  bool retval;

  retval = m_plugin->dispatcher(m_plugin, effGetMidiKeyName, index, 0, &ptr, 0);
  memcpy(m_shmControlptr->midikey, &ptr, sizeof(MidiKeyName));
  return retval;
}

bool RemoteVSTServer::getMidiProgName(int index, ShmControl *m_shmControlptr) {
  MidiProgramName ptr;
  bool retval;

  retval =
      m_plugin->dispatcher(m_plugin, effGetMidiProgramName, index, 0, &ptr, 0);
  memcpy(m_shmControlptr->midiprogram, &ptr, sizeof(MidiProgramName));
  return retval;
}

bool RemoteVSTServer::getMidiCurProg(int index, ShmControl *m_shmControlptr) {
  MidiProgramName ptr;
  bool retval;

  retval = m_plugin->dispatcher(m_plugin, effGetCurrentMidiProgram, index, 0,
                                &ptr, 0);
  memcpy(m_shmControlptr->midiprogram, &ptr, sizeof(MidiProgramName));
  return retval;
}

bool RemoteVSTServer::getMidiProgCat(int index, ShmControl *m_shmControlptr) {
  MidiProgramCategory ptr;
  bool retval;

  retval = m_plugin->dispatcher(m_plugin, effGetMidiProgramCategory, index, 0,
                                &ptr, 0);
  memcpy(m_shmControlptr->midiprogramcat, &ptr, sizeof(MidiProgramCategory));
  return retval;
}

bool RemoteVSTServer::getMidiProgCh(int index, ShmControl *m_shmControlptr) {
  bool retval;

  retval =
      m_plugin->dispatcher(m_plugin, effHasMidiProgramsChanged, index, 0, 0, 0);
  return retval;
}

bool RemoteVSTServer::setSpeaker(ShmControl *m_shmControlptr) {
  VstSpeakerArrangement ptr;
  VstSpeakerArrangement value;
  bool retval;

  memcpy(&ptr, m_shmControlptr->vstspeaker2, sizeof(VstSpeakerArrangement));
  memcpy(&value, m_shmControlptr->vstspeaker, sizeof(VstSpeakerArrangement));
  retval = m_plugin->dispatcher(m_plugin, effSetSpeakerArrangement, 0,
                                (VstIntPtr)&value, &ptr, 0);
  return retval;
}

bool RemoteVSTServer::getSpeaker(ShmControl *m_shmControlptr) {
  VstSpeakerArrangement ptr;
  VstSpeakerArrangement value;
  bool retval;

  retval = m_plugin->dispatcher(m_plugin, effSetSpeakerArrangement, 0,
                                (VstIntPtr)&value, &ptr, 0);
  memcpy(m_shmControlptr->vstspeaker2, &ptr, sizeof(VstSpeakerArrangement));
  memcpy(m_shmControlptr->vstspeaker, &value, sizeof(VstSpeakerArrangement));
  return retval;
}
#endif

#ifdef CANDOEFF
bool RemoteVSTServer::getEffCanDo(std::string ptr) {
  if (m_plugin->dispatcher(m_plugin, effCanDo, 0, 0, (char *)ptr.c_str(), 0))
    return true;
  else
    return false;
}
#endif

int RemoteVSTServer::getEffInt(int opcode, int value) {
  int retval;

  if (opcode == effGetPlugCategory) {
    if (vst2wrap->synth == true)
      return 2;
    else
      return 1;
  }

  if (opcode == effMainsChanged) {
    if (value == 0)
      vst2wrap->suspend();
    if (value == 1)
      vst2wrap->resume();
    return 0;
  }

  if (opcode == effSetProcessPrecision) {
    retval = vst2wrap->setProcessPrecision(value);
    return retval;
  }
}

void RemoteVSTServer::effDoVoid(int opcode) {
  if (opcode == 78345432) {
    //        hostreaper = 1;
    return;
  }

  if (opcode == effClose) {
    // usleep(500000);
    waitForServerexit();
    terminate();
    return;
  }

  if (opcode == effStartProcess)
    vst2wrap->startProcess();

  if (opcode == effStopProcess)
    vst2wrap->stopProcess();
}

int RemoteVSTServer::effDoVoid2(int opcode, int index, int value, float opt) {
  int ret;

  ret = 0;
  /*
      if(opcode == hidegui2)
      {
      hidegui = 1;
  #ifdef XECLOSE
      while(hidegui == 1)
      {
      sched_yield();
      }
  #endif
      }
      else
   */

#ifdef EMBED
#ifdef TRACKTIONWM
  if (opcode == 67584930) {
    hosttracktion = 1;
    return offset.y;
  }
  if (opcode == 67584931) {
    return offset.x;
  }  
#endif
#endif
}

std::string RemoteVSTServer::getEffString(int opcode, int index) {
  char name[512];
  memset(name, 0, sizeof(name));

  if (opcode == effGetParamDisplay)
    vst2wrap->getParameterDisplay(index, name);

  if (opcode == effGetParamLabel)
    vst2wrap->getParameterLabel(index, name);

  return name;
}

void RemoteVSTServer::setBufferSize(int sz) {
  if (bufferSize != sz) {
    vst2wrap->suspend();
    vst2wrap->setBlockSize(sz);
    vst2wrap->resume();
    bufferSize = sz;
  }
}

void RemoteVSTServer::setSampleRate(int sr) {
  if (sampleRate != sr) {
    vst2wrap->suspend();
    vst2wrap->setSampleRate(sr);
    vst2wrap->resume();
    sampleRate = sr;
  }
}

void RemoteVSTServer::reset() {
  cerr << "dssi-vst-server[1]: reset" << endl;

  //  m_plugin->dispatcher(m_plugin, effMainsChanged, 0, 0, NULL, 0);
  //  m_plugin->dispatcher(m_plugin, effMainsChanged, 0, 1, NULL, 0);
}

void RemoteVSTServer::terminate() {
  exiting = true;

  //  cerr << "RemoteVSTServer::terminate: setting exiting flag" << endl;
}

std::string RemoteVSTServer::getParameterName(int p) {
  char name[512];
  memset(name, 0, sizeof(name));
  vst2wrap->getParameterName(p, name);
  return name;
}

std::string RemoteVSTServer::getParameterLabel(int p) {
  char name[512];
  memset(name, 0, sizeof(name));
  vst2wrap->getParameterLabel(p, name);
  return name;
}

std::string RemoteVSTServer::getParameterDisplay(int p) {
  char name[512];
  memset(name, 0, sizeof(name));
  vst2wrap->getParameterDisplay(p, name);
  return name;
}

void RemoteVSTServer::setParameter(int p, float v) {
  vst2wrap->setParameter(p, v);
}

float RemoteVSTServer::getParameter(int p) { return vst2wrap->getParameter(p); }

void RemoteVSTServer::getParameters(int p0, int pn, float *v) {}

int RemoteVSTServer::getProgramNameIndexed(int p, char *name) {
  if (debugLevel > 1)
    cerr << "dssi-vst-server[2]: getProgramName(" << p << ")" << endl;

  int retval = 0;
  char nameret[512];
  memset(nameret, 0, sizeof(nameret));
  retval = vst2wrap->getProgramNameIndexed(0, p, nameret);
  strcpy(name, nameret);
  return retval;
}

std::string RemoteVSTServer::getProgramName() {
  if (debugLevel > 1)
    cerr << "dssi-vst-server[2]: getProgramName()" << endl;

  char name[512];
  memset(name, 0, sizeof(name));
  vst2wrap->getProgramName(name);
  return name;
}

void RemoteVSTServer::setCurrentProgram(int p) {
  if (debugLevel > 1)
    cerr << "dssi-vst-server[2]: setCurrentProgram(" << p << ")" << endl;

  if (p < vst2wrap->numprograms)
    vst2wrap->setProgram(p);
}

bool RemoteVSTServer::warn(std::string warning) {
  if (hWnd)
    MessageBox(hWnd, warning.c_str(), "Error", 0);
  return true;
}

void RemoteVSTServer::showGUI(ShmControl *m_shmControlptr) {
#ifdef EMBED
  winm->handle = 0;
  winm->width = 0;
  winm->height = 0;
  winm->winerror = 0;
#endif

  if (guiVisible)
    return;

  if (haveGui == false)
    return;

  memset(&wclass, 0, sizeof(WNDCLASSEX));
  wclass.cbSize = sizeof(WNDCLASSEX);
  wclass.style = 0;
  // CS_HREDRAW | CS_VREDRAW;
  wclass.lpfnWndProc = MainProc;
  wclass.cbClsExtra = 0;
  wclass.cbWndExtra = 0;
  wclass.hInstance = GetModuleHandle(0);
  wclass.hIcon = LoadIcon(GetModuleHandle(0), APPLICATION_CLASS_NAME);
  wclass.hCursor = LoadCursor(0, IDI_APPLICATION);
  // wclass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
  wclass.lpszMenuName = "MENU_DSSI_VST";
  wclass.lpszClassName = APPLICATION_CLASS_NAME;
  wclass.hIconSm = 0;

  if (!RegisterClassEx(&wclass)) {
    cerr << "dssi-vst-server: ERROR: Failed to window class!\n" << endl;
#ifdef EMBED
    winm->winerror = 1;
    memcpy(m_shmControlptr->wret, winm, sizeof(winmessage));
#endif
    return;
  }

#ifdef EMBED
#ifdef EMBEDDRAG
  hWnd = CreateWindowEx(WS_EX_TOOLWINDOW | WS_EX_ACCEPTFILES,
                        APPLICATION_CLASS_NAME, "LinVst", WS_POPUP, 0, 0, 200,
                        200, 0, 0, GetModuleHandle(0), 0);
#else
  hWnd = CreateWindowEx(WS_EX_TOOLWINDOW, APPLICATION_CLASS_NAME, "LinVst",
                        WS_POPUP, 0, 0, 200, 200, 0, 0, GetModuleHandle(0), 0);
#endif
  if (!hWnd) {
    cerr << "dssi-vst-server: ERROR: Failed to create window!\n" << endl;
    winm->winerror = 1;
    UnregisterClassA(APPLICATION_CLASS_NAME, GetModuleHandle(0));
    memcpy(m_shmControlptr->wret, winm, sizeof(winmessage));
    return;
  }
    
  SetWindowPos(hWnd, HWND_TOP, GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), 200, 200, 0);    

  rect = 0;

  vst2wrap->editor->getRect(&rect);
  vst2wrap->editor->open(hWnd);
  vst2wrap->editor->getRect(&rect);

  if (!rect) {
    cerr << "dssi-vst-server: ERROR: Plugin failed to report window size\n"
         << endl;
    if (hWnd)
      DestroyWindow(hWnd);
    UnregisterClassA(APPLICATION_CLASS_NAME, GetModuleHandle(0));
    winm->winerror = 1;
    memcpy(m_shmControlptr->wret, winm, sizeof(winmessage));
    return;
  }
#ifdef TRACKTIONWM
  if (hosttracktion == 1) {
    // if(GetSystemMetrics(SM_CMONITORS) > 1)
    SetWindowPos(hWnd, HWND_TOP, GetSystemMetrics(SM_XVIRTUALSCREEN) + offset.x,
                 GetSystemMetrics(SM_YVIRTUALSCREEN) + offset.y,
                 rect->right - rect->left, rect->bottom - rect->top, 0);
    // else
    // SetWindowPos(hWnd, HWND_TOP, offset.x, offset.y, rect->right -
    // rect->left, rect->bottom - rect->top, 0);
  } else {
    // if(GetSystemMetrics(SM_CMONITORS) > 1)
    SetWindowPos(hWnd, HWND_TOP, GetSystemMetrics(SM_XVIRTUALSCREEN),
                 GetSystemMetrics(SM_YVIRTUALSCREEN), rect->right - rect->left,
                 rect->bottom - rect->top, 0);
    // else
    // SetWindowPos(hWnd, HWND_TOP, 0, 0, rect->right - rect->left, rect->bottom
    // - rect->top, 0);
  }
#else
  // if(GetSystemMetrics(SM_CMONITORS) > 1)
  SetWindowPos(hWnd, HWND_TOP, GetSystemMetrics(SM_XVIRTUALSCREEN),
               GetSystemMetrics(SM_YVIRTUALSCREEN), rect->right - rect->left,
               rect->bottom - rect->top, 0);
  // else
  // SetWindowPos(hWnd, HWND_TOP, 0, 0, rect->right - rect->left, rect->bottom -
  // rect->top, 0);
#endif
  handlewin = 0;

  winm->width = rect->right - rect->left;
  winm->height = rect->bottom - rect->top;
  handlewin = GetPropA(hWnd, "__wine_x11_whole_window");
  winm->handle = (long int)handlewin;
  memcpy(m_shmControlptr->wret, winm, sizeof(winmessage));
#else
#ifdef DRAG
  hWnd = CreateWindowEx(WS_EX_ACCEPTFILES, APPLICATION_CLASS_NAME, "LinVst",
                        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
                        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                        CW_USEDEFAULT, 0, 0, GetModuleHandle(0), 0);
#else
  hWnd = CreateWindow(APPLICATION_CLASS_NAME, "LinVst",
                      WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
                      CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                      CW_USEDEFAULT, 0, 0, GetModuleHandle(0), 0);
#endif
  if (!hWnd) {
    cerr << "dssi-vst-server: ERROR: Failed to create window!\n" << endl;
    UnregisterClassA(APPLICATION_CLASS_NAME, GetModuleHandle(0));
    return;
  }

  SetWindowText(hWnd, m_name.c_str());

  rect = 0;

  vst2wrap->editor->getRect(&rect);
  vst2wrap->editor->open(hWnd);
  vst2wrap->editor->getRect(&rect);

  if (!rect) {
    cerr << "dssi-vst-server: ERROR: Plugin failed to report window size\n"
         << endl;
    if (hWnd)
      DestroyWindow(hWnd);
    UnregisterClassA(APPLICATION_CLASS_NAME, GetModuleHandle(0));
    return;
  } else {
#ifdef WINONTOP
    SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, rect->right - rect->left + 6,
                 rect->bottom - rect->top + 25, SWP_NOMOVE);
#else
    SetWindowPos(hWnd, HWND_TOP, 0, 0, rect->right - rect->left + 6,
                 rect->bottom - rect->top + 25, SWP_NOMOVE);
#endif

    guiVisible = true;
    ShowWindow(hWnd, SW_SHOWNORMAL);
    UpdateWindow(hWnd);
  }
#endif

  guiresizewidth = rect->right - rect->left;
  guiresizeheight = rect->bottom - rect->top;
}

void RemoteVSTServer::hideGUI2() {
  hidegui = 1;
  //#ifdef XECLOSE
  while (hidegui == 1) {
    sched_yield();
  }
  //#endif
}

void RemoteVSTServer::hideGUI() {
#ifdef EMBED
  if (haveGui == false) {
    winm->handle = 0;
    winm->width = 0;
    winm->height = 0;
    winm->winerror = 0;
    guiVisible = false;
    hidegui = 0;
    return;
  }
#endif

  // if (!hWnd)
  // return;
  /*
      if ((haveGui == false) || (guiVisible == false))
      {
      hideguival = 0;
      return;
      }

  */

#ifndef EMBED
  ShowWindow(hWnd, SW_HIDE);
  UpdateWindow(hWnd);
#endif

  if (melda == 0) {
    vst2wrap->editor->close();
  }

  if (hWnd) {
    DestroyWindow(hWnd);
    UnregisterClassA(APPLICATION_CLASS_NAME, GetModuleHandle(0));
  }

  if (melda == 1) {
    vst2wrap->editor->close();
  }

  guiVisible = false;

  hidegui = 0;

  // if (!exiting)
  //    usleep(50000);
  
#ifdef DRAGWIN  
  if(g_hook)
  UnhookWinEvent(g_hook); 

  if(drag_win && display)
  {
  XDestroyWindow(display, drag_win);
  drag_win = 0;  
  }
#endif    
}

#ifdef DRAGWIN 
void CALLBACK SysDragImage(HWINEVENTHOOK hook, DWORD event, HWND hWnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime)
{
typedef struct tagTrackerWindowInfo
{
IDataObject* dataObject;
IDropSource* dropSource;
} TrackerWindowInfo;

IEnumFORMATETC* ienum;
STGMEDIUM stgMedium;
FORMATETC fmt; 
HRESULT hr;
char* winedirpath;
char hit2[4096];
UINT numchars;
UINT numfiles;
ULONG numfmt;
int cfdrop;

  if(remoteVSTServerInstance->dodragwin == 1)
  return;

  if(remoteVSTServerInstance->windowok == 1)
  return;

  remoteVSTServerInstance->dodragwin = 0;
  remoteVSTServerInstance->pwindow = 0;          		
  remoteVSTServerInstance->window = 0;		
  remoteVSTServerInstance->xdndversion = -1;				
  remoteVSTServerInstance->data = 0;
  remoteVSTServerInstance->data2 = 0;			
  remoteVSTServerInstance->prevx = -1;
  remoteVSTServerInstance->prevy = -1;
  remoteVSTServerInstance->windowok = 0;
  remoteVSTServerInstance->dndfinish = 0;
  remoteVSTServerInstance->dndaccept = 0;
  remoteVSTServerInstance->dropdone = 0;
  remoteVSTServerInstance->proxyptr = 0;
  remoteVSTServerInstance->winehwnd = 0;
  
  cfdrop = 0;

  if(event == EVENT_OBJECT_CREATE && idObject == OBJID_WINDOW) 
  {
  if(!IsWindow(FindWindow(NULL , TEXT("TrackerWindow"))))
  return;
  TrackerWindowInfo *trackerInfo = (TrackerWindowInfo*)GetWindowLongPtrA(hWnd, 0);
  if(trackerInfo)
  {
  ienum = NULL;
  trackerInfo->dataObject->EnumFormatEtc(DATADIR_GET, &ienum);
  if (ienum) 
  {             
  while(ienum->Next(1, &fmt, &numfmt) == S_OK )
  {             
  if (trackerInfo->dataObject->QueryGetData(&fmt) == S_OK)
  {
  stgMedium = {0};
  hr = trackerInfo->dataObject->GetData(&fmt, &stgMedium);
       
  if(hr == S_OK)
  {  
  remoteVSTServerInstance->dragfilelist.clear();
  memset(hit2, 0, 4096);
    
  switch (stgMedium.tymed) 
  {
  case TYMED_HGLOBAL: 
  {   
  HGLOBAL gmem = stgMedium.hGlobal;
  if(gmem)
  {
  HDROP hdrop = (HDROP)GlobalLock(gmem);
        
  if(hdrop)
  {
  numfiles = DragQueryFileW((HDROP)hdrop, 0xFFFFFFFF, NULL, 0);

  WCHAR buffer[MAX_PATH];
  
  for(int i=0;i<numfiles;i++)
  {
  numchars = DragQueryFileW((HDROP)hdrop, i, buffer, MAX_PATH);
  
  if(numchars == 0)
  continue;
     
  winedirpath = wine_get_unix_file_name(buffer);
                        
  remoteVSTServerInstance->dragfilelist += "file://";
               
  if(realpath(winedirpath, hit2) != 0) 
  {
  remoteVSTServerInstance->dragfilelist += hit2;
    
  for(size_t pos = remoteVSTServerInstance->dragfilelist.find(' '); 
  pos != string::npos; 
  pos = remoteVSTServerInstance->dragfilelist.find(' ', pos))
  {
  remoteVSTServerInstance->dragfilelist.replace(pos, 1, "%20");
  }
  }
  else
  remoteVSTServerInstance->dragfilelist += winedirpath;
  remoteVSTServerInstance->dragfilelist += "\r"; 
  remoteVSTServerInstance->dragfilelist += "\n"; 
  }
  cfdrop = 1;     
  }
  GlobalUnlock(gmem);    
  } 
  }
  break;
       
  case TYMED_FILE:
  {
  winedirpath = wine_get_unix_file_name(stgMedium.lpszFileName);
       
  remoteVSTServerInstance->dragfilelist += "file://";
               
  if(realpath(winedirpath, hit2) != 0) 
  {
  remoteVSTServerInstance->dragfilelist += hit2;
    
  for (size_t pos = remoteVSTServerInstance->dragfilelist.find(' '); 
  pos != string::npos; 
  pos = remoteVSTServerInstance->dragfilelist.find(' ', pos))
  {
  remoteVSTServerInstance->dragfilelist.replace(pos, 1, "%20");
  }
  }
  else
  remoteVSTServerInstance->dragfilelist += winedirpath;
 
  remoteVSTServerInstance->dragfilelist += "\r"; 
  remoteVSTServerInstance->dragfilelist += "\n";      
  } 
  cfdrop = 1;           
  break;
             
  default:
  break;
  }
  
  if(stgMedium.pUnkForRelease)
  stgMedium.pUnkForRelease->Release();
  else
  ::ReleaseStgMedium(&stgMedium);
  } 	  
  }
  }  
  if (ienum)   
  ienum->Release(); 
  if(cfdrop != 0)
  remoteVSTServerInstance->dodragwin = 1;
  }       
  }
  }
}
#endif

/*

https://stackoverflow.com/questions/2947139/how-to-know-user-is-dragging-something-when-the-cursor-is-not-in-my-window?tab=active


When most of Drag&Drop operations start, system creates a feedback window with "SysDragImage" class. It's possible to catch the creation and destruction of this feedback window, and react in your application accordingly.

Here's a sample code (form class declaration is skipped to make it shorter):

procedure WinEventProc(hWinEventHook: THandle; event: DWORD;
  hwnd: HWND; idObject, idChild: Longint; idEventThread, dwmsEventTime: DWORD); stdcall;
var
  ClassName: string;
begin
  SetLength(ClassName, 255);
  SetLength(ClassName, GetClassName(hWnd, pchar(ClassName), 255));

  if pchar(ClassName) = 'SysDragImage' then
  begin
    if event = EVENT_OBJECT_CREATE then
      Form1.Memo1.Lines.Add('Drag Start')
    else
      Form1.Memo1.Lines.Add('Drag End');    
  end;
end;

procedure TForm1.FormCreate(Sender: TObject);
begin
  FEvent1 := SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_CREATE, 0, @WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
  FEvent2 := SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_DESTROY, 0, @WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
end;

procedure TForm1.FormDestroy(Sender: TObject);
begin
  UnhookWinEvent(FEvent1);
  UnhookWinEvent(FEvent2);
end;

The only issue here is when you press Escape right after beginning of the Drag & Drop, the system won't generate EVENT_OBJECT_DESTROY event. But this can easily be solved by starting timer on EVENT_OBJECT_CREATE, and periodically monitoing if the feedback windows is still alive.

*/

#ifdef EMBED
void RemoteVSTServer::openGUI() {
  if (haveGui == false) {
    guiVisible = false;
    return;
  }
  guiVisible = true;     
  ShowWindow(hWnd, SW_SHOWNORMAL);
  // ShowWindow(hWnd, SW_SHOW);
  UpdateWindow(hWnd);
  
#ifdef DRAGWIN  
  if(display)
  {
  XSetWindowAttributes attr = {0};  
  attr.event_mask = NoEventMask;

  drag_win = XCreateWindow(display, DefaultRootWindow(display), 0, 0, 1, 1, 0, 0, InputOutput, CopyFromParent, CWEventMask, &attr);

  if (drag_win) 
  {
  int version = 5;
  XChangeProperty(display, drag_win, XdndAware, XA_ATOM, 32, PropModeReplace, (unsigned char *)&version, 1);

  atomlist = XInternAtom(display, "text/uri-list", False);
  atomplain = XInternAtom(display, "text/plain", False);
  atomstring = XInternAtom(display, "audio/midi", False);
  
  if (!XSetSelectionOwner(display, XdndSelection, drag_win, CurrentTime))
  {
  if(drag_win && display)
  XDestroyWindow(display, drag_win);
  drag_win = 0;  
  return;
  }
                     
  g_hook = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_CREATE, NULL, SysDragImage, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);   
  }
  } 
#endif    
}
#endif

int RemoteVSTServer::processVstEvents() {
  int els;
  int *ptr;
  int sizeidx = 0;
  int size;
  VstEvents *evptr;  
  int retval;

  ptr = (int *)m_shm2;
  els = *ptr;
  sizeidx = sizeof(int);  

 // if (els > VSTSIZE)
 //   els = VSTSIZE;

  evptr = &vstev[0];
  evptr->numEvents = els;
  evptr->reserved = 0;

  for (int i = 0; i < els; i++) {
    VstEvent *bsize = (VstEvent *)&m_shm2[sizeidx];
    size = bsize->byteSize + (2 * sizeof(VstInt32));
    evptr->events[i] = bsize;
    sizeidx += size;
  }

  vst2wrap->processEvents(evptr);

  return 1;
}

void RemoteVSTServer::getChunk(ShmControl *m_shmControlptr) {
#ifdef CHUNKBUF
  int bnk_prg = m_shmControlptr->value;
  int sz = vst2wrap->getChunk((void **)&chunkptr, bnk_prg ? true : false);

  if (sz >= CHUNKSIZEMAX) {
    m_shmControlptr->retint = sz;
    return;
  } else {
    if (sz < CHUNKSIZEMAX)
      memcpy(m_shm3, chunkptr, sz);
    m_shmControlptr->retint = sz;
    return;
  }
#else
  void *ptr;
  int bnk_prg = m_shmControlptr->value;
  int sz = vst2wrap->getChunk((void **)&ptr, bnk_prg ? true : false);
  if (sz < CHUNKSIZEMAX)
    memcpy(m_shm3, ptr, sz);
  m_shmControlptr->retint = sz;
  return;
#endif
}

void RemoteVSTServer::setChunk(ShmControl *m_shmControlptr) {
#ifdef CHUNKBUF
  int sz = m_shmControlptr->value;
  if (sz >= CHUNKSIZEMAX) {
    int bnk_prg = m_shmControlptr->value2;
    void *ptr = chunkptr2;
    int r = vst2wrap->setChunk(ptr, sz, bnk_prg ? true : false);
    free(chunkptr2);
    m_shmControlptr->retint = r;
#ifdef PCACHE
    getParameterCount();  
#endif   
    return;
  } else {
    int bnk_prg = m_shmControlptr->value2;
    void *ptr = m_shm3;
    int r = vst2wrap->setChunk(ptr, sz, bnk_prg ? true : false);
    m_shmControlptr->retint = r;
#ifdef PCACHE
    getParameterCount(); 
#endif    
    return;
  }
#else
  int sz = m_shmControlptr->value;
  int bnk_prg = m_shmControlptr->value2;
  void *ptr = m_shm3;
  int r = vst2wrap->setChunk(ptr, sz, bnk_prg ? true : false);
  m_shmControlptr->retint = r;
#ifdef PCACHE
  getParameterCount();  
#endif 
  return;
#endif
}

void RemoteVSTServer::canBeAutomated(ShmControl *m_shmControlptr) {
  int param = m_shmControlptr->value;
  int r = vst2wrap->canParameterBeAutomated(param);
  m_shmControlptr->retint = r;
}

void RemoteVSTServer::getProgram(ShmControl *m_shmControlptr) {
  int r = vst2wrap->getProgram();
  m_shmControlptr->retint = r;
}

void RemoteVSTServer::waitForServer(ShmControl *m_shmControlptr) {
  fpost2(m_shmControlptr, &m_shmControlptr->runServer);

  if (fwait2(m_shmControlptr, &m_shmControlptr->runClient, 60000)) {
    if (m_inexcept == 0)
      RemotePluginClosedException();
  }
}

void RemoteVSTServer::waitForServerexit() {
  fpost(m_shmControl, &m_shmControl->runServer);
  fpost(m_shmControl, &m_shmControl->runClient);
}

#ifdef VESTIGE
VstIntPtr VESTIGECALLBACK hostCallback(AEffect *plugin, VstInt32 opcode,
                                       VstInt32 index, VstIntPtr value,
                                       void *ptr, float opt)
#else
VstIntPtr VSTCALLBACK hostCallback(AEffect *plugin, VstInt32 opcode,
                                   VstInt32 index, VstIntPtr value, void *ptr,
                                   float opt)
#endif
{
  VstIntPtr rv = 0;
  int retval = 0;

  if (remoteVSTServerInstance) {
    remoteVSTServerInstance->m_shmControlptr =
        remoteVSTServerInstance->m_shmControl;
  } else {
    switch (opcode) {
    case audioMasterVersion:
      if (debugLevel > 1)
        cerr << "dssi-vst-server[2]: audioMasterVersion requested" << endl;
      rv = 2400;
      break;
    default:
      if (debugLevel > 0)
        cerr << "dssi-vst-server[0]: unsupported audioMaster callback opcode "
             << opcode << endl;
      break;
    }

    return rv;
  }

  switch (opcode) {
  case audioMasterVersion:
    if (debugLevel > 1)
      cerr << "dssi-vst-server[2]: audioMasterVersion requested" << endl;
    rv = 2400;
    break;

  case audioMasterAutomate:
    //     plugin->setParameter(plugin, index, opt);
    if (remoteVSTServerInstance) {
      if (!remoteVSTServerInstance->exiting &&
          remoteVSTServerInstance->effectrun) {
        remoteVSTServerInstance->m_shmControlptr->ropcode =
            (RemotePluginOpcode)opcode;
        remoteVSTServerInstance->m_shmControlptr->value = index;
        remoteVSTServerInstance->m_shmControlptr->floatvalue = opt;
        remoteVSTServerInstance->waitForServer(
            remoteVSTServerInstance->m_shmControlptr);
        retval = 0;
        retval = remoteVSTServerInstance->m_shmControlptr->retint;
        rv = retval;
      }
    }
    break;

  case audioMasterGetAutomationState:
    if (debugLevel > 1)
      cerr << "dssi-vst-server[2]: audioMasterGetAutomationState requested"
           << endl;

    if (remoteVSTServerInstance) {
      if (!remoteVSTServerInstance->exiting &&
          remoteVSTServerInstance->effectrun) {
        remoteVSTServerInstance->m_shmControlptr->ropcode =
            (RemotePluginOpcode)opcode;
        remoteVSTServerInstance->waitForServer(
            remoteVSTServerInstance->m_shmControlptr);
        retval = 0;
        retval = remoteVSTServerInstance->m_shmControlptr->retint;
        rv = retval;
      }
    }
    //     rv = 4; // read/write
    break;

  case audioMasterBeginEdit:
    if (debugLevel > 1)
      cerr << "dssi-vst-server[2]: audioMasterBeginEdit requested" << endl;

    if (remoteVSTServerInstance) {
      if (!remoteVSTServerInstance->exiting &&
          remoteVSTServerInstance->effectrun) {
        remoteVSTServerInstance->m_shmControlptr->ropcode =
            (RemotePluginOpcode)opcode;
        remoteVSTServerInstance->m_shmControlptr->value = index;
        remoteVSTServerInstance->waitForServer(
            remoteVSTServerInstance->m_shmControlptr);
        retval = 0;
        retval = remoteVSTServerInstance->m_shmControlptr->retint;
        rv = retval;
      }
    }
    break;

  case audioMasterEndEdit:
    if (debugLevel > 1)
      cerr << "dssi-vst-server[2]: audioMasterEndEdit requested" << endl;

    if (remoteVSTServerInstance) {
      if (!remoteVSTServerInstance->exiting &&
          remoteVSTServerInstance->effectrun) {
        remoteVSTServerInstance->m_shmControlptr->ropcode =
            (RemotePluginOpcode)opcode;
        remoteVSTServerInstance->m_shmControlptr->value = index;
        remoteVSTServerInstance->waitForServer(
            remoteVSTServerInstance->m_shmControlptr);
        retval = 0;
        retval = remoteVSTServerInstance->m_shmControlptr->retint;
        rv = retval;
      }
    }
    break;

  case audioMasterCurrentId:
    if (debugLevel > 1)
      cerr << "dssi-vst-server[2]: audioMasterCurrentId requested" << endl;

    break;

  case audioMasterIdle:
    if (debugLevel > 1)
      cerr << "dssi-vst-server[2]: audioMasterIdle requested " << endl;
    // plugin->dispatcher(plugin, effEditIdle, 0, 0, 0, 0);
    break;

  case audioMasterSetTime:

    if (remoteVSTServerInstance) {
      if (!remoteVSTServerInstance->exiting &&
          remoteVSTServerInstance->effectrun) {
        memcpy(remoteVSTServerInstance->m_shmControlptr->timeset, ptr,
               sizeof(VstTimeInfo));

        remoteVSTServerInstance->m_shmControlptr->ropcode =
            (RemotePluginOpcode)opcode;
        remoteVSTServerInstance->waitForServer(
            remoteVSTServerInstance->m_shmControlptr);
        retval = 0;
        retval = remoteVSTServerInstance->m_shmControlptr->retint;
        rv = retval;
      }
    }
    break;

  case audioMasterGetTime:

    if (remoteVSTServerInstance) {
      if (!remoteVSTServerInstance->exiting &&
          remoteVSTServerInstance->effectrun) {
        memcpy(remoteVSTServerInstance->timeinfo,
               remoteVSTServerInstance->m_shmControlptr->timeget,
               sizeof(VstTimeInfo));

        // printf("%f\n", remoteVSTServerInstance->timeinfo->sampleRate);

        if (remoteVSTServerInstance->m_shmControlptr->timeinit)
          rv = (VstIntPtr)remoteVSTServerInstance->timeinfo;
      }
    }
    break;

  case audioMasterProcessEvents:
      if (debugLevel > 1)
      cerr << "dssi-vst-server[2]: audioMasterProcessEvents requested" << endl;
    {
      VstEvents *evnts;
      int eventnum;
 	  int eventnum2;       
      int *ptr2;
      int sizeidx = 0;
      int ok;

    if (remoteVSTServerInstance) {
      if (!remoteVSTServerInstance->exiting &&
          remoteVSTServerInstance->effectrun) {
        evnts = (VstEvents *)ptr;

        if (!evnts) {
          break;
        }

        if (evnts->numEvents <= 0) {
          break;
        }

        eventnum = evnts->numEvents;
        eventnum2 = 0;  

        ptr2 = (int *)&remoteVSTServerInstance->m_shm3[VSTEVENTS_SEND_OFFSET];

        sizeidx = sizeof(int);

     //   if (eventnum > VSTSIZE)
     //     eventnum = VSTSIZE;

        for (int i = 0; i < eventnum; i++) {
          VstEvent *pEvent = evnts->events[i];
          if (pEvent->type == kVstSysExType)
          continue;
          else {
            unsigned int size =
                (2 * sizeof(VstInt32)) + evnts->events[i]->byteSize;
            memcpy(&remoteVSTServerInstance->m_shm3[VSTEVENTS_SEND_OFFSET + sizeidx], evnts->events[i],
                   size);
            sizeidx += size;
            if((sizeidx) >= VSTEVENTS_SEND)
            break;   
            eventnum2++;      
          }
        }
        
        if(eventnum2 > 0)
        {       
        *ptr2 = eventnum2;

        remoteVSTServerInstance->m_shmControlptr->ropcode = (RemotePluginOpcode)opcode;
        remoteVSTServerInstance->waitForServer(remoteVSTServerInstance->m_shmControlptr);
        retval = 0;
        retval = remoteVSTServerInstance->m_shmControlptr->retint;
        rv = retval;
        }
      }
    }
   }
    break;

  case audioMasterIOChanged: {
    struct amessage am;

    if (!remoteVSTServerInstance->exiting &&
        remoteVSTServerInstance->effectrun) {
      //   am.pcount = plugin->numPrograms;
      //   am.parcount = plugin->numParams;
      am.incount = plugin->numInputs;
      am.outcount = plugin->numOutputs;
      am.delay = plugin->initialDelay;
      /*
      #ifndef DOUBLEP
                      am.flags = plugin->flags;
                      am.flags &= ~effFlagsCanDoubleReplacing;
      #else
                      am.flags = plugin->flags;
      #endif
      */

      if ((am.incount != remoteVSTServerInstance->m_numInputs) ||
          (am.outcount != remoteVSTServerInstance->m_numOutputs) ||
          (am.delay != remoteVSTServerInstance->m_delay)) {
        /*
 if((am.incount != m_numInputs) || (am.outcount != m_numOutputs))
 {
 if ((am.incount + am.outcount) * m_bufferSize * sizeof(float) < (PROCESSSIZE))
        }
*/

        if (am.delay != remoteVSTServerInstance->m_delay)
          remoteVSTServerInstance->m_delay = am.delay;

        memcpy(remoteVSTServerInstance->m_shmControlptr->amptr, &am,
               sizeof(am));

        remoteVSTServerInstance->m_shmControlptr->ropcode =
            (RemotePluginOpcode)opcode;
        remoteVSTServerInstance->waitForServer(
            remoteVSTServerInstance->m_shmControlptr);
        retval = 0;
        retval = remoteVSTServerInstance->m_shmControlptr->retint;
        rv = retval;
        // }
      }
      /*
                      if((am.incount != m_numInputs) || (am.outcount !=
         m_numOutputs))
                      {
                      if ((am.incount + am.outcount) * m_bufferSize *
         sizeof(float) < (PROCESSSIZE))
                      {
                      m_updateio = 1;
                      m_updatein = am.incount;
                      m_updateout = am.outcount;
                      }
                      }
          */
      /*
              AEffect* update = m_plugin;
              update->flags = am.flags;
              update->numPrograms = am.pcount;
              update->numParams = am.parcount;
              update->numInputs = am.incount;
              update->numOutputs = am.outcount;
              update->initialDelay = am.delay;
      */
    }
  } break;

  case audioMasterUpdateDisplay:
    /*
           if (debugLevel > 1)
               cerr << "dssi-vst-server[2]: audioMasterUpdateDisplay requested"
       << endl;

       if(remoteVSTServerInstance)
       {
       if (!remoteVSTServerInstance->exiting &&
       remoteVSTServerInstance->effectrun)
       {

       remoteVSTServerInstance->m_shmControlptr->ropcode =
       (RemotePluginOpcode)opcode;
       remoteVSTServerInstance->waitForServer(remoteVSTServerInstance->m_shmControlptr);
       retval = 0;
       retval = remoteVSTServerInstance->m_shmControlptr->retint;
       rv = retval;

         }
        }
    */
    break;

  case audioMasterSizeWindow:
    if (debugLevel > 1)
      cerr << "dssi-vst-server[2]: audioMasterSizeWindow requested" << endl;
#ifdef EMBEDRESIZE
    {
      int opcodegui = 123456789;
#ifdef EMBED
      if (remoteVSTServerInstance) {
        if (remoteVSTServerInstance->hWnd &&
            remoteVSTServerInstance->guiVisible &&
            !remoteVSTServerInstance->exiting &&
            remoteVSTServerInstance->effectrun &&
            (remoteVSTServerInstance->guiupdate == 0)) {
          if ((remoteVSTServerInstance->guiresizewidth == index) &&
              (remoteVSTServerInstance->guiresizeheight == value)) {
            break;
          }

          remoteVSTServerInstance->guiresizewidth = index;
          remoteVSTServerInstance->guiresizeheight = value;

          // ShowWindow(remoteVSTServerInstance->hWnd, SW_HIDE);
          // SetWindowPos(remoteVSTServerInstance->hWnd, HWND_TOP, 0, 0,
          // remoteVSTServerInstance->guiresizewidth,
          // remoteVSTServerInstance->guiresizeheight, 0);

/*
#ifdef TRACKTIONWM
          if (remoteVSTServerInstance->hosttracktion == 1)
            SetWindowPos(remoteVSTServerInstance->hWnd, HWND_TOP,
                         GetSystemMetrics(SM_XVIRTUALSCREEN) +
                             remoteVSTServerInstance->offset.x,
                         GetSystemMetrics(SM_YVIRTUALSCREEN) +
                             remoteVSTServerInstance->offset.y,
                         index, value, 0);
#endif
*/

          remoteVSTServerInstance->m_shmControlptr->ropcode =
              (RemotePluginOpcode)opcode;
          remoteVSTServerInstance->m_shmControlptr->value = index;
          remoteVSTServerInstance->m_shmControlptr->value2 = value;
          remoteVSTServerInstance->waitForServer(
              remoteVSTServerInstance->m_shmControlptr);
          retval = 0;
          retval = remoteVSTServerInstance->m_shmControlptr->retint;
          rv = retval;
          //   remoteVSTServerInstance->guiupdate = 1;
        }
      }
#else
      if (remoteVSTServerInstance) {
        if (remoteVSTServerInstance->hWnd &&
            !remoteVSTServerInstance->exiting &&
            remoteVSTServerInstance->effectrun &&
            remoteVSTServerInstance->guiVisible) {
          /*
          //    SetWindowPos(remoteVSTServerInstance->hWnd, 0, 0, 0, index + 6,
          value + 25, SWP_NOMOVE | SWP_HIDEWINDOW);
              SetWindowPos(remoteVSTServerInstance->hWnd, 0, 0, 0, index + 6,
          value + 25, SWP_NOMOVE); ShowWindow(remoteVSTServerInstance->hWnd,
          SW_SHOWNORMAL); UpdateWindow(remoteVSTServerInstance->hWnd);
          */

          if ((remoteVSTServerInstance->guiresizewidth == index) &&
              (remoteVSTServerInstance->guiresizeheight == value))
            break;

          remoteVSTServerInstance->guiresizewidth = index;
          remoteVSTServerInstance->guiresizeheight = value;
          remoteVSTServerInstance->guiupdate = 1;
          rv = 1;
        }
      }
#endif
    }
#endif
    break;

  case audioMasterGetVendorString:
    if (debugLevel > 1)
      cerr << "dssi-vst-server[2]: audioMasterGetVendorString requested"
           << endl;
    {
      char retstr[512];

      if (remoteVSTServerInstance) {
        if (!remoteVSTServerInstance->exiting) {
          remoteVSTServerInstance->m_shmControlptr->ropcode =
              (RemotePluginOpcode)opcode;
          remoteVSTServerInstance->waitForServer(
              remoteVSTServerInstance->m_shmControlptr);
          strcpy(retstr, remoteVSTServerInstance->m_shmControlptr->retstr);
          strcpy((char *)ptr, retstr);
          retval = 0;
          retval = remoteVSTServerInstance->m_shmControlptr->retint;
          rv = retval;
        }
      }
    }
    break;

  case audioMasterGetProductString:
    if (debugLevel > 1)
      cerr << "dssi-vst-server[2]: audioMasterGetProductString requested"
           << endl;
    {
      char retstr[512];

      if (remoteVSTServerInstance) {
        if (!remoteVSTServerInstance->exiting) {
          remoteVSTServerInstance->m_shmControlptr->ropcode =
              (RemotePluginOpcode)opcode;
          remoteVSTServerInstance->waitForServer(
              remoteVSTServerInstance->m_shmControlptr);
          strcpy(retstr, remoteVSTServerInstance->m_shmControlptr->retstr);
          strcpy((char *)ptr, retstr);
          retval = 0;
          retval = remoteVSTServerInstance->m_shmControlptr->retint;
          rv = retval;
        }
      }
    }
    break;

  case audioMasterGetVendorVersion:
    if (debugLevel > 1)
      cerr << "dssi-vst-server[2]: audioMasterGetVendorVersion requested"
           << endl;

    if (remoteVSTServerInstance) {
      if (!remoteVSTServerInstance->exiting) {
        remoteVSTServerInstance->m_shmControlptr->ropcode =
            (RemotePluginOpcode)opcode;
        remoteVSTServerInstance->waitForServer(
            remoteVSTServerInstance->m_shmControlptr);
        retval = 0;
        retval = remoteVSTServerInstance->m_shmControlptr->retint;
        rv = retval;
      }
    }
    break;

  case audioMasterCanDo:
    if (debugLevel > 1)
      cerr << "dssi-vst-server[2]: audioMasterCanDo(" << (char *)ptr
           << ") requested" << endl;
#ifdef CANDOEFF
    {
      int retval;

      if (remoteVSTServerInstance && !remoteVSTServerInstance->exiting) {
        remoteVSTServerInstance->m_shmControlptr->ropcode =
            (RemotePluginOpcode)opcode;
        strcpy(m_shmControlptr->retstr, (char *)ptr);
        remoteVSTServerInstance->waitForServer(
            remoteVSTServerInstance->m_shmControlptr);

        memcpy(&retval, m_shmControlptr->retint, sizeof(int));
        rv = retval;
      }
    }
#else
    if (!strcmp((char *)ptr, "sendVstEvents") ||
        !strcmp((char *)ptr, "sendVstMidiEvent") ||
        !strcmp((char *)ptr, "receiveVstEvents") ||
        !strcmp((char *)ptr, "receiveVstMidiEvents") ||
        !strcmp((char *)ptr, "receiveVstTimeInfo") ||
        !strcmp((char *)ptr, "acceptIOChanges") ||
        !strcmp((char *)ptr, "startStopProcess")
#ifdef EMBED
#ifdef EMBEDRESIZE
        || !strcmp((char *)ptr, "sizeWindow")
#endif
#else
        || !strcmp((char *)ptr, "sizeWindow")
#endif
        // || !strcmp((char*)ptr, "supplyIdle")
    )
      rv = 1;
#endif
    break;

  case audioMasterGetSampleRate:
    if (debugLevel > 1)
      cerr << "dssi-vst-server[2]: audioMasterGetSampleRate requested" << endl;
    /*
        if(remoteVSTServerInstance)
        {
            if (!remoteVSTServerInstance->exiting &&
       remoteVSTServerInstance->effectrun)
            {
            if (!remoteVSTServerInstance->sampleRate)
            {
                //  cerr << "WARNING: Sample rate requested but not yet set" <<
       endl; break;
            }
            plugin->dispatcher(plugin, effSetSampleRate, 0, 0, NULL,
       (float)remoteVSTServerInstance->sampleRate);
            }
            }
    */
    /*
        if(remoteVSTServerInstance)
        {
        if (!remoteVSTServerInstance->exiting &&
       remoteVSTServerInstance->effectrun)
        {
        remoteVSTServerInstance->m_shmControlptr->ropcode =
       (RemotePluginOpcode)opcode;
        remoteVSTServerInstance->waitForServer(remoteVSTServerInstance->m_shmControlptr);
        retval = 0;
        retval = remoteVSTServerInstance->m_shmControlptr->retint;
        rv = retval;
          }
         }
        */
    if (remoteVSTServerInstance) {
      if (!remoteVSTServerInstance->exiting &&
          remoteVSTServerInstance->effectrun) {
        rv = remoteVSTServerInstance->sampleRate;
      }
    }
    break;

  case audioMasterGetBlockSize:
    if (debugLevel > 1)
      cerr << "dssi-vst-server[2]: audioMasterGetBlockSize requested" << endl;
    /*
        if(remoteVSTServerInstance)
        {
            if (!remoteVSTServerInstance->exiting &&
       remoteVSTServerInstance->effectrun)
            {
            if (!remoteVSTServerInstance->bufferSize)
            {
                // cerr << "WARNING: Buffer size requested but not yet set" <<
       endl; break;
            }
            plugin->dispatcher(plugin, effSetBlockSize, 0,
       remoteVSTServerInstance->bufferSize, NULL, 0);
            }
            }
    */
    /*
        if(remoteVSTServerInstance)
        {
        if (!remoteVSTServerInstance->exiting &&
       remoteVSTServerInstance->effectrun)
        {
        remoteVSTServerInstance->m_shmControlptr->ropcode =
       (RemotePluginOpcode)opcode;
        remoteVSTServerInstance->waitForServer(remoteVSTServerInstance->m_shmControlptr);
        retval = 0;
        retval = remoteVSTServerInstance->m_shmControlptr->retint;
        rv = retval;
          }
         }
     */
    if (remoteVSTServerInstance) {
      if (!remoteVSTServerInstance->exiting &&
          remoteVSTServerInstance->effectrun) {
        rv = remoteVSTServerInstance->bufferSize;
      }
    }
    break;

  case audioMasterGetInputLatency:
    if (debugLevel > 1)
      cerr << "dssi-vst-server[2]: audioMasterGetInputLatency requested"
           << endl;
    /*
        if(remoteVSTServerInstance)
        {
        if (!remoteVSTServerInstance->exiting &&
       remoteVSTServerInstance->effectrun)
        {
        remoteVSTServerInstance->m_shmControlptr->ropcode =
       (RemotePluginOpcode)opcode;
        remoteVSTServerInstance->waitForServer(remoteVSTServerInstance->m_shmControlptr);
        retval = 0;
        retval = remoteVSTServerInstance->m_shmControlptr->retint;
        rv = retval;
          }
         }
    */
    break;

  case audioMasterGetOutputLatency:
    if (debugLevel > 1)
      cerr << "dssi-vst-server[2]: audioMasterGetOutputLatency requested"
           << endl;
    /*
        if(remoteVSTServerInstance)
        {
        if (!remoteVSTServerInstance->exiting &&
       remoteVSTServerInstance->effectrun)
        {
        remoteVSTServerInstance->m_shmControlptr->ropcode =
       (RemotePluginOpcode)opcode;
        remoteVSTServerInstance->waitForServer(remoteVSTServerInstance->m_shmControlptr);
        retval = 0;
        retval = remoteVSTServerInstance->m_shmControlptr->retint;
        rv = retval;
          }
         }
    */
    break;

  case audioMasterGetCurrentProcessLevel:
    if (debugLevel > 1)
      cerr << "dssi-vst-server[2]: audioMasterGetCurrentProcessLevel requested"
           << endl;
    // 0 -> unsupported, 1 -> gui, 2 -> process, 3 -> midi/timer, 4 -> offline

    if (remoteVSTServerInstance) {
      if (!remoteVSTServerInstance->exiting &&
          remoteVSTServerInstance->effectrun) {
        if (remoteVSTServerInstance->inProcessThread)
          rv = 2;
        else
          rv = 1;
      }
    }
    break;

  case audioMasterGetLanguage:
    if (debugLevel > 1)
      cerr << "dssi-vst-server[2]: audioMasterGetLanguage requested" << endl;
    rv = kVstLangEnglish;
    break;

  case DEPRECATED_VST_SYMBOL(audioMasterWillReplaceOrAccumulate):
    if (debugLevel > 1)
      cerr << "dssi-vst-server[2]: audioMasterWillReplaceOrAccumulate requested"
           << endl;
    // 0 -> unsupported, 1 -> replace, 2 -> accumulate
    rv = 1;
    break;

  case DEPRECATED_VST_SYMBOL(audioMasterWantMidi):
    if (debugLevel > 1)
      cerr << "dssi-vst-server[2]: audioMasterWantMidi requested" << endl;
    // happy to oblige
    rv = 1;
    break;

  case DEPRECATED_VST_SYMBOL(audioMasterTempoAt):
    // if (debugLevel > 1)
    // cerr << "dssi-vst-server[2]: audioMasterTempoAt requested" << endl;
    // can't support this, return 120bpm
    rv = 120 * 10000;
    break;

  case DEPRECATED_VST_SYMBOL(audioMasterGetNumAutomatableParameters):
    if (debugLevel > 1)
      cerr << "dssi-vst-server[2]: audioMasterGetNumAutomatableParameters "
              "requested"
           << endl;
    rv = 5000;
    break;

  case DEPRECATED_VST_SYMBOL(audioMasterGetParameterQuantization):
    if (debugLevel > 1)
      cerr
          << "dssi-vst-server[2]: audioMasterGetParameterQuantization requested"
          << endl;
    rv = 1;
    break;

  case DEPRECATED_VST_SYMBOL(audioMasterNeedIdle):
    if (debugLevel > 1)
      cerr << "dssi-vst-server[2]: audioMasterNeedIdle requested" << endl;
    // might be nice to handle this better
    rv = 1;
    break;

  default:
    if (debugLevel > 0)
      cerr << "dssi-vst-server[0]: unsupported audioMaster callback opcode "
           << opcode << endl;
    break;
  }

  return rv;
}

void RemoteVSTServer::finisherror() {
  cerr << "Failed to create par thread!" << endl;

  exiting = 1;
  sleep(1);

  if (ThreadHandle[0]) {
    WaitForSingleObject(ThreadHandle[0], 5000);
    // TerminateThread(ThreadHandle[0], 0);
    CloseHandle(ThreadHandle[0]);
  }
#ifdef DRAGWIN 
  if (ThreadHandle[1]) {
    WaitForSingleObject(ThreadHandle[1], 5000);
    // TerminateThread(ThreadHandle[1], 0);
    CloseHandle(ThreadHandle[1]);
  }
#endif
#ifndef PCACHE
  if (ThreadHandle[2]) {
    WaitForSingleObject(ThreadHandle[2], 5000);
    // TerminateThread(remoteVSTServerInstance->ThreadHandle[2], 0);
    CloseHandle(ThreadHandle[2]);
  }
#endif  
  if (ThreadHandle[3]) {
    WaitForSingleObject(ThreadHandle[3], 5000);
    // TerminateThread(remoteVSTServerInstance->ThreadHandle[3], 0);
    CloseHandle(ThreadHandle[3]);
  }  

  if (m_shmControl) {
    m_shmControl->ropcode = (RemotePluginOpcode)disconnectserver;
    waitForServer(remoteVSTServerInstance->m_shmControl);
    waitForClient2exit();
    waitForClient3exit();
    waitForClient4exit();
    waitForClient5exit();
    waitForClient6exit();
  }
  usleep(5000000);
}

VOID CALLBACK TimerProc(HWND hWnd, UINT message, UINT idTimer, DWORD dwTime) {
  HWND hwnderr = FindWindow(NULL, "LinVst Error");
  SendMessage(hwnderr, WM_COMMAND, IDCANCEL, 0);
}

void RemoteVSTServer::guiUpdate() {
#ifdef EMBED
#ifdef EMBEDRESIZE
  remoteVSTServerInstance->guiupdatecount += 1;

  if (remoteVSTServerInstance->guiupdatecount == 2) {
    ShowWindow(remoteVSTServerInstance->hWnd, SW_SHOWNORMAL);
    UpdateWindow(remoteVSTServerInstance->hWnd);
    remoteVSTServerInstance->guiupdate = 0;
    remoteVSTServerInstance->guiupdatecount = 0;
  }
#endif
#endif
#ifndef EMBED
  //      SetWindowPos(remoteVSTServerInstance->hWnd, 0, 0, 0,
  //      remoteVSTServerInstance->guiresizewidth + 6,
  //      remoteVSTServerInstance->guiresizeheight + 25, SWP_NOMOVE |
  //      SWP_HIDEWINDOW);
  SetWindowPos(remoteVSTServerInstance->hWnd, 0, 0, 0,
               remoteVSTServerInstance->guiresizewidth + 6,
               remoteVSTServerInstance->guiresizeheight + 25, SWP_NOMOVE);
  ShowWindow(remoteVSTServerInstance->hWnd, SW_SHOWNORMAL);
  UpdateWindow(remoteVSTServerInstance->hWnd);
  remoteVSTServerInstance->guiupdate = 0;
#endif
}

bool InitModule() { return true; }

bool DeinitModule() { return true; }

#define mchr(a, b, c, d) (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))

Steinberg::Vst::Vst2Wrapper *
createEffectInstance2(audioMasterCallback audioMaster, HMODULE libHandle,
                      std::string libnamepath, std::string partidx,
                      Steinberg::IPluginFactory *factory, int *vstuid) {
  std::string libnamepath2;
  bool test;
  Steinberg::FIDString cid;
  Steinberg::FIDString firstcid;
  int audioclasscount = 0;
  char ibuf[256];
  int parthit = 0;
  int parthit2 = 0;
  int audioclassfirst = 0;
  int audioclass = 0;
  int firstdone = 0;

  if (libnamepath.find(".vst3") != std::string::npos) {
    libnamepath.replace(libnamepath.begin() + libnamepath.find(".vst3"),
                        libnamepath.end(), "");
  } else if (libnamepath.find(".Vst3") != std::string::npos) {
    libnamepath.replace(libnamepath.begin() + libnamepath.find(".Vst3"),
                        libnamepath.end(), "");
  } else if (libnamepath.find(".VST3") != std::string::npos) {
    libnamepath.replace(libnamepath.begin() + libnamepath.find(".VST3"),
                        libnamepath.end(), "");
  }

  auto proc =
      (GetFactoryProc)::GetProcAddress((HMODULE)libHandle, "GetPluginFactory");

  if (!proc)
    return nullptr;

  factory = proc();

  if (!factory)
    return nullptr;

  Steinberg::PFactoryInfo factoryInfo;

  factory->getFactoryInfo(&factoryInfo);

  // cerr << "  Factory Info:\n\tvendor = " << factoryInfo.vendor << "\n\turl =
  // " << factoryInfo.url << "\n\temail = " << factoryInfo.email << "\n\n" <<
  // endl;

  int countclasses = factory->countClasses();

  if (countclasses == 0)
    return nullptr;

  int idval = 0;

  for (Steinberg::int32 i = 0; i < countclasses; i++) {
    Steinberg::PClassInfo classInfo;

    factory->getClassInfo(i, &classInfo);

    if (strcmp(classInfo.category, "Audio Module Class") == 0) {
      if (audioclassfirst == 0 && firstdone == 0) {
        audioclassfirst = i;
        firstdone = 1;
      }

      audioclass = i;

      memset(ibuf, 0, sizeof(ibuf));

      if (i < 10) {
        sprintf(ibuf, "%d", 0);
        sprintf(&ibuf[1], "%d", i);
      } else
        sprintf(ibuf, "%d", i);

      libnamepath2 = libnamepath + "-" + "part" + "-" + ibuf + ".so";

      test = std::ifstream(libnamepath2.c_str()).good();

      if ((atoi(partidx.c_str()) == (10000)) && !test &&
          (audioclasscount > 0)) {
        parthit = 1;

        std::ifstream source(libnamepath + ".so", std::ios::binary);

        std::ofstream dest(libnamepath2, std::ios::binary);

        dest << source.rdbuf();

        source.close();
        dest.close();
      } else if ((atoi(partidx.c_str()) == (i)) && test &&
                 (audioclasscount > 0)) {
        parthit2 = 1;
        break;
      }

      audioclasscount++;

    } // Audio Module Class
  }   // for

  if ((atoi(partidx.c_str()) == (10000)) && (parthit == 1))
    audioclass = audioclassfirst;

  if ((atoi(partidx.c_str()) == (10000)) && (parthit == 0 && parthit2 == 0))
    audioclass = audioclassfirst;

  Steinberg::PClassInfo classInfo2;

  factory->getClassInfo(audioclass, &classInfo2);
  cid = classInfo2.cid;

  Steinberg::FIDString iid = Steinberg::Vst::IComponent::iid;

  Steinberg::char8 cidString[50];
  Steinberg::FUID(classInfo2.cid).toRegistryString(cidString);
  Steinberg::String cidStr(cidString);
  // cerr << "  Class Info " << audioclass << ":\n\tname = " << classInfo2.name
  // << "\n\tcategory = " << classInfo2.category << "\n\tcid = " << cidStr.text8
  // () << "\n\n" << endl;

  idval = mchr(cidStr[1], cidStr[10], cidStr[15], cidStr[20]);

  int idval1 = cidStr[1] + cidStr[2] + cidStr[3] + cidStr[4];
  int idval2 = cidStr[5] + cidStr[6] + cidStr[7] + cidStr[8];
  int idval3 = cidStr[10] + cidStr[11] + cidStr[12] + cidStr[13];
  int idval4 = cidStr[15] + cidStr[16] + cidStr[17] + cidStr[18];
  int idval5 = cidStr[20] + cidStr[21] + cidStr[22] + cidStr[23];
  int idval6 = cidStr[25] + cidStr[26] + cidStr[27] + cidStr[28];
  int idval7 = cidStr[29] + cidStr[30] + cidStr[31] + cidStr[32];
  int idval8 = cidStr[33] + cidStr[34] + cidStr[35] + cidStr[36];

  int idvalout = 0;

  idvalout += idval1;
  idvalout += idval2 << 1;
  idvalout += idval3 << 2;
  idvalout += idval4 << 3;
  idvalout += idval5 << 4;
  idvalout += idval6 << 5;
  idvalout += idval7 << 6;
  idvalout += idval8 << 7;

  idval += idvalout;

  *vstuid = idval;

  return Steinberg::Vst::Vst2Wrapper::create(factory, cid, idval, audioMaster);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR cmdline,
                   int cmdshow) {
  //  HANDLE  remoteVSTServerInstance->ThreadHandle[3] = {0,0,0};
  string pathName;
  string fileName;
  char cdpath[4096];
  char *libname = 0;
  char *libname2 = 0;
  char *fileInfo = 0;
  string libnamepath;
  string partidx;
  char *outname[10];
  int offset = 0;
  int idx = 0;
  int ci = 0;
  HMODULE libHandle = 0;

  cerr << "DSSI VST plugin server v" << RemotePluginVersion << endl;
  cerr << "Copyright (c) 2012-2013 Filipe Coelho" << endl;
  cerr << "Copyright (c) 2010-2011 Kristian Amlie" << endl;
  cerr << "Copyright (c) 2004-2006 Chris Cannam" << endl;
  cerr << "LinVst3 version 4.5.2" << endl;

  /*

  if (cmdline)
  {
  int offset = 0;
  if (cmdline[0] == '"' || cmdline[0] == '\'') offset = 1;
  for (int ci = offset; cmdline[ci]; ++ci)
  {
  if (cmdline[ci] == ',')
  {
  libname2 = strndup(cmdline + offset, ci - offset);
  ++ci;
  if (cmdline[ci])
  {
  fileInfo = strdup(cmdline + ci);
  int l = strlen(fileInfo);
  if (fileInfo[l-1] == '"' || fileInfo[l-1] == '\'')
  fileInfo[l-1] = '\0';
  }
  }
  }
  }
  */

  if (cmdline[0] == '\0') {
    exit(0);
    // return 1;
  }

  if (cmdline) {
    if (cmdline[0] == '"' || cmdline[0] == '\'')
      offset = 1;
    for (ci = offset; cmdline[ci]; ++ci) {
      if (cmdline[ci] == ',') {
        outname[idx] = strndup(cmdline + offset, ci - offset);
        ++idx;
        ++ci;
        offset = ci;
      }
    }
    outname[idx] = strndup(cmdline + offset, ci - offset);
  }

  partidx = outname[0];
  libname2 = outname[1];
  fileInfo = outname[2];
  int l = strlen(fileInfo);
  if (fileInfo[l - 1] == '"' || fileInfo[l - 1] == '\'')
    fileInfo[l - 1] = '\0';

  // printf("fileinfo %s\n", fileInfo);

  if (libname2 != NULL) {
    if ((libname2[0] == '/') && (libname2[1] == '/'))
      libname = strdup(&libname2[1]);
    else
      libname = strdup(libname2);
  } else {
    cerr << "Usage: dssi-vst-server <vstname.dll>,<tmpfilebase>" << endl;
    cerr << "(Command line was: " << cmdline << ")" << endl;

    exit(0);
    // return 1;
  }

  if (!libname || !libname[0] || !fileInfo || !fileInfo[0]) {
    cerr << "Usage: dssi-vst-server <vstname.dll>,<tmpfilebase>" << endl;
    cerr << "(Command line was: " << cmdline << ")" << endl;

    exit(0);
    // return 1;
  }

  strcpy(cdpath, libname);
  pathName = cdpath;
  fileName = cdpath;
  size_t found = pathName.find_last_of("/");
  pathName = pathName.substr(0, found);
  size_t found2 = fileName.find_last_of("/");
  fileName = fileName.substr(found2 + 1, strlen(libname) - (found2 + 1));
  // SetCurrentDirectory(pathName.c_str());
    
  libnamepath = " ";    
    
  libnamepath = libname;    

  remoteVSTServerInstance = 0;

  string deviceName = fileName;
  size_t foundext = deviceName.find_last_of(".");
  deviceName = deviceName.substr(0, foundext);
  remoteVSTServerInstance = new RemoteVSTServer(fileInfo, deviceName);

  if (!remoteVSTServerInstance) {
    cerr << "ERROR: Remote VST startup failed" << endl;
    usleep(5000000);
    exit(0);
  }

  if (remoteVSTServerInstance->starterror == 1) {
    cerr << "ERROR: Remote VST startup error" << endl;
    if (remoteVSTServerInstance) {
      remoteVSTServerInstance->finisherror();
      delete remoteVSTServerInstance;
    }
    exit(0);
  }

  remoteVSTServerInstance->ThreadHandle[0] = 0;
  remoteVSTServerInstance->ThreadHandle[1] = 0;
  remoteVSTServerInstance->ThreadHandle[2] = 0;
  remoteVSTServerInstance->ThreadHandle[3] = 0;  

  DWORD threadIdp = 0;
  remoteVSTServerInstance->ThreadHandle[0] =
      CreateThread(0, 0, AudioThreadMain, 0, CREATE_SUSPENDED, &threadIdp);
  if (!remoteVSTServerInstance->ThreadHandle[0]) {
    cerr << "Failed to create audio thread!" << endl;
    remoteVSTServerInstance->finisherror();
    delete remoteVSTServerInstance;
    exit(0);
  }

#ifdef DRAGWIN 
  DWORD threadIdp2 = 0;
  remoteVSTServerInstance->ThreadHandle[1] =
      CreateThread(0, 0, GetSetThreadMain, 0, CREATE_SUSPENDED, &threadIdp2);
  if (!remoteVSTServerInstance->ThreadHandle[1]) {
    cerr << "Failed to create getset thread!" << endl;
    remoteVSTServerInstance->finisherror();
    delete remoteVSTServerInstance;
    exit(0);
  }
#endif

#ifndef PCACHE
  DWORD threadIdp3 = 0;
  remoteVSTServerInstance->ThreadHandle[2] =
      CreateThread(0, 0, ParThreadMain, 0, CREATE_SUSPENDED, &threadIdp3);
  if (!remoteVSTServerInstance->ThreadHandle[2]) {
    cerr << "Failed to create par thread!" << endl;
    remoteVSTServerInstance->finisherror();
    delete remoteVSTServerInstance;
    exit(0);
  }
#endif
  
  DWORD threadIdp4 = 0;
  remoteVSTServerInstance->ThreadHandle[3] =
      CreateThread(0, 0, ControlThreadMain, 0, CREATE_SUSPENDED, &threadIdp4);
  if (!remoteVSTServerInstance->ThreadHandle[3]) {
    cerr << "Failed to create par thread!" << endl;
    remoteVSTServerInstance->finisherror();
    delete remoteVSTServerInstance;
    exit(0);
  }    

  cerr << "Loading  " << libname << endl;

  libHandle = ::LoadLibraryA(libname);
  if (!libHandle) {
    cerr << "dssi-vst-server: ERROR: Couldn't load VST DLL \"" << libname
         << "\"" << endl;
    remoteVSTServerInstance->finisherror();
    delete remoteVSTServerInstance;
    exit(0);
  }

  InitModuleProc initProc =
      (InitModuleProc)::GetProcAddress((HMODULE)libHandle, kInitModuleProcName);
  if (initProc) {
    if (initProc() == false) {
      cerr << "initprocerr" << endl;

      remoteVSTServerInstance->finisherror();
      delete remoteVSTServerInstance;

      if (libHandle) {
        ExitModuleProc exitProc = (ExitModuleProc)::GetProcAddress(
            (HMODULE)libHandle, kExitModuleProcName);
        if (exitProc)
          exitProc();
        FreeLibrary(libHandle);
      }
      exit(0);
      // return 1;
    }
  }

  int *ptr;
  ptr = (int *)remoteVSTServerInstance->m_shm;
  *ptr = 2000;

  int startok;

  startok = 0;

  for (int i = 0; i < 400000; i++) {
    if (*ptr == 6000) {
      startok = 1;
      break;
    }
    usleep(100);
  }

  if (startok == 0) {
    cerr << "Failed to connect" << endl;
    remoteVSTServerInstance->finisherror();
    delete remoteVSTServerInstance;

    if (libHandle) {
      ExitModuleProc exitProc = (ExitModuleProc)::GetProcAddress(
          (HMODULE)libHandle, kExitModuleProcName);
      if (exitProc)
        exitProc();
      FreeLibrary(libHandle);
    }
    exit(0);
  }

  audioMasterCallback hostCallbackFuncPtr = hostCallback;

  remoteVSTServerInstance->vst2wrap = createEffectInstance2(
      hostCallbackFuncPtr, libHandle, libnamepath, partidx,
      remoteVSTServerInstance->factory, &remoteVSTServerInstance->vst2uid);

  if (!remoteVSTServerInstance->vst2wrap) {
    cerr << "dssi-vst-server: ERROR: Failed to instantiate plugin in VST DLL \""
         << libname << "\"" << endl;

    remoteVSTServerInstance->finisherror();
    delete remoteVSTServerInstance;

    if (libHandle) {
      ExitModuleProc exitProc = (ExitModuleProc)::GetProcAddress(
          (HMODULE)libHandle, kExitModuleProcName);
      if (exitProc)
        exitProc();
      FreeLibrary(libHandle);
    }
    exit(0);
    // return 1;
  }

  if (remoteVSTServerInstance->vst2wrap->editor) {
    remoteVSTServerInstance->haveGui = true;
    remoteVSTServerInstance->mpluginptr->flags |= effFlagsHasEditor;
  } else
    remoteVSTServerInstance->haveGui = false;

  ResumeThread(remoteVSTServerInstance->ThreadHandle[0]);
#ifdef DRAGWIN 
  ResumeThread(remoteVSTServerInstance->ThreadHandle[1]);
#endif
#ifndef PCACHE
  ResumeThread(remoteVSTServerInstance->ThreadHandle[2]);
#endif
  ResumeThread(remoteVSTServerInstance->ThreadHandle[3]);  

  remoteVSTServerInstance->deviceName2 = deviceName;
  
#ifdef DRAGWIN  
   XInitThreads();
  
   remoteVSTServerInstance->display = XOpenDisplay(NULL);
	
   if(remoteVSTServerInstance->display)
   {   
   remoteVSTServerInstance->XdndSelection = XInternAtom(remoteVSTServerInstance->display, "XdndSelection", False);
   remoteVSTServerInstance->XdndAware = XInternAtom(remoteVSTServerInstance->display, "XdndAware", False);
   remoteVSTServerInstance->XdndProxy = XInternAtom(remoteVSTServerInstance->display, "XdndProxy", False);
   remoteVSTServerInstance->XdndTypeList = XInternAtom(remoteVSTServerInstance->display, "XdndTypeList", False);
   remoteVSTServerInstance->XdndActionCopy = XInternAtom(remoteVSTServerInstance->display, "XdndActionCopy", False);
   
   remoteVSTServerInstance->XdndPosition = XInternAtom(remoteVSTServerInstance->display, "XdndPosition", False);
   remoteVSTServerInstance->XdndStatus = XInternAtom(remoteVSTServerInstance->display, "XdndStatus", False);
   remoteVSTServerInstance->XdndActionCopy = XInternAtom(remoteVSTServerInstance->display, "XdndActionCopy", False);
   remoteVSTServerInstance->XdndEnter = XInternAtom(remoteVSTServerInstance->display, "XdndEnter", False);
   remoteVSTServerInstance->XdndDrop = XInternAtom(remoteVSTServerInstance->display, "XdndDrop", False);
   remoteVSTServerInstance->XdndLeave = XInternAtom(remoteVSTServerInstance->display, "XdndLeave", False);
   remoteVSTServerInstance->XdndFinished = XInternAtom(remoteVSTServerInstance->display, "XdndFinished", False);
   
   remoteVSTServerInstance->hCurs1 = LoadCursor(NULL, IDC_SIZEALL); 
   remoteVSTServerInstance->hCurs2 = LoadCursor(NULL, IDC_NO); 
   }
#endif     

  MSG msg;
  int tcount = 0;

  while (!remoteVSTServerInstance->exiting) {
    if (remoteVSTServerInstance->wavesthread == 1) {
      for (int loopidx = 0;
           (loopidx < 10) && PeekMessage(&msg, 0, 0, 0, PM_REMOVE); loopidx++) {
        if (remoteVSTServerInstance->exiting)
          break;

        if (msg.message == 15 && !remoteVSTServerInstance->guiVisible)
          break;

        TranslateMessage(&msg);
        DispatchMessage(&msg);

        //if (remoteVSTServerInstance->hidegui == 1)
        // break;
        }

      //if (remoteVSTServerInstance->hidegui == 1) {
      //  remoteVSTServerInstance->hideGUI();
      //}

      if (remoteVSTServerInstance->exiting)
        break;
      remoteVSTServerInstance->dispatchControl(5);
    } else {
      while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
        if (remoteVSTServerInstance->exiting)
          break;

        if (msg.message == 15 && !remoteVSTServerInstance->guiVisible)
          break;

        TranslateMessage(&msg);
        DispatchMessage(&msg);

        //if (remoteVSTServerInstance->hidegui == 1)
        // break;
      }

      //if (remoteVSTServerInstance->hidegui == 1) {
      //  remoteVSTServerInstance->hideGUI();
      //}

      if (remoteVSTServerInstance->exiting)
        break;
      remoteVSTServerInstance->dispatchControl(5);
    }
  }

  remoteVSTServerInstance->waitForServerexit();
  remoteVSTServerInstance->waitForClient2exit();
  remoteVSTServerInstance->waitForClient3exit();
  remoteVSTServerInstance->waitForClient4exit();
  remoteVSTServerInstance->waitForClient5exit();
  remoteVSTServerInstance->waitForClient6exit();

 // WaitForMultipleObjects(4, remoteVSTServerInstance->ThreadHandle, TRUE, 5000);

  for (int idx50 = 0; idx50 < 100000; idx50++) {
    if (
#ifndef PCACHE
remoteVSTServerInstance->parfin && 
#endif
remoteVSTServerInstance->audfin &&
#ifdef DRAGWIN 
        remoteVSTServerInstance->getfin && 
#endif
remoteVSTServerInstance->confin)
      break;
    usleep(100);
  }

  if (debugLevel > 0)
    cerr << "dssi-vst-server[1]: cleaning up" << endl;

  if (remoteVSTServerInstance->ThreadHandle[0]) {
    // TerminateThread(remoteVSTServerInstance->ThreadHandle[0], 0);
    CloseHandle(remoteVSTServerInstance->ThreadHandle[0]);
  }

#ifdef DRAGWIN 
  if (remoteVSTServerInstance->ThreadHandle[1]) {
    // TerminateThread(remoteVSTServerInstance->ThreadHandle[1], 0);
    CloseHandle(remoteVSTServerInstance->ThreadHandle[1]);
  }
#endif

#ifndef PCACHE
  if (remoteVSTServerInstance->ThreadHandle[2]) {
    // TerminateThread(remoteVSTServerInstance->ThreadHandle[2], 0);
    CloseHandle(remoteVSTServerInstance->ThreadHandle[2]);
  }
#endif
  
  if (remoteVSTServerInstance->ThreadHandle[3]) {
    // TerminateThread(remoteVSTServerInstance->ThreadHandle[3], 0);
    CloseHandle(remoteVSTServerInstance->ThreadHandle[3]);
  }  

  if (debugLevel > 0)
    cerr << "dssi-vst-server[1]: closed threads" << endl;
    
#ifdef DRAGWIN  
  if(remoteVSTServerInstance->display)  
    XCloseDisplay(remoteVSTServerInstance->display);
#endif    

  if (remoteVSTServerInstance)
    delete remoteVSTServerInstance;

  if (libHandle) {
    ExitModuleProc exitProc = (ExitModuleProc)::GetProcAddress(
        (HMODULE)libHandle, kExitModuleProcName);
    if (exitProc)
      exitProc();
    FreeLibrary(libHandle);
  }

  if (debugLevel > 0)
    cerr << "dssi-vst-server[1]: freed dll" << endl;
  //   if (debugLevel > 0)
  cerr << "dssi-vst-server[1]: exiting" << endl;

  exit(0);

  //   return 0;
}
