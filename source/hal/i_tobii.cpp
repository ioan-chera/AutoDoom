
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "eyex/EyeX.h"

#include "../z_zone.h"
#include "i_tobii.h"
#include "../m_qstr.h"

#define INTMAX_MAX 0x7fffffffffffffffLL
#include <mutex>

static const TX_CONSTSTRING g_interactorId = "EyeId";

//
// Used for main window lookup
//
struct handledata_t
{
   unsigned long processId;
   HWND bestHandle;
};

//
// The eyetracking event, observed by the game
//
static struct
{
   double x;
   double y;
   bool fired;
} g_event;

static std::mutex g_eventMutex;

static TX_CONTEXTHANDLE g_context;
static TX_HANDLE g_globalInteractorSnapshot;
static TX_TICKET g_connectionStateChangedTicket;
static TX_TICKET g_eventHandlerTicket;
static HWND g_mainWindow;

//=============================================================================
//
// Extra functions
//

//
// Sets up the snapshot
//
static bool I_initializeGlobalInteractorSnapshot()
{
   TX_HANDLE interactor = TX_EMPTY_HANDLE;
   TX_FIXATIONDATAPARAMS params = { TX_FIXATIONDATAMODE_SENSITIVE };
   TX_RESULT result;

   result = txCreateGlobalInteractorSnapshot(g_context, g_interactorId,
      &g_globalInteractorSnapshot, &interactor);
   if(result != TX_RESULT_OK)
      return false;

   result = txCreateFixationDataBehavior(interactor, &params);
   if(result != TX_RESULT_OK)
   {
      txReleaseObject(&interactor);
      txReleaseObject(&g_globalInteractorSnapshot);
      return false;
   }

   txReleaseObject(&interactor);

   return true;
}

//
// Called when data arrives
//
static void I_onFixationDataEvent(TX_HANDLE behavior)
{
   TX_FIXATIONDATAEVENTPARAMS eventParams;
   TX_FIXATIONDATAEVENTTYPE eventType;

   TX_RESULT result;

   result = txGetFixationDataEventParams(behavior, &eventParams);
   if(result != TX_RESULT_OK)
      return;

   eventType = eventParams.EventType;
   if(eventType == TX_FIXATIONDATAEVENTTYPE_END)
      return;

   RECT rect;
   if(!g_mainWindow || !GetWindowRect(g_mainWindow, &rect))
      return;
   if(rect.right == rect.left || rect.bottom == rect.top)
      return;

   std::lock_guard<std::mutex> lock(g_eventMutex);

   g_event.x = double(eventParams.X - rect.left) / (rect.right - rect.left);
   g_event.y = double(eventParams.Y - rect.top) / (rect.bottom - rect.top);
   g_event.fired = true;
}

//
// Helper function to get whether a hwnd is main
//
static bool I_isMainWindow(HWND handle)
{
   return GetWindow(handle, GW_OWNER) == 0 && IsWindowVisible(handle);
}

//
// For window getting
//
static BOOL CALLBACK I_enumWindowsCallback(HWND handle, LPARAM lParam)
{
   handledata_t &data = *(handledata_t *)lParam;
   unsigned long processId = 0;
   GetWindowThreadProcessId(handle, &processId);
   if(data.processId != processId || !I_isMainWindow(handle))
      return TRUE;

   data.bestHandle = handle;
   g_mainWindow = data.bestHandle;
   return FALSE;
}

//
// For window getting
//
static bool I_findMainWindow()
{
   //SDL_SysWMinfo wminfo;
   //if(SDL_GetWMInfo(&wminfo) == -1)
   //{
   //   puts(SDL_GetError());
   //   return false;
   //}

   handledata_t data;
   data.processId = GetCurrentProcessId();
   data.bestHandle = 0;
   if(!EnumWindows(I_enumWindowsCallback, (LPARAM)&data))
   {
      return GetLastError() == 0;
   }

   g_mainWindow = data.bestHandle;
   return true;
}

//=============================================================================
//
// Callbacks
//

//
// When snapshot is committed
//
static void TX_CALLCONVENTION
I_tobiiSnapshotCommitted(TX_CONSTHANDLE hAsyncData, TX_USERPARAM param)
{
   TX_RESULT result;
   TX_RESULT asyncResult = TX_RESULT_UNKNOWN;
   result = txGetAsyncDataResultCode(hAsyncData, &asyncResult);
   if(result != TX_RESULT_OK)
      puts("FAILED OBTAINING ASYNC DATA RESULT CODE");
   if(asyncResult == TX_RESULT_OK)
      puts("OK commmitting");
   else if(asyncResult == TX_RESULT_CANCELLED)
      puts("CANCELLED committing");
   else
      printf("FAILED committing (%d)\n", asyncResult);
}

