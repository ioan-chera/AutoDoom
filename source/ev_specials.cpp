// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 2013 James Haley et al.
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
//   Generalized line action system
//
//-----------------------------------------------------------------------------

#include "z_zone.h"

#include "autodoom/b_botmap.h"
#include "autodoom/b_think.h"
#include "acs_intr.h"
#include "c_io.h"
#include "c_runcmd.h"
#include "d_dehtbl.h"
#include "d_gi.h"
#include "doomstat.h"
#include "e_exdata.h"
#include "e_hash.h"
#include "e_inventory.h"
#include "ev_actions.h"
#include "ev_macros.h"
#include "ev_specials.h"
#include "ev_bindings.h"
#include "g_game.h"
#include "p_info.h"
#include "p_mobj.h"
#include "p_setup.h"
#include "p_skin.h"
#include "p_spec.h"
#include "p_xenemy.h"
#include "polyobj.h"
#include "r_data.h"
#include "r_defs.h"
#include "r_state.h"
#include "s_sound.h"

#define INIT_STRUCT edefstructvar

//=============================================================================
//
// Utilities
//

//
// EV_ClearSwitchOnFail
//
// haleyjd 08/29/09: Replaces demo_compatibility checks for clearing 
// W1/S1/G1 line actions on action failure, because it makes some maps
// unplayable if it is disabled unconditionally outside of demos.
//
inline static bool EV_ClearSwitchOnFail(void)
{
   return demo_compatibility || (demo_version >= 335 && comp[comp_special]);
}

//
// EV_Check3DMidTexSwitch
//
// haleyjd 12/02/12: split off 3DMidTex switch range testing into its own
// independent routine.
//
static bool EV_Check3DMidTexSwitch(line_t *line, Mobj *thing, int side)
{
   int     sidenum = line->sidenum[side];
   side_t *sidedef = NULL;

   if(demo_version < 331)
      return true; // 3DMidTex don't exist in old demos

   if(sidenum != -1)
      sidedef = &sides[sidenum];

   // SoM: only allow switch specials on 3d sides to be triggered if 
   // the mobj is within range of the side.
   // haleyjd 05/02/06: ONLY on two-sided lines.
   if((line->flags & ML_3DMIDTEX) && line->backsector && 
      sidedef && sidedef->midtexture)
   {
      fixed_t opentop, openbottom, textop, texbot;

      opentop = line->frontsector->ceilingheight < line->backsector->ceilingheight ?
                line->frontsector->ceilingheight : line->backsector->ceilingheight;
      
      openbottom = line->frontsector->floorheight > line->backsector->floorheight ?
                   line->frontsector->floorheight : line->backsector->floorheight;

      if(line->flags & ML_DONTPEGBOTTOM)
      {
         texbot = sidedef->rowoffset + openbottom;
         textop = texbot + textures[sidedef->midtexture]->heightfrac;
      }
      else
      {
         textop = opentop + sidedef->rowoffset;
         texbot = textop - textures[sidedef->midtexture]->heightfrac;
      }

      if(thing->z > textop || thing->z + thing->height < texbot)
         return false;
   }

   return true;
}

//=============================================================================
// 
// Null Activation Helpers - They do nothing.
//

static bool EV_NullPreCrossLine(ev_action_t *action, ev_instance_t *instance)
{
   return false;
}

static int EV_NullPostCrossLine(ev_action_t *action, int result,
                                ev_instance_t *instance)
{
   return false;
}

//=============================================================================
//
// DOOM Activation Helpers - Preambles and Post-Actions
//

//
// EV_DOOMPreCrossLine
//
// This function is used as the preamble to all DOOM cross-type line specials.
//
static bool EV_DOOMPreCrossLine(ev_action_t *action, ev_instance_t *instance)
{
   unsigned int flags = EV_CompositeActionFlags(action);

   // DOOM-style specials require an actor and a line
   REQUIRE_ACTOR(instance->actor);
   REQUIRE_LINE(instance->line);

   // Things that should never trigger lines
   if(!instance->actor->player)
   {
      // haleyjd: changed to check against MF2_NOCROSS flag instead of 
      // switching on type.
      if(instance->actor->flags2 & MF2_NOCROSS)
         return false;
   }

   // Check if action only allows player
   // jff 3/5/98 add ability of monsters etc. to use teleporters
   if(!instance->actor->player && !(flags & EV_PREALLOWMONSTERS))
      return false;

   // jff 2/27/98 disallow zero tag on some types
   // killough 11/98: compatibility option:
   if(!(instance->tag || comp[comp_zerotags] || (flags & EV_PREALLOWZEROTAG)))
      return false;

   // check for first-side-only instance
   if(instance->side && (flags & EV_PREFIRSTSIDEONLY))
      return false;

   // line type is *only* for monsters?
   if(instance->actor->player && (flags & EV_PREMONSTERSONLY))
      return false;

   return true;
}

//
// EV_DOOMPostCrossLine
//
// This function is used as the post-action for all DOOM cross-type line
// specials.
//
static int EV_DOOMPostCrossLine(ev_action_t *action, int result,
                                ev_instance_t *instance)
{
   unsigned int flags = EV_CompositeActionFlags(action);

   if(flags & EV_POSTCLEARSPECIAL)
   {
      bool clearSpecial = (result || EV_ClearSwitchOnFail());

      if(clearSpecial || (flags & EV_POSTCLEARALWAYS))
         instance->line->special = 0;
   }

   return result;
}

//
// EV_DOOMPreUseLine
//
// Preamble for DOOM-type use (switch or manual) line types.
//
static bool EV_DOOMPreUseLine(ev_action_t *action, ev_instance_t *instance)
{
   unsigned int flags = EV_CompositeActionFlags(action);

   Mobj   *thing = instance->actor;
   line_t *line  = instance->line;

   // actor and line are required
   REQUIRE_ACTOR(thing);
   REQUIRE_LINE(line);

   // All DOOM-style use specials only support activation from the first side
   if(instance->side) 
      return false;

   // Check for 3DMidTex range restrictions
   if(!EV_Check3DMidTexSwitch(line, thing, instance->side))
      return false;

   if(!thing->player)
   {
      // Monsters never activate use specials on secret lines
      if(line->flags & ML_SECRET)
         return false;

      // Otherwise, check the special flags
      if(!(flags & EV_PREALLOWMONSTERS))
         return false;
   }

   // check for zero tag
   if(!(instance->tag || comp[comp_zerotags] || (flags & EV_PREALLOWZEROTAG)))
      return false;

   return true;
}

