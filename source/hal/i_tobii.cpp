#include "eyex/EyeX.h"

#include "../z_zone.h"
#include "i_tobii.h"

static TX_CONTEXTHANDLE g_context;

static void TX_CALLCONVENTION 
I_tobiiConnectionStateChanged(TX_CONNECTIONSTATE state, TX_USERPARAM userParam)
{
   // TODO: report on state
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

   TX_CONTEXTHANDLE context = nullptr;
   
   result = txCreateContext(&context, TX_FALSE);
   if(result != TX_RESULT_OK)
      return false;

   TX_TICKET ticket = 0;

   result = txRegisterConnectionStateChangedHandler(context, &ticket, 
      I_tobiiConnectionStateChanged, nullptr);
   if(result != TX_RESULT_OK)
   {
      txReleaseContext(&context);
      return false;
   }



   g_context = context;

   return true;
}

//
// Shuts down whatever was opened by I_TobiiInit
//
void I_TobiiShutdown()
{
   if(g_context)
      txReleaseContext(&g_context);
}