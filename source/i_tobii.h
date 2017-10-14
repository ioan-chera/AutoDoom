//
// The Eternity Engine
// Copyright (C) 2017 James Haley et al.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//
// Additional terms and conditions compatible with the GPLv3 apply. See the
// file COPYING-EE for details.
//
// Purpose: Tobii eye tracker Stream Engine support
// Authors: Ioan Chera
//

#ifndef I_TOBII2_H_
#define I_TOBII2_H_

enum
{
   EYE_EVENT_GAZE =     1,
   EYE_EVENT_PRESENCE = 2
};

bool I_EyeInit();
void I_EyeAttachToWindow();
void I_EyeGetEvent(double &x, double &y, bool &presence, unsigned &eventGot);
void I_EyeShutdown();

#endif

// EOF