//
// EV_DOOMPostUseLine
//
// Post-activation semantics for DOOM-style use line actions.
//
static int EV_DOOMPostUseLine(ev_action_t *action, int result,
                              ev_instance_t *instance)
{
   unsigned int flags = EV_CompositeActionFlags(action);

   // check for switch texture changes
   if(flags & EV_POSTCHANGESWITCH)
   {
      if(result || (flags & EV_POSTCHANGEALWAYS))
      {
         int useAgain   = !(flags & EV_POSTCLEARSPECIAL);
         int changeSide = (flags & EV_POSTCHANGESIDED) ? instance->side : 0;
         P_ChangeSwitchTexture(instance->line, useAgain, changeSide);
      }
   }

   // FIXME/TODO: ideally we return result; the vanilla code always returns true
   // if the line action was *attempted*, not if it succeeded. Fixing this will
   // require a new comp flag.
   
   //return result;
   return true; // temporary
}

//
// EV_DOOMPreShootLine
//
// Pre-activation semantics for DOOM gun-type actions
//
static bool EV_DOOMPreShootLine(ev_action_t *action, ev_instance_t *instance)
{
   unsigned int flags = EV_CompositeActionFlags(action);

   Mobj   *thing = instance->actor;
   line_t *line  = instance->line;

   // actor and line are required
   REQUIRE_ACTOR(thing);
   REQUIRE_LINE(line);

   if(!thing->player)
   {
      // Check if special allows monsters
      if(!(flags & EV_PREALLOWMONSTERS))
         return false;
   }

   // check for zero tag
   if(!(instance->tag || comp[comp_zerotags] || (flags & EV_PREALLOWZEROTAG)))
      return false;

   return true;
}

//
// EV_DOOMPostShootLine
//
// Post-activation semantics for DOOM-style use line actions.
//
static int EV_DOOMPostShootLine(ev_action_t *action, int result,
                                ev_instance_t *instance)
{
   unsigned int flags = EV_CompositeActionFlags(action);

   // check for switch texture changes
   if(flags & EV_POSTCHANGESWITCH)
   {
      // Non-BOOM gun line types may clear their special even if they fail
      if(result || (flags & EV_POSTCHANGEALWAYS) || 
         (action->minversion < 200 && EV_ClearSwitchOnFail()))
      {
         int useAgain   = !(flags & EV_POSTCLEARSPECIAL);
         int changeSide = (flags & EV_POSTCHANGESIDED) ? instance->side : 0;
         P_ChangeSwitchTexture(instance->line, useAgain, changeSide);
      }
   }

   return result;
}

//=============================================================================
//
// BOOM Generalized Pre- and Post-Actions
//
// You'd think these could all be very simply combined into each other, but,
// thanks to a reckless implementation by the BOOM team, not quite.
//

// Obtain an EDF lockdef ID for a generalized lock number
static int lockdefIDForGenLock[MaxKeyKind][2] =
{
   // !skulliscard,           skulliscard
   {  EV_LOCKDEF_ANYKEY,      EV_LOCKDEF_ANYKEY   }, // AnyKey
   {  EV_LOCKDEF_REDCARD,     EV_LOCKDEF_REDGREEN }, // RCard
   {  EV_LOCKDEF_BLUECARD,    EV_LOCKDEF_BLUE     }, // BCard
   {  EV_LOCKDEF_YELLOWCARD,  EV_LOCKDEF_YELLOW   }, // YCard
   {  EV_LOCKDEF_REDSKULL,    EV_LOCKDEF_REDGREEN }, // RSkull
   {  EV_LOCKDEF_BLUESKULL,   EV_LOCKDEF_BLUE     }, // BSkull
   {  EV_LOCKDEF_YELLOWSKULL, EV_LOCKDEF_YELLOW   }, // YSkull
   {  EV_LOCKDEF_ALL6,        EV_LOCKDEF_ALL3     }, // AllKeys
};

//
// EV_lockdefIDForGenSpec
//
// Get the lockdef ID used for a generalized line special.
//
static int EV_lockdefIDForGenSpec(int special)
{
   // does this line special distinguish between skulls and keys?
   int skulliscard = (special & LockedNKeys) >> LockedNKeysShift;
   int genLock     = (special & LockedKey  ) >> LockedKeyShift;
   
   return lockdefIDForGenLock[genLock][skulliscard];
}

//
// EV_canUnlockGenDoor()
//
// Passed a generalized locked door linedef and a player, returns whether
// the player has the keys necessary to unlock that door.
//
// Note: The linedef passed MUST be a generalized locked door type
//       or results are undefined.
//
// jff 02/05/98 routine added to test for unlockability of
//  generalized locked doors
//
// killough 11/98: reformatted
//
// haleyjd 08/22/00: fixed bug found by fraggle
//
static bool EV_canUnlockGenDoor(line_t *line, player_t *player)
{
   int lockdefID = EV_lockdefIDForGenSpec(line->special);

   itemeffect_t *yskull = E_ItemEffectForName(ARTI_YELLOWSKULL);
   bool hasYellowSkull  = (E_GetItemOwnedAmount(player, yskull) > 0);

   // MBF compatibility hack - if lockdef ID is ALL3 and the player has the 
   // YellowSkull, we have to take it away; if he doesn't have it, we have to 
   // give it.
   if(demo_version == 203 && lockdefID == EV_LOCKDEF_ALL3)
   {
      if(hasYellowSkull)
         E_RemoveInventoryItem(player, yskull, -1);
      else
         E_GiveInventoryItem(player, yskull);
   }

   bool result = E_PlayerCanUnlock(player, lockdefID, false);

   // restore inventory state if lockdef was ALL3 and playing back an MBF demo
   if(demo_version == 203 && lockdefID == EV_LOCKDEF_ALL3)
   {
      if(hasYellowSkull)
         E_GiveInventoryItem(player, yskull);
      else
         E_RemoveInventoryItem(player, yskull, -1);
   }

   return result;
}

