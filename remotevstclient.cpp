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

#include "remotevstclient.h"

#include <sys/un.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>

#include "paths.h"

#include <dlfcn.h>

#include <iostream>
#include <string>
#include <fstream>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

void errwin(std::string dllname)
{
static Window window = 0;
static Window ignored = 0;
static Display* display = 0;
static int screen = 0;
static Atom winstate;
static Atom winmodal;
    
std::string filename;
std::string filename2;

  size_t found2 = dllname.find_last_of("/");
  filename = dllname.substr(found2 + 1, strlen(dllname.c_str()) - (found2 +1));
  filename2 = "VST dll file not found or timeout:  " + filename;
      
  XInitThreads();
  display = XOpenDisplay(NULL);  
  if (!display) 
  return;  
  screen = DefaultScreen(display);
  window = XCreateSimpleWindow(display, RootWindow(display, screen), 10, 10, 480, 20, 0, BlackPixel(display, screen), WhitePixel(display, screen));
  if (!window) 
  return;
  winstate = XInternAtom(display, "_NET_WM_STATE", True);
  winmodal = XInternAtom(display, "_NET_WM_STATE_ABOVE", True);
  XChangeProperty(display, window, winstate, XA_ATOM, 32, PropModeReplace, (unsigned char*)&winmodal, 1);
  XStoreName(display, window, filename2.c_str()); 
  XMapWindow(display, window);
  XSync (display, false);
  XFlush(display);
  sleep(10);
  XSync (display, false);
  XFlush(display);
  XDestroyWindow(display, window);
  XCloseDisplay(display);  
  }

const char *selfname()
{
    int i = 5;
}

