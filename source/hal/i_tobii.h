#ifndef I_TOBII_H_
#define I_TOBII_H_

//
// Engine availability
//
enum class i_tobiiAvail
{
   yes,           // yes and active
   notRunning,    // installed but not running
   notInstalled,  // not even installed
   error          // error getting data (assume negative)
};

i_tobiiAvail I_TobiiIsAvailable();
bool I_TobiiInit();
void I_TobiiUpdateWindow();
bool I_TobiiGetEvent(double &x, double &y);
void I_TobiiShutdown();

#endif