//
// EV_BOOMGenPreActivate
//
// Pre-activation logic for BOOM generalized line types
//
static bool EV_BOOMGenPreActivate(ev_action_t *action, ev_instance_t *instance)
{
   Mobj   *thing = instance->actor;
   line_t *line  = instance->line;
   
   REQUIRE_ACTOR(thing);
   REQUIRE_LINE(line);

   // check against zero tags
   if(!line->args[0])
   {
      switch(instance->genspac)
      {
      case WalkOnce:
      case WalkMany:
         // jff 2/27/98 all walk generalized types require tag
         // haleyjd 12/01/12: except not, Jim, because you forgot locked doors -_-
         if(instance->gentype != GenTypeLocked)
            return false;
         break;
      case SwitchOnce:
      case SwitchMany:
      case PushOnce:
      case PushMany:
         // jff 3/2/98 all non-manual generalized types require tag
         if((line->special & 6) != 6)
            return false;
         break;
      case GunOnce:
      case GunMany:
         // jff 2/27/98 all gun generalized types require tag
         // haleyjd 12/01/12: except this time you forgot lifts :/
         if(instance->gentype != GenTypeLift)
            return false;
         break;
      default:
         break;
      }
   }

   // check whether monsters are allowed or not
   if(!thing->player)
   {
      switch(instance->gentype)
      {
      case GenTypeFloor:
         // FloorModel is "Allow Monsters" if FloorChange is 0
         if((line->special & FloorChange) || !(line->special & FloorModel))
            return false;
         break;
      case GenTypeCeiling:
         // CeilingModel is "Allow Monsters" if CeilingChange is 0
         if((line->special & CeilingChange) || !(line->special & CeilingModel))
            return false; 
         break;
      case GenTypeDoor:
         if(!(line->special & DoorMonster))
            return false;            // monsters disallowed from this door
         if(line->flags & ML_SECRET) // they can't open secret doors either
            return false;
         break;
      case GenTypeLocked:
         return false; // monsters disallowed from unlocking doors
      case GenTypeLift:
         if(!(line->special & LiftMonster))
            return false; // monsters disallowed
         break;
      case GenTypeStairs:
         if(!(line->special & StairMonster))
            return false; // monsters disallowed
         break;
      case GenTypeCrusher:
         if(!(line->special & CrusherMonster))
            return false; // monsters disallowed
         break;
      default:
         break;
      }
   }

   // check each range of generalized linedefs (special checks)
   switch(instance->gentype)
   {
   case GenTypeLocked:
      if(thing->player && !EV_canUnlockGenDoor(line, thing->player))
         return false;
      break;
   case GenTypeCrusher:
      // haleyjd 06/09/09: This was completely forgotten in BOOM, disabling
      // all generalized walk-over crusher types!
      if((instance->genspac == WalkOnce || instance->genspac == WalkMany)
         && demo_version < 335)
         return false;
      break;
   default:
      break;
   }

   // check for line side?
   // NB: BOOM code checked specially for Push types, but, that check was
   // redundant to one at the top of P_UseSpecialLine that applies to 
   // everything.
   if(instance->side)
   {
      switch(instance->genspac)
      {
      case PushOnce:
      case PushMany:
      case SwitchOnce:
      case SwitchMany:
         return false; // activate from first side only.
      default:
         break;
      }
   }

   // check for 3DMidTex switch restrictions
   switch(instance->genspac)
   {
   case PushOnce:
   case PushMany:
   case SwitchOnce:
   case SwitchMany:
      if(!EV_Check3DMidTexSwitch(line, thing, instance->side))
         return false;
   }

   return true;
}

//
// EV_BOOMGenPostActivate
//
// Post-activation logic for BOOM generalized line types
//
static int EV_BOOMGenPostActivate(ev_action_t *action, int result,
                                  ev_instance_t *instance)
{
   if(result)
   {
      switch(instance->genspac)
      {
      case GunOnce:
      case SwitchOnce:
         P_ChangeSwitchTexture(instance->line, 0, 0);
         break;
      case GunMany:
      case SwitchMany:
         P_ChangeSwitchTexture(instance->line, 1, 0);
         break;
      case WalkOnce:
      case PushOnce:
         instance->line->special = 0;
         break;
      default:
         break;
      }
   }

   // FIXME/TODO: P_Cross and P_Shoot have always been void, and P_UseSpecialLine
   // just returns true if an action is attempted, not if it succeeds. Until a
   // comp var is added, we return true from here instead of forwarding on the
   // result.
   
   //return result;
   return true; // temporary
}

//=============================================================================
//
// Parameterized Pre- and Post-Actions
//

//
// EV_ParamPreActivate
//
// The only check to make here is for 3DMidTex switches. The rest of the logic
// has been implemented in EV_checkSpac.
//
static bool EV_ParamPreActivate(ev_action_t *action, ev_instance_t *instance)
{
   if(instance->line && instance->actor && instance->spac == SPAC_USE)
   {
      if(!EV_Check3DMidTexSwitch(instance->line, instance->actor, instance->side))
         return false;
   }

   return true;
}

//
// EV_ParamPostActivate
//
// If the action was successful, and has a source linedef, check for
// reusability and for switch texture changes, depending on the spac.
//
static int EV_ParamPostActivate(ev_action_t *action, int result,
                                ev_instance_t *instance)
{
   // check for switch texture change and special clear
   if(result && instance->line)
   {
      // ioanch 20160229: do NOT reset special for non-parameterized "Start line
      // script" SR/WR/GR lines.
      const ev_action_t *action = EV_ActionForSpecial(instance->line->special);
      int reuse = (instance->line->extflags & EX_ML_REPEAT) ||
         (action && action->action == EV_ActionStartLineScript && 
                  !(action->type->flags & EV_POSTCLEARSPECIAL));

      if(!reuse)
         instance->line->special = 0;

      if(instance->spac == SPAC_USE || instance->spac == SPAC_IMPACT)
         P_ChangeSwitchTexture(instance->line, reuse, instance->side);
   }

   return result;
}

//=============================================================================
//
// Action Types
//

// The Null action type doesn't do anything.
ev_actiontype_t NullActionType =
{
   -1,
   EV_NullPreCrossLine,
   EV_NullPostCrossLine,
   0,
   "None"
};

// DOOM-Style Action Types

// WR-Type lines may be crossed multiple times
ev_actiontype_t WRActionType =
{
   SPAC_CROSS,           // line must be crossed
   EV_DOOMPreCrossLine,  // pre-activation callback
   EV_DOOMPostCrossLine, // post-activation callback
   0,                    // no default flags
   "WR"                  // display name
};

// W1-Type lines may be activated once. Semantics are implemented in the 
// post-cross callback to implement compatibility behaviors regarding the 
// premature clearing of specials crossed from the wrong side or without
// successful activation having occurred.
ev_actiontype_t W1ActionType =
{
   SPAC_CROSS,           // line must be crossed
   EV_DOOMPreCrossLine,  // pre-activation callback
   EV_DOOMPostCrossLine, // post-activation callback
   EV_POSTCLEARSPECIAL,  // special will be cleared after activation
   "W1"                  // display name
};

// SR-Type lines may be activated multiple times by using them.
ev_actiontype_t SRActionType =
{
   SPAC_USE,             // line must be used
   EV_DOOMPreUseLine,    // pre-activation callback
   EV_DOOMPostUseLine,   // post-activation callback
   EV_POSTCHANGESWITCH,  // change switch texture
   "SR"                  // display name
};

// S1-Type lines may be activated once, by using them.
ev_actiontype_t S1ActionType =
{
   SPAC_USE,             // line must be used
   EV_DOOMPreUseLine,    // pre-activation callback
   EV_DOOMPostUseLine,   // post-activation callback
   EV_POSTCHANGESWITCH | EV_POSTCLEARSPECIAL, // change switch and clear special
   "S1"                  // display name
};