RemoteVSTClient::RemoteVSTClient(audioMasterCallback theMaster) : RemotePluginClient(theMaster)
{
    pid_t       child;
    Dl_info     info;
    std::string dllName;
    std::string LinVstName;
    bool        test;
    size_t found2;
    std::string filename;

   int          dlltype;
   unsigned int offset;
   char         buffer[256];

    char        hit2[4096];

    if (!dladdr((const char*) selfname, &info))
    {
        m_runok = 1;
        cleanup();
        return;
    }

    if (!info.dli_fname)
    {
        m_runok = 1;
        cleanup();
        return;
    }

    dllName = info.dli_fname;
    
    found2 = dllName.find_last_of("/");
    filename = dllName.substr(found2 + 1, strlen(dllName.c_str()) - (found2 +1));
     
    if(!strcmp(filename.c_str(),"linvst3.so"))
    {
        m_runok = 2;
        cleanup();
        return;          
    }    
             
    if (realpath(dllName.c_str(), hit2) == 0)
    {
        m_runok = 1;
        cleanup();
        return;
    }
    
        dllName = hit2;
    
        size_t found3 = dllName.find("-part-");
        
        if(found3 != std::string::npos)
        {                
    //    printf("partnamesofile %s\n", dllName.c_str()); 
                
        size_t found4 = dllName.find_last_of("-");
        
        if(found4 != std::string::npos)
        {           
        filename = dllName.substr(found4 + 1, strlen(dllName.c_str()) - (found4 +1));          
        filename.replace(filename.begin() + filename.find(".so"), filename.end(), "");        
        }
        
       dllName = dllName.substr(0, found3);
       dllName = dllName + ".so";
       
// printf("partname %s\n", dllName.c_str()); 

// printf("idxname %s\n", filename.c_str());               
        }
        else 
        filename = "10000"; 
        
    dllName.replace(dllName.begin() + dllName.find(".so"), dllName.end(), ".vst3");
    test = std::ifstream(dllName.c_str()).good();

    if (!test)
    {
        dllName = hit2;
        dllName.replace(dllName.begin() + dllName.find(".so"), dllName.end(), ".Vst3");
        test = std::ifstream(dllName.c_str()).good();

        if (!test)
        {
            dllName = hit2;
            dllName.replace(dllName.begin() + dllName.find(".so"), dllName.end(), ".VST3");
            test = std::ifstream(dllName.c_str()).good();
        }

        if (!test)
        {
             dllName = hit2;
             dllName.replace(dllName.begin() + dllName.find(".so"), dllName.end(), ".dll");
        //     errwin(dllName);
             m_runok = 2;
             cleanup();
             return;
        }
    }
    
    std::ifstream mfile(dllName.c_str(), std::ifstream::binary);

    if (!mfile)
    {
        m_runok = 1;
        cleanup();
        return;
    }

    mfile.read(&buffer[0], 2);
    short *ptr;
    ptr = (short *) &buffer[0];

    if (*ptr != 0x5a4d)
    {
        mfile.close();
        m_runok = 1;
        cleanup();
        return;
    }

    mfile.seekg (60, mfile.beg);
    mfile.read (&buffer[0], 4);

    int *ptr2;
    ptr2 = (int *) &buffer[0];
    offset = *ptr2;
    offset += 4;

    mfile.seekg (offset, mfile.beg);
    mfile.read (&buffer[0], 2);

    unsigned short *ptr3;
    ptr3 = (unsigned short *) &buffer[0];

    dlltype = 0;
    if (*ptr3 == 0x8664)
        dlltype = 1;
    else if (*ptr3 == 0x014c)
        dlltype = 2;
    else if (*ptr3 == 0x0200)
        dlltype = 3;

    if (dlltype == 0 || dlltype == 2)
    {
        mfile.close();
        m_runok = 1;
        cleanup();
        return;
    }

    mfile.close();
        
#ifdef EMBED
#ifdef TRACKTIONWM     
    LinVstName = "/usr/bin/lin-vst3-servertrack.exe";
    test = std::ifstream(LinVstName.c_str()).good();
    if (!test)
    {
    m_runok = 1;
    cleanup();
    return;
    }
    
    LinVstName = "/usr/bin/lin-vst3-servertrack.exe.so";
    test = std::ifstream(LinVstName.c_str()).good();
    if (!test)
    {
    m_runok = 1;
    cleanup();
    return;
    }
#else
    LinVstName = "/usr/bin/lin-vst3-server.exe";
    test = std::ifstream(LinVstName.c_str()).good();
    if (!test)
    {
    m_runok = 1;
    cleanup();
    return;
    }
    
    LinVstName = "/usr/bin/lin-vst3-server.exe.so";
    test = std::ifstream(LinVstName.c_str()).good();
    if (!test)
    {
    m_runok = 1;
    cleanup();
    return;
    }    
#endif    
#else
#ifdef TRACKTIONWM    
    LinVstName = "/usr/bin/lin-vst3-servertrackst.exe";
    test = std::ifstream(LinVstName.c_str()).good();
    if (!test)
    {
    m_runok = 1;
    cleanup();
    return;
    }
    LinVstName = "/usr/bin/lin-vst3-servertrackst.exe.so";
    test = std::ifstream(LinVstName.c_str()).good();
    if (!test)
    {
    m_runok = 1;
    cleanup();
    return;
    }
#else
    LinVstName = "/usr/bin/lin-vst3-serverst.exe";
    test = std::ifstream(LinVstName.c_str()).good();
    if (!test)
    {
    m_runok = 1;
    cleanup();
    return;
    }
    LinVstName = "/usr/bin/lin-vst3-serverst.exe.so";
    test = std::ifstream(LinVstName.c_str()).good();
    if (!test)
    {
    m_runok = 1;
    cleanup();
    return;
    }    
#endif    
#endif

    hit2[0] = '\0';

    std::string dllNamewin = dllName;
    std::size_t idx = dllNamewin.find("drive_c");

    if (idx != std::string::npos)
    {
        const char *hit = dllNamewin.c_str();
        strcpy(hit2, hit);
        hit2[idx - 1] = '\0';
        setenv("WINEPREFIX", hit2, 1);
    }

    std::string arg = filename + "," + dllName + "," + getFileIdentifiers();
    const char *argStr = arg.c_str();

    #ifdef LVRT
    struct sched_param param;
    param.sched_priority = 1;

    int result = sched_setscheduler(0, SCHED_FIFO, &param);

    if (result < 0)
    {
        perror("Failed to set realtime priority");
    }
    #endif

    if ((child = fork()) < 0)
    {
        m_runok = 1;
        cleanup();
        return;
    }
    else if (child == 0)
    {
    #ifdef EMBED
    #ifdef TRACKTIONWM     
    if (execlp("/usr/bin/lin-vst3-servertrack.exe", "/usr/bin/lin-vst3-servertrack.exe", argStr, NULL))
    #else
    if (execlp("/usr/bin/lin-vst3-server.exe", "/usr/bin/lin-vst3-server.exe", argStr, NULL))    
    #endif    
    #else
    #ifdef TRACKTIONWM       
    if (execlp("/usr/bin/lin-vst3-servertrackst.exe", "/usr/bin/lin-vst3-servertrackst.exe", argStr, NULL))
    #else
    if (execlp("/usr/bin/lin-vst3-serverst.exe", "/usr/bin/lin-vst3-serverst.exe", argStr, NULL))    
    #endif    
    #endif
    {
         m_runok = 1;
         cleanup();
         return;
    }
    }
    //  signal(SIGCHLD, SIG_IGN);
    
    syncStartup();
}

RemoteVSTClient::~RemoteVSTClient()
{
 //     wait(NULL);
}
