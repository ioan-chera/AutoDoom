// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Copyright(C) 2020 Ioan Chera
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
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//      Bot miscellaenous learning
//
//-----------------------------------------------------------------------------

#include "../z_zone.h"

#include "b_learn.h"
#include "../i_system.h"
#include "../m_compare.h"

static int b_learn = -1;
static int b_kills;
static int b_takenDamage;

//
// Enables learning, with the given value
//
void B_EnableLearning(int value)
{
   b_learn = eclamp(value, 0, 8);
}

//
// Returns the current learning value
//
int B_GetLearningValue()
{
   return b_learn;
}

//
// Returns learning time
//
int B_GetLearningTime()
{
   return 5 * 60 * 35;
}

//
// Adds a kill to the list
//
void B_AddLearningKill()
{
   ++b_kills;
}

//
// Adds taken damage
//
void B_AddLearningTakenDamage(int damage)
{
   b_takenDamage += damage;
}

//
// Exits with the conclusion
//
void B_LearningConclude()
{
   I_Error("Weapon %d made %d kills and player suffered %d damage.\n", b_learn, b_kills,
           b_takenDamage);
}