// DR-Type lines may be activated repeatedly by pushing; the main distinctions
// from switch-types are that switch textures are not changed and the sector
// target is always the line's backsector. Therefore the tag can be zero, and
// when not zero, is recycled by BOOM to allow a special lighting effect.
ev_actiontype_t DRActionType =
{
   SPAC_USE,             // line must be used
   EV_DOOMPreUseLine,    // pre-activation callback
   EV_DOOMPostUseLine,   // post-activation callback
   EV_PREALLOWZEROTAG,   // tags are never used for activation purposes
   "DR"                  // display name
};

// GR-Type lines may be activated multiple times by hitscan attacks
// FIXME/TODO: In Raven and Rogue games, projectiles activate them as well.
ev_actiontype_t GRActionType =
{
   SPAC_IMPACT,          // line must be hit
   EV_DOOMPreShootLine,  // pre-activation callback
   EV_DOOMPostShootLine, // post-activation callback (same as use)
   EV_POSTCHANGESWITCH,  // change switch texture
   "GR"                  // display name
};

// G1-Type lines may be activated once, by a hitscan attack.
ev_actiontype_t G1ActionType = 
{
   SPAC_IMPACT,          // line must be hit
   EV_DOOMPreShootLine,  // pre-activation callback
   EV_DOOMPostShootLine, // post-activation callback
   EV_POSTCHANGESWITCH | EV_POSTCLEARSPECIAL, // change switch and clear special
   "G1"                  // display name
};

// BOOM Generalized Action Type (there is but one)

ev_actiontype_t BoomGenActionType =
{
   -1,                     // SPAC is determined by the line special
   EV_BOOMGenPreActivate,  // pre-activation callback
   EV_BOOMGenPostActivate, // post-activation callback
   0,                      // flags are not used by this type
   "Generalized"           // display name
};

// Parameterized Action Type (there is but one)

ev_actiontype_t ParamActionType =
{
   -1,                    // SPAC is determined by line flags
   EV_ParamPreActivate,   // pre-activation callback
   EV_ParamPostActivate,  // post-activation callback
   EV_PARAMLINESPEC,      // is parameterized
   "Parameterized"        // display name
};

//=============================================================================
//
// Special Resolution
//
// Lookup an action by number.
//

// Special hash table type
typedef EHashTable<ev_binding_t, EIntHashKey, &ev_binding_t::actionNumber,
                   &ev_binding_t::links> EV_SpecHash;

// By-name hash table type
typedef EHashTable<ev_binding_t, ENCStringHashKey, &ev_binding_t::name,
                   &ev_binding_t::namelinks> EV_SpecNameHash;

// Hash tables for binding arrays
static EV_SpecHash DOOMSpecHash;
static EV_SpecHash HereticSpecHash;
static EV_SpecHash HexenSpecHash;
static EV_SpecHash UDMFEternitySpecHash;
static EV_SpecHash ACSSpecHash;

static EV_SpecNameHash DOOMSpecNameHash;
static EV_SpecNameHash HexenSpecNameHash;
static EV_SpecNameHash UDMFEternitySpecNameHash;

//
// EV_initSpecHash
//
// Initialize a special hash table.
//
static void EV_initSpecHash(EV_SpecHash &hash, ev_binding_t *bindings, 
                            size_t numBindings)
{
   hash.initialize((unsigned int)numBindings);

   for(size_t i = 0; i < numBindings; i++)
      hash.addObject(bindings[i]);
}

//
// EV_initSpecNameHash
//
// Initialize a by-name lookup hash table.
//
static void EV_initSpecNameHash(EV_SpecNameHash &hash, ev_binding_t *bindings,
                                size_t numBindings)
{
   hash.initialize((unsigned int)numBindings);

   for(size_t i = 0; i < numBindings; i++)
   {
      if(bindings[i].name)
         hash.addObject(bindings[i]);
   }
}

//
// EV_initDoomSpecHash
//
// Initializes the DOOM special bindings hash table the first time it is 
// called.
//
static void EV_initDoomSpecHash()
{
   if(DOOMSpecHash.isInitialized())
      return;

   EV_initSpecHash    (DOOMSpecHash,     DOOMBindings, DOOMBindingsLen);
   EV_initSpecNameHash(DOOMSpecNameHash, DOOMBindings, DOOMBindingsLen);
}

//
// EV_initHereticSpecHash
//
// Initializes the Heretic special bindings hash table the first time it is 
// called. Also inits DOOM bindings.
//
static void EV_initHereticSpecHash()
{
   if(HereticSpecHash.isInitialized())
      return;

   EV_initSpecHash(HereticSpecHash, HereticBindings, HereticBindingsLen);
   EV_initDoomSpecHash(); // initialize DOOM bindings as well
}

//
// EV_initHexenSpecHash
//
// Initializes the Hexen special bindings hash table the first time it is
// called.
//
static void EV_initHexenSpecHash()
{
   if(HexenSpecHash.isInitialized())
      return;

   EV_initSpecHash    (HexenSpecHash,     HexenBindings, HexenBindingsLen);
   EV_initSpecNameHash(HexenSpecNameHash, HexenBindings, HexenBindingsLen);
}

//
// EV_initUDMFEternitySpecHash
//
// Initializes the UDMF special bindings hash table the first time it is
// called.
//
static void EV_initUDMFEternitySpecHash()
{
   if (UDMFEternitySpecHash.isInitialized())
      return;

   EV_initSpecHash(UDMFEternitySpecHash, UDMFEternityBindings, UDMFEternityBindingsLen);
   EV_initSpecNameHash(UDMFEternitySpecNameHash, UDMFEternityBindings, UDMFEternityBindingsLen);
   EV_initHexenSpecHash();
}

//
// Initializes the ACS special bindings hash table the first time it is
// called.
//
static void EV_initACSSpecHash()
{
   if(ACSSpecHash.isInitialized())
      return;

   EV_initSpecHash(ACSSpecHash, ACSBindings, ACSBindingsLen);
   EV_initUDMFEternitySpecHash();
}

//
// EV_DOOMBindingForSpecial
//
// Returns a special binding from the DOOM gamemode's bindings array, 
// regardless of the current gamemode or map format. Returns NULL if
// the special is not bound to an action.
//
ev_binding_t *EV_DOOMBindingForSpecial(int special)
{
   EV_initDoomSpecHash(); // ensure table is initialized

   return DOOMSpecHash.objectForKey(special);
}

//
// EV_DOOMBindingForName
//
// Returns a special binding from the DOOM gamemode's bindings array
// by name.
//
ev_binding_t *EV_DOOMBindingForName(const char *name)
{
   EV_initDoomSpecHash();

   return DOOMSpecNameHash.objectForKey(name);
}

