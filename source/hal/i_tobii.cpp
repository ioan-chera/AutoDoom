#include "eyex/EyeX.h"

#include "../z_zone.h"
#include "i_tobii.h"
#include "../m_qstr.h"

static const TX_CONSTSTRING g_interactorId = "EyeId";

static TX_CONTEXTHANDLE g_context;
static TX_TICKET g_connectionStateChangedTicket;
static TX_TICKET g_queryTicket;

//=============================================================================
//
// Helpers
//

//
// Gets the window id conveniently
//
static bool I_getWindowId(TX_HANDLE snapshot, qstring &windowId)
{
   TX_RESULT result;
   TX_SIZE size = 0;
   result = txGetSnapshotWindowId(snapshot, 0, nullptr, &size);
   if(result != TX_RESULT_OK)
   {
      return false;
   }
   windowId.createSize(size);
   result = txGetSnapshotWindowId(snapshot, 0, windowId.getBuffer(), &size);
   if(result != TX_RESULT_OK)
   {
      return false;
   }
   return true;
}

//=============================================================================
//
// Callbacks
//

//
// Called when connection to server changed
//
static void TX_CALLCONVENTION 
I_tobiiConnectionStateChanged(TX_CONNECTIONSTATE state, TX_USERPARAM userParam)
{
   // TODO: report on state
   switch(state)
   {
   case TX_CONNECTIONSTATE_CONNECTED:
      puts("Connected");
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
// Called when getting queried by server
//
static void TX_CALLCONVENTION I_tobiiQuery(
   TX_CALLBACK_PARAM(TX_CONSTHANDLE) hAsyncData, TX_USERPARAM userParam)
{
   TX_RESULT result;
   TX_HANDLE query = nullptr;
   TX_HANDLE bounds = nullptr;
   TX_HANDLE snapshot = nullptr;
   TX_HANDLE interactor = nullptr;
   qstring windowId;

   result = txGetAsyncDataContent(hAsyncData, &query);
   if(result != TX_RESULT_OK)
   {
      puts("Failed getting content");
      return;
   }

   result = txGetQueryBounds(query, &bounds);
   if(result != TX_RESULT_OK)
   {
      puts("Failed getting query bounds");
      txReleaseObject(&query);
      return;
   }
   
   TX_REAL x = 0, y = 0, width = 0, height = 0;

   result = txGetRectangularBoundsData(bounds, &x, &y, &width, &height);
   if(result != TX_RESULT_OK)
   {
      puts("Failed getting rectangular bounds data");
      txReleaseObject(&bounds);
      txReleaseObject(&query);
      return;
   }

   printf("%g %g %g %g\n", x, y, width, height);

   txReleaseObject(&bounds);
   txReleaseObject(&query);

   //result = txCreateSnapshotForQuery(hAsyncData, &snapshot);
   //if(result != TX_RESULT_OK)
   //{
   //   puts("Failed getting snapshot for query");
   //   txReleaseObject(&snapshot);
   //   txReleaseObject(&bounds);
   //   return;
   //}

   //if(!I_getWindowId(snapshot, windowId))
   //{
   //   puts("Failed getting snapshot for query");
   //   txReleaseObject(&snapshot);
   //   txReleaseObject(&bounds);
   //   return;
   //}

   //result = txCreateInteractor(snapshot, &interactor, g_interactorId, 
   //   TX_LITERAL_ROOTID, windowId.constPtr());
   //if(result != TX_RESULT_OK)
   //{
   //   puts("Failed getting snapshot for query");
   //   txReleaseObject(&snapshot);
   //   txReleaseObject(&bounds);
   //   return;
   //}
   //// TODO: behaviors and stuff
   //

   //result = txCommitSnapshotAsync(snapshot, nullptr, nullptr);
   //if(result != TX_RESULT_OK)
   //{
   //   puts("Failed committing snapshot");
   //   txReleaseObject(&interactor);
   //   txReleaseObject(&snapshot);
   //   txReleaseObject(&bounds);
   //   return;
   //}
   //// TODO: cleanup


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
      return false;

   result = txRegisterConnectionStateChangedHandler(g_context, 
      &g_connectionStateChangedTicket, I_tobiiConnectionStateChanged, 
      nullptr);
   if(result != TX_RESULT_OK)
   {
      I_TobiiShutdown();
      return false;
   }

   result = txRegisterQueryHandler(g_context, &g_queryTicket, I_tobiiQuery, 
      nullptr);
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
// Shuts down whatever was opened by I_TobiiInit
//
void I_TobiiShutdown()
{
   if(!g_context)
      return;
   if(g_queryTicket)
   {
      txUnregisterQueryHandler(g_context, g_queryTicket);
      g_queryTicket = 0;
   }
   if(g_connectionStateChangedTicket)
   {
      txUnregisterConnectionStateChangedHandler(g_context, g_connectionStateChangedTicket);
      g_connectionStateChangedTicket = 0;
   }
   // TODO: txShutdownContext
   txReleaseContext(&g_context);
}