//
// Called when connection to server changed
//
static void TX_CALLCONVENTION 
I_tobiiConnectionStateChanged(TX_CONNECTIONSTATE state, TX_USERPARAM userParam)
{
   TX_RESULT result;

   switch(state)
   {
   case TX_CONNECTIONSTATE_CONNECTED:
      puts("Connected");
      result = txCommitSnapshotAsync(g_globalInteractorSnapshot, I_tobiiSnapshotCommitted, nullptr);
      if(result != TX_RESULT_OK)
         puts("FAILED INITIALIZING DATA STREAM");
      break;
   case TX_CONNECTIONSTATE_DISCONNECTED:
      puts("Disconnected");
      break;
   case TX_CONNECTIONSTATE_TRYINGTOCONNECT:
      puts("Trying to connect");
      break;
   case TX_CONNECTIONSTATE_SERVERVERSIONTOOLOW:
      // TODO: print warning message
      puts("Server version too low");
      break;
   case TX_CONNECTIONSTATE_SERVERVERSIONTOOHIGH:
      // TODO: print warning message
      puts("Server version too high");
      break;
   }
}

//
// Handle events
//
static void TX_CALLCONVENTION I_tobiiHandleEvent(TX_CONSTHANDLE hAsyncData, 
   TX_USERPARAM userParam)
{
   TX_HANDLE event = TX_EMPTY_HANDLE;
   TX_HANDLE behavior = TX_EMPTY_HANDLE;

   TX_RESULT result;

   result = txGetAsyncDataContent(hAsyncData, &event);
   if(result != TX_RESULT_OK)
      return;

   result = txGetEventBehavior(event, &behavior, TX_BEHAVIORTYPE_FIXATIONDATA);
   if(result != TX_RESULT_OK)
   {
      txReleaseObject(&event);
      return;
   }

   I_onFixationDataEvent(behavior);

   txReleaseObject(&behavior);
   txReleaseObject(&event);
}

//=============================================================================
//
// Public API
//

//
// Checks whether Tobii eye-tracking is available, not installed at all or
// an error occurred.
//
i_tobiiAvail I_TobiiIsAvailable()
{
   TX_EYEXAVAILABILITY avail;
   TX_RESULT result = txGetEyeXAvailability(&avail);
   if(result != TX_RESULT_OK)
      return i_tobiiAvail::error;

   switch(avail)
   {
   case TX_EYEXAVAILABILITY_NOTAVAILABLE:
      return i_tobiiAvail::notInstalled;
   case TX_EYEXAVAILABILITY_NOTRUNNING:
      return i_tobiiAvail::notRunning;
   case TX_EYEXAVAILABILITY_RUNNING:
      return i_tobiiAvail::yes;
   }

   return i_tobiiAvail::error;
}

//
// Finds the main window again
//
void I_TobiiUpdateWindow()
{
   I_findMainWindow();
}

//
// Assuming it's available, initialize it
//
bool I_TobiiInit()
{
   if(g_context)
      return false;

   TX_EYEXCOMPONENTOVERRIDEFLAGS flags = TX_EYEXCOMPONENTOVERRIDEFLAG_NONE;
   TX_RESULT result;

   result = txInitializeEyeX(flags, nullptr, nullptr, nullptr, nullptr);
   if(result != TX_RESULT_OK)
      return false;

   result = txCreateContext(&g_context, TX_FALSE);
   if(result != TX_RESULT_OK)
   {
      I_TobiiShutdown();
      return false;
   }
   if(!I_initializeGlobalInteractorSnapshot())
   {
      I_TobiiShutdown();
      return false;
   }

   result = txRegisterConnectionStateChangedHandler(g_context, 
      &g_connectionStateChangedTicket, I_tobiiConnectionStateChanged, 
      nullptr);
   if(result != TX_RESULT_OK)
   {
      I_TobiiShutdown();
      return false;
   }

   result = txRegisterEventHandler(g_context, &g_eventHandlerTicket,
      I_tobiiHandleEvent, nullptr);
   if(result != TX_RESULT_OK)
   {
      I_TobiiShutdown();
      return false;
   }

   result = txEnableConnection(g_context);
   if(result != TX_RESULT_OK)
   {
      I_TobiiShutdown();
      return false;
   }

   return true;
}

//
// Checks if there's an event
//
bool I_TobiiGetEvent(double &x, double &y)
{
   std::lock_guard<std::mutex> lock(g_eventMutex);
   if(!g_event.fired)
      return false;
   g_event.fired = false;
   x = g_event.x;
   y = g_event.y;
   return true;
}

//
// Shuts down whatever was opened by I_TobiiInit
//
void I_TobiiShutdown()
{
   if(!g_context)
      return;

   txDisableConnection(g_context);
   
   if(g_eventHandlerTicket)
   {
      txUnregisterEventHandler(g_context, g_eventHandlerTicket);
      g_eventHandlerTicket = 0;
   }
   if(g_connectionStateChangedTicket)
   {
      txUnregisterConnectionStateChangedHandler(g_context, g_connectionStateChangedTicket);
      g_connectionStateChangedTicket = 0;
   }
   
   txReleaseObject(&g_globalInteractorSnapshot);
   txShutdownContext(g_context, TX_CLEANUPTIMEOUT_DEFAULT, TX_FALSE);
   txReleaseContext(&g_context);
   txUninitializeEyeX();
}