//
// EV_DOOMActionForSpecial
//
// Returns an action from the DOOM gamemode's bindings array, regardless
// of the current gamemode or map format. Returns NULL if the special is
// not bound to an action.
//
ev_action_t *EV_DOOMActionForSpecial(int special)
{
   ev_binding_t *bind = EV_DOOMBindingForSpecial(special);

   return bind ? bind->action : NULL;
}

//
// EV_HereticBindingForSpecial
//
// Returns a special binding from the Heretic gamemode's bindings array,
// regardless of the current gamemode or map format. Returns NULL if
// the special is not bound to an action.
//
ev_binding_t *EV_HereticBindingForSpecial(int special)
{
   ev_binding_t *bind;

   EV_initHereticSpecHash(); // ensure table is initialized

   // Try Heretic's bindings first. If nothing is found, defer to DOOM's 
   // bindings.
   if(!(bind = HereticSpecHash.objectForKey(special)))
      bind = DOOMSpecHash.objectForKey(special);

   return bind;
}

//
// EV_HereticActionForSpecial
//
// Returns an action from the Heretic gamemode's bindings array,
// regardless of the current gamemode or map format. Returns NULL if
// the special is not bound to an action.
//
ev_action_t *EV_HereticActionForSpecial(int special)
{
   ev_binding_t *bind = EV_HereticBindingForSpecial(special);

   return bind ? bind->action : NULL;
}

//
// EV_HexenBindingForSpecial
//
// Returns a special binding from the Hexen gamemode's bindings array,
// regardless of the current gamemode or map format. Returns NULL if
// the special is not bound to an action.
//
ev_binding_t *EV_HexenBindingForSpecial(int special)
{
   EV_initHexenSpecHash(); // ensure table is initialized

   return HexenSpecHash.objectForKey(special);
}

//
// EV_HexenBindingForName
//
// Returns a special binding from the Hexen gamemode's bindings array
// by name.
//
ev_binding_t *EV_HexenBindingForName(const char *name)
{
   EV_initHexenSpecHash();

   return HexenSpecNameHash.objectForKey(name);
}

//
// EV_HexenActionForSpecial
//
// Returns a special binding from the Hexen gamemode's bindings array,
// regardless of the current gamemode or map format. Returns NULL if
// the special is not bound to an action.
//
ev_action_t *EV_HexenActionForSpecial(int special)
{
   ev_binding_t *bind = EV_HexenBindingForSpecial(special);

   return bind ? bind->action : NULL;
}

//
// EV_StrifeActionForSpecial
//
// TODO
//
static ev_action_t *EV_StrifeActionForSpecial(int special)
{
   return NULL;
}

//
// EV_PSXBindingForSpecial
//
// Returns a special binding from the PSX mission's bindings array,
// regardless of the current gamemode or map format. Returns NULL if
// the special is not bound to an action.
//
static ev_binding_t *EV_PSXBindingForSpecial(int special)
{
   // small set, so simple linear search.
   for(size_t i = 0; i < PSXBindingsLen; i++)
   {
      if(PSXBindings[i].actionNumber == special)
         return &PSXBindings[i];
   }

   // otherwise, defer to DOOM lookup
   return EV_DOOMBindingForSpecial(special);
}

//
// EV_PSXActionForSpecial
//
// Likewise as above but returning the action pointer if the binding exists.
//
static ev_action_t *EV_PSXActionForSpecial(int special)
{
   ev_binding_t *bind = EV_PSXBindingForSpecial(special);

   return bind ? bind->action : nullptr;
}

//
// EV_UDMFEternityBindingForSpecial
//
// Returns a special binding from the UDMFEternity gamemode's bindings array,
// regardless of the current gamemode or map format. Returns NULL if
// the special is not bound to an action.
//
static ev_binding_t *EV_UDMFEternityBindingForSpecial(int special)
{
   ev_binding_t *bind;

   EV_initUDMFEternitySpecHash(); // ensure table is initialized

  // Try UDMFEternity's bindings first. If nothing is found, defer to Hexen's 
  // bindings.
   if(!(bind = UDMFEternitySpecHash.objectForKey(special)))
      bind = HexenSpecHash.objectForKey(special);

   return bind;
}

//
// EV_UDMFEternityBindingForName
//
// Returns a special binding from the UDMFEternity gamemode's bindings array
// by name.
//
static ev_binding_t *EV_UDMFEternityBindingForName(const char *name)
{
   ev_binding_t *bind;

   EV_initUDMFEternitySpecHash(); // ensure table is initialized

   // Try UDMFEternity's bindings first. If nothing is found, defer to Hexen's 
   // bindings.
   if(!(bind = UDMFEternitySpecNameHash.objectForKey(name)))
      bind = HexenSpecNameHash.objectForKey(name);

   return bind;
}

//
// EV_UDMFEternityActionForSpecial
//
// Returns a special binding from the UDMFEternity gamemode's bindings array,
// regardless of the current gamemode or map format. Returns NULL if
// the special is not bound to an action.
//
static ev_action_t *EV_UDMFEternityActionForSpecial(int special)
{
   ev_binding_t *bind = EV_UDMFEternityBindingForSpecial(special);

   return bind ? bind->action : NULL;

}


//
// Returns a special binding from the ACS gamemode's bindings array,
// regardless of the current gamemode or map format. Returns NULL if
// the special is not bound to an action.
//
static ev_binding_t *EV_ACSBindingForSpecial(int special)
{
   ev_binding_t *bind;

   EV_initACSSpecHash(); // ensure table is initialized

  // Try ACS's bindings first. If nothing is found, defer to UDMFEternity's 
  // bindings, then to Hexen's.
   if(!(bind = ACSSpecHash.objectForKey(special)))
   {
      if(!(bind = UDMFEternitySpecHash.objectForKey(special)))
         bind = HexenSpecHash.objectForKey(special);
   }

   return bind;
}
//
// Returns a special binding from the ACS's bindings array,
// regardless of the current gamemode or map format. Returns NULL if
// the special is not bound to an action.
//
ev_action_t *EV_ACSActionForSpecial(int special)
{
   ev_binding_t *bind = EV_ACSBindingForSpecial(special);

   return bind ? bind->action : NULL;

}

//
// EV_BindingForName
//
// Look up a binding by name depending on the current map format and gamemode.
//
ev_binding_t *EV_BindingForName(const char *name)
{
   if(LevelInfo.mapFormat == LEVEL_FORMAT_UDMF_ETERNITY)
      return EV_UDMFEternityBindingForName(name);
   else if(LevelInfo.mapFormat == LEVEL_FORMAT_HEXEN)
      return EV_HexenBindingForName(name);
   else
      return EV_DOOMBindingForName(name);
}

//
// EV_GenTypeForSpecial
//
// Get a GenType enumeration value given a line special
//
int EV_GenTypeForSpecial(int special)
{
   // Floors
   if(special >= GenFloorBase)
      return GenTypeFloor;

   // Ceilings
   if(special >= GenCeilingBase)
      return GenTypeCeiling;

   // Doors
   if(special >= GenDoorBase)
      return GenTypeDoor;

   // Locked Doors
   if(special >= GenLockedBase)
      return GenTypeLocked;

   // Lifts
   if(special >= GenLiftBase)
      return GenTypeLift;

   // Stairs
   if(special >= GenStairsBase)
      return GenTypeStairs;

   // Crushers
   if(special >= GenCrusherBase)
      return GenTypeCrusher;

   // not a generalized line.
   return -1;
}

//
// EV_GenActivationType
//
// Extract the activation type from a generalized line special.
//
int EV_GenActivationType(int special)
{
   return (special & TriggerType) >> TriggerTypeShift;
}

//
// EV_ActionForSpecial
//
ev_action_t *EV_ActionForSpecial(int special)
{
   switch(LevelInfo.mapFormat)
   {
   case LEVEL_FORMAT_UDMF_ETERNITY:
      return EV_UDMFEternityActionForSpecial(special);
   case LEVEL_FORMAT_HEXEN:
      return EV_HexenActionForSpecial(special);
   case LEVEL_FORMAT_PSX:
      return EV_PSXActionForSpecial(special);
   default:
      switch(LevelInfo.levelType)
      {
      case LI_TYPE_DOOM:
      default:
         return EV_DOOMActionForSpecial(special);
      case LI_TYPE_HERETIC:
      case LI_TYPE_HEXEN:
         return EV_HereticActionForSpecial(special);
      case LI_TYPE_STRIFE:
         return EV_StrifeActionForSpecial(special);
      }
   }
}

//
// EV_ActionForInstance
//
// Given an instance, obtain the corresponding ev_action_t structure,
// within the currently defined set of bindings.
//
static ev_action_t *EV_ActionForInstance(ev_instance_t &instance)
{
   // check if it is a generalized type 
   instance.gentype = EV_GenTypeForSpecial(instance.special);

   if(instance.gentype >= GenTypeFloor)
   {
      // This is a BOOM generalized special type

      // set trigger type
      instance.genspac = EV_GenActivationType(instance.special);

      return &BoomGenAction;
   }

   return EV_ActionForSpecial(instance.special);
}

//
// Lookup the number to use in the local map's bindings for a special number
// provided in ACS.
//
int EV_ActionForACSAction(int acsActionNum)
{
   switch(LevelInfo.mapFormat)
   {
   case LEVEL_FORMAT_UDMF_ETERNITY:
   case LEVEL_FORMAT_HEXEN:
      // The numbers already match in these formats.
      return acsActionNum;
   default:
      // Find the UDMF binding for this action number, and then return its
      // ExtraData equivalent if it has one. Otherwise, zero is returned.
      {
         // initialize the cross-table pointers
         EV_InitUDMFToExtraDataLookup();

         ev_binding_t *pBind;
         if((pBind = EV_UDMFEternityBindingForSpecial(acsActionNum)))
         {
            if(pBind->pEDBinding)
               return pBind->pEDBinding->actionNumber;
            else
               return 0;
         }
      }
      break;
   }

   return 0;
}

//=============================================================================
//
// Lockdef ID Lookups
//

static int EV_DOOMLockDefIDForSpecial(int special)
{
   for(size_t i = 0; i < DOOMLockDefsLen; i++)
   {
      if(DOOMLockDefs[i].special == special)
         return DOOMLockDefs[i].lockID; // got one.
   }
   
   return 0; // nothing was found
}

static int EV_HereticLockDefIDForSpecial(int special)
{
   for(size_t i = 0; i < HereticLockDefsLen; i++)
   {
      if(HereticLockDefs[i].special == special)
         return HereticLockDefs[i].lockID; // got one.
   }

   // if nothing was found there, try the DOOM lookup
   return EV_DOOMLockDefIDForSpecial(special);
}

//
// EV_LockDefIDForSpecial
//
// Test lockdef ID bindings for the current gamemode. Returns zero
// when there's no lockdef ID binding for that special.
//
int EV_LockDefIDForSpecial(int special)
{
   if(special >= GenLockedBase && special < GenDoorBase) 
   {
      return EV_lockdefIDForGenSpec(special); // generalized lock
   }
   else if(LevelInfo.mapFormat == LEVEL_FORMAT_HEXEN ||
           LevelInfo.mapFormat == LEVEL_FORMAT_UDMF_ETERNITY)
   {
      return 0; // Hexen doesn't work this way.
   }
   else
   {
      switch(LevelInfo.levelType)
      {
      case LI_TYPE_DOOM:
      default:
         return EV_DOOMLockDefIDForSpecial(special);
      case LI_TYPE_HERETIC:
      case LI_TYPE_HEXEN:
         return EV_HereticLockDefIDForSpecial(special);
      case LI_TYPE_STRIFE:
         return 0; // STRIFE_TODO
      }
   }
}

//
// Test lockdef ID bindings for the current gamemode based on a line. 
// Returns zero when there's no lockdef ID binding for that special.
//
int EV_LockDefIDForLine(const line_t *line)
{
   ev_action_t *action = EV_ActionForSpecial(line->special);

   // handle parameterized functions which accept a lockdef ID argument
   if(EV_CompositeActionFlags(action) & EV_PARAMLOCKID)
      return line->args[action->lockarg];

   // otherwise, perform normal processing
   return EV_LockDefIDForSpecial(line->special);
}

//=============================================================================
//
// Activation
//

//
// EV_checkSpac
//
// Checks against the activation characteristics of the action to see if this
// method of activating the line is allowed.
//
static bool EV_checkSpac(ev_action_t *action, ev_instance_t *instance)
{
   if(action->type->activation >= 0) // specific type for this action?
   {
      return action->type->activation == instance->spac;
   }
   else if(instance->gentype >= GenTypeFloor) // generalized line?
   {
      switch(instance->genspac)
      {
      case WalkOnce:
      case WalkMany:
         return instance->spac == SPAC_CROSS;
      case GunOnce:
      case GunMany:
         return instance->spac == SPAC_IMPACT;
      case SwitchOnce:
      case SwitchMany:
      case PushOnce:
      case PushMany:
         return instance->spac == SPAC_USE;
      default:
         return false; // should be unreachable.
      }
   }
   else // activation ability is determined by the linedef's flags
   {
      Mobj        *thing = instance->actor;
      line_t      *line  = instance->line;
      unsigned int flags = 0;

      REQUIRE_LINE(line);

      if(instance->poly)
      {
         // check 1S only flag -- if set, must be activated from first side
         if((line->extflags & EX_ML_1SONLY) && instance->side != 0)
            return false;
         flags = EX_ML_POLYOBJECT | EX_ML_CROSS;
         return instance->spac == SPAC_CROSS &&
            (line->extflags & flags) == flags;
      }
      REQUIRE_ACTOR(thing);

      // check player / monster / missile / push enable flags
      if(thing->player)                    // treat as player?
         flags |= EX_ML_PLAYER;
      if(thing->flags3 & MF3_SPACMISSILE)  // treat as missile?
         flags |= EX_ML_MISSILE;
      if(thing->flags3 & MF3_SPACMONSTER)  // treat as monster?
      {
         // in vanilla Hexen, monsters can only cross lines.
         if(P_LevelIsVanillaHexen() && instance->spac != SPAC_CROSS)
            return false;

         // secret lines can't be activated by monsters
         if(line->flags & ML_SECRET)
            return false;

         flags |= EX_ML_MONSTER;
      }
      if(thing->flags4 & MF4_SPACPUSHWALL) // treat as a wall pusher?
      {
         // in vanilla Hexen, players or missiles can push walls;
         // otherwise, we allow any object so tagged to do the activation.
         if(instance->spac == SPAC_PUSH &&
            (!P_LevelIsVanillaHexen() || (flags & (EX_ML_PLAYER | EX_ML_MISSILE))))
         {
            flags = EX_ML_PUSH;
         }
      }

      if(!(line->extflags & flags))
         return false;

      // check 1S only flag -- if set, must be activated from first side
      if((line->extflags & EX_ML_1SONLY) && instance->side != 0)
         return false;

      // check activation flags -- can we activate this line this way?
      switch(instance->spac)
      {
      case SPAC_CROSS:
         flags = EX_ML_CROSS;
         break;
      case SPAC_USE:
         flags = EX_ML_USE;
         break;
      case SPAC_IMPACT:
         flags = EX_ML_IMPACT;
         break;
      case SPAC_PUSH:
         flags = EX_ML_PUSH;
         break;
      }

      return (line->extflags & flags) != 0;
   }
}

//
// EV_ActivateSpecial
//
// Shared logic for all types of line activation
//
static int EV_ActivateSpecial(ev_action_t *action, ev_instance_t *instance)
{
   // demo version check
   if(action->minversion > demo_version)
      return false;

   // execute pre-amble routine
   if(!action->type->pre(action, instance))
      return false;

   // execute the action
   int result = action->action(action, instance);

   // execute the post-action routine
   return action->type->post(action, result, instance);
}

//
// EV_ActivateSpecialLineWithSpac
//
// Populates the ev_instance_t from separate arguments and then activates the
// special.
//
bool EV_ActivateSpecialLineWithSpac(line_t *line, int side, Mobj *thing,
                                    polyobj_t *poly, int spac)
{
   ev_action_t *action;
   INIT_STRUCT(ev_instance_t, instance);

   // setup instance
   instance.actor   = thing;
   instance.args    = line->args;
   instance.line    = line;
   instance.poly    = poly;
   instance.special = line->special;
   instance.side    = side;
   instance.spac    = spac;
   instance.tag     = line->args[0];

   // get action
   if(!(action = EV_ActionForInstance(instance)))
      return false;

   // check for parameterized special behavior with tags
   if(EV_CompositeActionFlags(action) & EV_PARAMLINESPEC)
      instance.tag = instance.args[0];

   // check for special instance
   if(!EV_checkSpac(action, &instance))
      return false;

   // IOANCH 20140810: bot awareness
   if (botMap && thing->player)
   {
      for(int i = 0; i < MAXPLAYERS; ++i)
         if(playeringame[i])
            bots[i].addXYEvent(BOT_WALKTRIG, v2fixed_t(*line->v1));
   }

   return !!EV_ActivateSpecial(action, &instance);
}

//
// EV_ActivateSpecialNum
//
// Activate a special without a source linedef. Only some specials support
// this; ones which don't will return false in their preamble.
//
bool EV_ActivateSpecialNum(int special, int *args, Mobj *thing)
{
   ev_action_t *action;
   INIT_STRUCT(ev_instance_t, instance);

   // setup instance
   instance.actor   = thing;
   instance.args    = args;
   instance.special = special;
   instance.side    = 0;
   instance.spac    = SPAC_CROSS;
   instance.tag     = args[0];

   // get action
   if(!(action = EV_ActionForInstance(instance)))
      return false;

   return !!EV_ActivateSpecial(action, &instance);
}

//
// EV_ActivateACSSpecial
//
// Activate a special for ACS.
//
int EV_ActivateACSSpecial(line_t *line, int special, int *args, int side, Mobj *thing,
                          polyobj_t *poly)
{
   ev_action_t *action;
   INIT_STRUCT(ev_instance_t, instance);

   // setup instance
   instance.actor   = thing;
   instance.args    = args;
   instance.line    = line;
   instance.poly    = poly;
   instance.special = special;
   instance.side    = side;
   instance.spac    = SPAC_CROSS;
   instance.tag     = args[0];

   // get action (always from within the Hexen namespace)
   if(!(action = EV_ACSActionForSpecial(special)))
      return false;

   return EV_ActivateSpecial(action, &instance);
}

//
// EV_ActivateAction
//
// Activate an action that has been looked up elsewhere.
//
bool EV_ActivateAction(ev_action_t *action, int *args, Mobj *thing)
{
   if(!action)
      return false;
   
   INIT_STRUCT(ev_instance_t, instance);

   // setup instance
   instance.actor = thing;
   instance.args  = args;
   instance.side  = 0;
   instance.spac  = SPAC_CROSS;
   instance.tag   = args[0];

   return !!EV_ActivateSpecial(action, &instance);
}

//
// EV_IsParamLineSpec
//
// Query the currently active binding set for whether or not a given special
// number is a parameterized line special.
//
bool EV_IsParamLineSpec(int special)
{
   ev_action_t *action = EV_ActionForSpecial(special);
   return ((EV_CompositeActionFlags(action) & EV_PARAMLINESPEC) == EV_PARAMLINESPEC);
}

//
// True if it's a usable special
//
bool EV_IsSwitchSpecial(const line_t &line)
{
   const ev_action_t *action = EV_ActionForSpecial(line.special);
   if(!action)
      return false;

   // basic
   if(action->type == &S1ActionType || action->type == &SRActionType ||
      action->type == &DRActionType)
   {
      return true;
   }

   // generalized
   if(EV_GenTypeForSpecial(line.special) >= GenTypeFloor)
   {
      int genspac = EV_GenActivationType(line.special);
      return genspac == SwitchOnce || genspac == SwitchMany || genspac == PushOnce ||
      genspac == PushMany;
   }

   // parameterized
   return EV_IsParamAction(*action) && line.extflags & EX_ML_USE;
}

//
// True if it's a shootable special
//
bool EV_IsGunSpecial(const line_t &line)
{
   const ev_action_t *action = EV_ActionForSpecial(line.special);
   if(!action)
      return false;

   // basic
   if(action->type == &G1ActionType || action->type == &GRActionType)
      return true;

   // generalized
   if(EV_GenTypeForSpecial(line.special) >= GenTypeFloor)
   {
      int genspac = EV_GenActivationType(line.special);
      return genspac == GunOnce || genspac == GunMany;
   }

   // parameterized
   return EV_IsParamAction(*action) && line.extflags & EX_ML_IMPACT;
}

//
// True if it's a walkable special
//
bool EV_IsWalkSpecial(const line_t &line)
{
   const ev_action_t *action = EV_ActionForSpecial(line.special);
   if(!action)
      return false;

   // basic
   if(action->type == &W1ActionType || action->type == &WRActionType)
      return true;

   // generalized
   if(EV_GenTypeForSpecial(line.special) >= GenTypeFloor)
   {
      int genspac = EV_GenActivationType(line.special);
      return genspac == WalkOnce || genspac == WalkMany;
   }

   // parameterized
   return EV_IsParamAction(*action) && line.extflags & EX_ML_CROSS;
}

//
// True if it's not for players
//
bool EV_IsNonPlayerSpecial(const line_t &line)
{
   const ev_action_t *action = EV_ActionForSpecial(line.special);
   if(!action)
      return false;

   if(EV_CompositeActionFlags(action) & EV_PREMONSTERSONLY)
      return true;

   // FIXME: debatable if missile activation won't still count as for-players
   return EV_IsParamAction(*action) && !(line.extflags & EX_ML_PLAYER);
}

//
// True if reusable
//
bool EV_IsRepeatableSpecial(const line_t &line)
{
   const ev_action_t *action = EV_ActionForSpecial(line.special);
   if(!action)
      return false;
   if(action->type == &WRActionType || action->type == &SRActionType ||
      action->type == &GRActionType || action->type == &DRActionType)
   {
      return true;
   }
   if(EV_GenTypeForSpecial(line.special) >= GenTypeFloor)
   {
      int genspac = EV_GenActivationType(line.special);
      return genspac == WalkMany || genspac == PushMany || genspac == GunMany ||
      genspac == SwitchMany;
   }

   return EV_IsParamAction(*action) && line.extflags & EX_ML_REPEAT;
}

//
// True if it's a special which can only lower the ceiling (never helpful unless a push special
// depends on it, but DOOM natively lacks that)
//
bool EV_IsCeilingLoweringSpecial(const line_t &line)
{
   const ev_action_t *action = EV_ActionForSpecial(line.special);
   if(!action)
      return true;

   static const std::unordered_set<EVActionFunc> types = {
      EV_ActionCeilingLowerAndCrush,
      EV_ActionCeilingLowerToFloor,
      EV_ActionCloseDoor,
      EV_ActionCloseDoor30,
      EV_ActionDoorBlazeClose,

      EV_ActionParamCeilingLowerAndCrushDist,
      EV_ActionParamCeilingLowerAndCrush,
      EV_ActionParamCeilingLowerByTexture,
      EV_ActionParamCeilingLowerByValue,  // FIXME: negative values
      EV_ActionParamCeilingLowerByValueTimes8,
      EV_ActionParamCeilingLowerInstant,
      EV_ActionParamCeilingLowerToFloor,
      EV_ActionParamCeilingLowerToHighestFloor,
      EV_ActionParamCeilingLowerToLowest,
      EV_ActionParamCeilingLowerToNearest,
      EV_ActionParamCeilingRaiseToLowest,
      EV_ActionParamCeilingToFloorInstant,
      EV_ActionParamDoorClose,
      EV_ActionParamDoorCloseWaitOpen,
      EV_ActionParamDoorWaitClose,
   };
   if(types.count(action->action))
      return true;
   int gentype = EV_GenTypeForSpecial(line.special);
   auto genceilingcheck = [](int target, int up) {
      return target == CtoLnC || target == CtoF || (!up && (target == CtoNnC || target == CbyST ||
                                                            target == Cby24 || target == Cby32));
   };
   if(gentype == GenTypeDoor)
   {
      int value = line.special - GenDoorBase;
      int kind = (value & DoorKind) >> DoorKindShift;
      if(kind == CDoor || kind == CdODoor)
         return true;
   }
   else if(gentype == GenTypeCeiling)
   {
      int value = line.special - GenCeilingBase;
      int target = (value & CeilingTarget) >> CeilingTargetShift;
      int up = (value & CeilingDirection) >> CeilingDirectionShift;
      if(!genceilingcheck(target, up))
         return true;
   }
   if(action->action == EV_ActionParamCeilingGeneric)
   {
      int up = !!(line.args[4] & 8);
      int target = line.args[3];
      if(!target)
      {
         if(!up ^ line.args[2] < 0)
            return true;
      }
      target = eclamp(target - 1, (int)CtoHnC, (int)CbyST);
      if(!genceilingcheck(target, up))
         return true;
   }
   return false;
}

//
// If it's a tele special
//
TeleportSpecInfo EV_IsTeleportationSpecial(const line_t &line)
{
   const ev_action_t *action = EV_ActionForSpecial(line.special);
   if(!action)
      return { TeleportSpecInfo::none };

   const int *args = line.args;
   auto func = action->action;

   if(func == EV_ActionParamTeleport)
      return { TeleportSpecInfo::spot, args[1], args[0] };
   if(func == EV_ActionParamTeleportLine)
      return { TeleportSpecInfo::line, args[1], 0, args[2] ? TeleportSpecInfo::flip : 0 };
   if(func == EV_ActionParamTeleportNoFog)
      return { TeleportSpecInfo::spot, args[2], args[0], args[3] ? TeleportSpecInfo::keepheight : 0};
   if(func == EV_ActionSilentLineTeleport)
      return { TeleportSpecInfo::line, args[0], 0, 0 };
   if(func == EV_ActionSilentLineTeleportReverse)
      return { TeleportSpecInfo::line, args[0], 0, TeleportSpecInfo::flip };
   if(func == EV_ActionSilentTeleport || func == EV_ActionTeleport)
      return { TeleportSpecInfo::spot, args[0], 0 };

   return{ TeleportSpecInfo::none };
}

//=============================================================================
//
// Development Commands
//

CONSOLE_COMMAND(ev_mapspecials, cf_level)
{
   for(int i = 0; i < numlines; i++)
   {
      auto action = EV_ActionForSpecial(lines[i].special);
      if(action)
      {
         C_Printf("%5d: %3d %3d %s%s\n", i, lines[i].special, lines[i].tag,
            action->name, action->minversion > 0 ? "*" : "");
      }
   }
}

// EOF

