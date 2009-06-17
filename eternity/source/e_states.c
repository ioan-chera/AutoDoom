// Emacs style mode select -*- C++ -*-
//----------------------------------------------------------------------------
//
// Copyright(C) 2005 James Haley
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
//----------------------------------------------------------------------------
//
// EDF States Module
//
// By James Haley
//
//----------------------------------------------------------------------------

#include "z_zone.h"
#include "i_system.h"
#include "p_partcl.h"
#include "d_io.h"
#include "d_dehtbl.h"
#include "info.h"
#include "m_qstr.h"
#include "p_pspr.h"

#define NEED_EDF_DEFINITIONS

#include "Confuse/confuse.h"

#include "e_lib.h"
#include "e_edf.h"
#include "e_things.h"
#include "e_sound.h"
#include "e_string.h"
#include "e_states.h"

// 7/24/05: This is now global, for efficiency's sake

// The "S_NULL" state, which is required, has its number resolved
// in E_CollectStates
int NullStateNum;

// Frame section keywords
#define ITEM_FRAME_SPRITE "sprite"
#define ITEM_FRAME_SPRFRAME "spriteframe"
#define ITEM_FRAME_FULLBRT "fullbright"
#define ITEM_FRAME_TICS "tics"
#define ITEM_FRAME_ACTION "action"
#define ITEM_FRAME_NEXTFRAME "nextframe"
#define ITEM_FRAME_MISC1 "misc1"
#define ITEM_FRAME_MISC2 "misc2"
#define ITEM_FRAME_PTCLEVENT "particle_event"
#define ITEM_FRAME_ARGS "args"
#define ITEM_FRAME_DEHNUM "dehackednum"
#define ITEM_FRAME_CMP "cmp"

#define ITEM_DELTA_NAME "name"

// forward prototype for action function dispatcher
static int E_ActionFuncCB(cfg_t *cfg, cfg_opt_t *opt, int argc,
                          const char **argv);

//
// Frame Options
//

#define FRAME_FIELDS \
   CFG_STR(ITEM_FRAME_SPRITE,      "BLANK",     CFGF_NONE), \
   CFG_INT_CB(ITEM_FRAME_SPRFRAME, 0,           CFGF_NONE, E_SpriteFrameCB), \
   CFG_BOOL(ITEM_FRAME_FULLBRT,    cfg_false,   CFGF_NONE), \
   CFG_INT(ITEM_FRAME_TICS,        1,           CFGF_NONE), \
   CFG_STRFUNC(ITEM_FRAME_ACTION,  "NULL",      E_ActionFuncCB), \
   CFG_STR(ITEM_FRAME_NEXTFRAME,   "S_NULL",    CFGF_NONE), \
   CFG_STR(ITEM_FRAME_MISC1,       "0",         CFGF_NONE), \
   CFG_STR(ITEM_FRAME_MISC2,       "0",         CFGF_NONE), \
   CFG_STR(ITEM_FRAME_PTCLEVENT,   "pevt_none", CFGF_NONE), \
   CFG_STR(ITEM_FRAME_ARGS,        0,           CFGF_LIST), \
   CFG_INT(ITEM_FRAME_DEHNUM,      -1,          CFGF_NONE), \
   CFG_END()

cfg_opt_t edf_frame_opts[] =
{
   CFG_STR(ITEM_FRAME_CMP, 0, CFGF_NONE),
   FRAME_FIELDS
};

cfg_opt_t edf_fdelta_opts[] =
{
   CFG_STR(ITEM_DELTA_NAME, 0, CFGF_NONE),
   FRAME_FIELDS
};

//
// State Hash Lookup Functions
//

// State hash tables
// Note: Keep some buffer space between this prime number and the
// number of default states defined, so that users can add a number 
// of types without causing significant hash collisions. I do not 
// recommend changing the hash table to be dynamically allocated.

// State Hashing
#define NUMSTATECHAINS 2003
static int state_namechains[NUMSTATECHAINS];
static int state_dehchains[NUMSTATECHAINS];

//
// E_StateNumForDEHNum
//
// State DeHackEd numbers *were* simply the actual, internal state
// number, but we have to actually store and hash them for EDF to
// remain cross-version compatible. If a state with the requested
// dehnum isn't found, NUMSTATES is returned.
//
int E_StateNumForDEHNum(int dehnum)
{
   int statenum;
   int statekey = dehnum % NUMSTATECHAINS;

   // 08/31/03: return null state for negative numbers, to
   // please some old, incorrect DeHackEd patches
   if(dehnum < 0)
      return NullStateNum;

   statenum = state_dehchains[statekey];
   while(statenum != NUMSTATES && states[statenum].dehnum != dehnum)
      statenum = states[statenum].dehnext;

   return statenum;
}

//
// E_GetStateNumForDEHNum
//
// As above, but causes a fatal error if the state isn't found,
// rather than returning NUMSTATES. This keeps error checking code
// from being necessitated all over the source code.
//
int E_GetStateNumForDEHNum(int dehnum)
{
   int statenum = E_StateNumForDEHNum(dehnum);

   if(statenum == NUMSTATES)
      I_Error("E_GetStateNumForDEHNum: invalid deh num %d\n", dehnum);

   return statenum;
}

//
// E_SafeState
//
// As above, but returns index of S_NULL state if the requested
// one was not found.
//
int E_SafeState(int dehnum)
{
   int statenum = E_StateNumForDEHNum(dehnum);

   if(statenum == NUMSTATES)
      statenum = NullStateNum;

   return statenum;
}

//
// E_StateNumForName
//
// Returns the number of a state given its name. Returns NUMSTATES
// if the state is not found.
//
int E_StateNumForName(const char *name)
{
   int statenum;
   unsigned int statekey = D_HashTableKey(name) % NUMSTATECHAINS;
   
   statenum = state_namechains[statekey];
   while(statenum != NUMSTATES && strcasecmp(name, states[statenum].name))
      statenum = states[statenum].namenext;
   
   return statenum;
}

//
// E_GetStateNumForName
//
// As above, but causes a fatal error if the state doesn't exist.
//
int E_GetStateNumForName(const char *name)
{
   int statenum = E_StateNumForName(name);

   if(statenum == NUMSTATES)
      I_Error("E_GetStateNumForName: bad frame %s\n", name);

   return statenum;
}

// haleyjd 03/22/06: automatic dehnum allocation
//
// Automatic allocation of dehacked numbers allows states to be used with
// parameterized codepointers without having had a DeHackEd number explicitly
// assigned to them by the EDF author. This was requested by several users
// after v3.33.02.
//

// allocation starts at D_MAXINT and works toward 0
static int edf_alloc_state_dehnum = D_MAXINT;

boolean E_AutoAllocStateDEHNum(int statenum)
{
   unsigned int key;
   int dehnum;
   state_t *st = &states[statenum];

#ifdef RANGECHECK
   if(st->dehnum != -1)
      I_Error("E_AutoAllocStateDEHNum: called for state with valid dehnum\n");
#endif

   // cannot assign because we're out of dehnums?
   if(edf_alloc_state_dehnum < 0)
      return false;

   do
   {
      dehnum = edf_alloc_state_dehnum--;
   } 
   while(dehnum >= 0 && E_StateNumForDEHNum(dehnum) != NUMSTATES);

   // ran out while looking for an unused number?
   if(dehnum < 0)
      return false;

   // assign it!
   st->dehnum = dehnum;
   key = dehnum % NUMSTATECHAINS;
   st->dehnext = state_dehchains[key];
   state_dehchains[key] = statenum;

   return true;
}

//
// EDF Processing Routines
//

//
// E_CollectStates
//
// Pre-creates and hashes by name the states, for purpose of mutual 
// and forward references.
//
void E_CollectStates(cfg_t *scfg)
{
   int i;

   // allocate array
   states = Z_Malloc(sizeof(state_t)*NUMSTATES, PU_STATIC, NULL);

   // initialize hash slots
   for(i = 0; i < NUMSTATECHAINS; ++i)
      state_namechains[i] = state_dehchains[i] = NUMSTATES;

   // build hash table
   E_EDFLogPuts("\t* Building state hash tables\n");

   for(i = 0; i < NUMSTATES; ++i)
   {
      unsigned int key;
      cfg_t *statecfg = cfg_getnsec(scfg, EDF_SEC_FRAME, i);
      const char *name = cfg_title(statecfg);
      int tempint;

      // verify length
      if(strlen(name) > 40)
      {
         E_EDFLoggedErr(2, 
            "E_CollectStates: invalid frame mnemonic '%s'\n", name);
      }

      // copy it to the state
      memset(states[i].name, 0, 41);
      strncpy(states[i].name, name, 41);

      // hash it
      key = D_HashTableKey(name) % NUMSTATECHAINS;
      states[i].namenext = state_namechains[key];
      state_namechains[key] = i;

      // process dehackednum and add state to dehacked hash table,
      // if appropriate
      tempint = cfg_getint(statecfg, ITEM_FRAME_DEHNUM);
      states[i].dehnum = tempint;
      if(tempint >= 0)
      {
         int dehkey = tempint % NUMSTATECHAINS;
         int cnum;
         
         // make sure it doesn't exist yet
         if((cnum = E_StateNumForDEHNum(tempint)) != NUMSTATES)
         {
            E_EDFLoggedErr(2, 
               "E_CollectStates: frame '%s' reuses dehackednum %d\n"
               "                 Conflicting frame: '%s'\n",
               states[i].name, tempint, states[cnum].name);
         }
         
         states[i].dehnext = state_dehchains[dehkey];
         state_dehchains[dehkey] = i;
      }

      // haleyjd 06/15/09: set state index
      states[i].index = i;
   }

   // verify the existence of the S_NULL frame
   NullStateNum = E_StateNumForName("S_NULL");
   if(NullStateNum == NUMSTATES)
      E_EDFLoggedErr(2, "E_CollectStates: 'S_NULL' frame must be defined!\n");
}

// frame field parsing routines

//
// E_StateSprite
//
// Isolated code to process the frame sprite field.
//
static void E_StateSprite(const char *tempstr, int i)
{
   // check for special 'BLANK' identifier
   if(!strcasecmp(tempstr, "BLANK"))
      states[i].sprite = blankSpriteNum;
   else
   {
      // resolve normal sprite name
      int sprnum = E_SpriteNumForName(tempstr);
      if(sprnum == NUMSPRITES)
      {
         // haleyjd 05/31/06: downgraded to warning
         E_EDFLogPrintf("\t\tWarning: frame '%s': invalid sprite '%s'\n",
                        states[i].name, tempstr);
         sprnum = blankSpriteNum;
      }
      states[i].sprite = sprnum;
   }
}

//
// E_ActionFuncCB
//
// haleyjd 04/03/08: Callback function for the new function-valued string
// option used to specify state action functions. This is called during 
// parsing, not processing, and thus we do not look up/resolve anything
// at this point. We are only interested in populating the cfg's args
// values with the strings passed to this callback as parameters. The value
// of the option has already been set to the name of the codepointer by
// the libConfuse framework.
//
static int E_ActionFuncCB(cfg_t *cfg, cfg_opt_t *opt, int argc, 
                          const char **argv)
{
   if(argc > 0)
      cfg_setlistptr(cfg, "args", argc, (void *)argv);

   return 0; // everything is good
}

//
// E_StateAction
//
// Isolated code to process the frame action field.
//
static void E_StateAction(const char *tempstr, int i)
{
   deh_bexptr *dp = D_GetBexPtr(tempstr);
   
   if(!dp)
   {
      E_EDFLoggedErr(2, "E_ProcessState: frame '%s': bad action '%s'\n",
                     states[i].name, tempstr);
   }

   states[i].action = dp->cptr;
}

enum
{
   PREFIX_FRAME,
   PREFIX_THING,
   PREFIX_SOUND,
   PREFIX_FLAGS,
   PREFIX_FLAGS2,
   PREFIX_FLAGS3,
   PREFIX_BEXPTR,
   PREFIX_STRING,
   NUM_MISC_PREFIXES
};

static const char *misc_prefixes[NUM_MISC_PREFIXES] =
{
   "frame",  "thing",  "sound", "flags", "flags2", "flags3",
   "bexptr", "string"
};

static void E_AssignMiscThing(long *target, int thingnum)
{
   // 09/19/03: add check for no dehacked number
   // 03/22/06: auto-allocate dehacked numbers where possible
   if(mobjinfo[thingnum].dehnum >= 0 || E_AutoAllocThingDEHNum(thingnum))
      *target = mobjinfo[thingnum].dehnum;
   else
   {
      E_EDFLogPrintf("\t\tWarning: failed to auto-allocate DeHackEd number "
                     "for thing %s\n", mobjinfo[thingnum].name);
      *target = UnknownThingType;
   }
}

static void E_AssignMiscState(long *target, int framenum)
{
   // 09/19/03: add check for no dehacked number
   // 03/22/06: auto-allocate dehacked numbers where possible
   if(states[framenum].dehnum >= 0 || E_AutoAllocStateDEHNum(framenum))
      *target = states[framenum].dehnum;
   else
   {
      E_EDFLogPrintf("\t\tWarning: failed to auto-allocate DeHackEd number "
                     "for frame %s\n", states[framenum].name);
      *target = NullStateNum;
   }
}

static void E_AssignMiscSound(long *target, sfxinfo_t *sfx)
{
   // 01/04/09: check for NULL just in case
   if(!sfx)
      sfx = &NullSound;

   // 03/22/06: auto-allocate dehacked numbers where possible
   if(sfx->dehackednum >= 0 || E_AutoAllocSoundDEHNum(sfx))
      *target = sfx->dehackednum;
   else
   {
      E_EDFLogPrintf("\t\tWarning: failed to auto-allocate DeHackEd number "
                     "for sound %s\n", sfx->mnemonic);
      *target = 0;
   }
}

static void E_AssignMiscString(long *target, edf_string_t *str, const char *name)
{
   if(!str || str->numkey < 0)
   {
      E_EDFLogPrintf("\t\tWarning: bad string %s\n", name);
      *target = 0;
   }
   else
      *target = str->numkey;
}

static void E_AssignMiscBexptr(long *target, deh_bexptr *dp, const char *name)
{
   if(!dp)
      E_EDFLoggedErr(2, "E_ParseMiscField: bad bexptr '%s'\n", name);
   
   // get the index of this deh_bexptr in the master
   // deh_bexptrs array, and store it in the arg field
   *target = dp - deh_bexptrs;
}

//
// E_ParseMiscField
//
// This function implements the quite powerful prefix:value syntax
// for misc and args fields in frames. Some of the code within may
// be candidate for generalization, since other fields may need
// this syntax in the near future.
//
static void E_ParseMiscField(char *value, long *target)
{
   int i;
   char prefix[16];
   char *colonloc, *strval;
   
   memset(prefix, 0, 16);

   // look for a colon ending a possible prefix
   colonloc = E_ExtractPrefix(value, prefix, 16);
   
   if(colonloc)
   {
      // a colon was found, so identify the prefix
      strval = colonloc + 1;

      i = E_StrToNumLinear(misc_prefixes, NUM_MISC_PREFIXES, prefix);

      switch(i)
      {
      case PREFIX_FRAME:
         {
            int framenum = E_StateNumForName(strval);
            if(framenum == NUMSTATES)
            {
               E_EDFLogPrintf("\t\tWarning: invalid state '%s' in misc field\n",
                              strval);
               *target = NullStateNum;
            }
            else
               E_AssignMiscState(target, framenum);
         }
         break;
      case PREFIX_THING:
         {
            int thingnum = E_ThingNumForName(strval);
            if(thingnum == NUMMOBJTYPES)
            {
               E_EDFLogPrintf("\t\tWarning: invalid thing '%s' in misc field\n",
                              strval);
               *target = UnknownThingType;
            }
            else
               E_AssignMiscThing(target, thingnum);
         }
         break;
      case PREFIX_SOUND:
         {
            sfxinfo_t *sfx = E_EDFSoundForName(strval);
            if(!sfx)
            {
               // haleyjd 05/31/06: relaxed to warning
               E_EDFLogPrintf("\t\tWarning: invalid sound '%s' in misc field\n", 
                              strval);
               sfx = &NullSound;
            }
            E_AssignMiscSound(target, sfx);
         }
         break;
      case PREFIX_FLAGS:
         *target = deh_ParseFlagsSingle(strval, DEHFLAGS_MODE1);
         break;
      case PREFIX_FLAGS2:
         *target = deh_ParseFlagsSingle(strval, DEHFLAGS_MODE2);
         break;
      case PREFIX_FLAGS3:
         *target = deh_ParseFlagsSingle(strval, DEHFLAGS_MODE3);
         break;
      case PREFIX_BEXPTR:
         {
            deh_bexptr *dp = D_GetBexPtr(strval);
            E_AssignMiscBexptr(target, dp, strval);
         }
         break;
      case PREFIX_STRING:
         {
            edf_string_t *str = E_StringForName(strval);
            E_AssignMiscString(target, str, strval);
         }
         break;
      default:
         E_EDFLogPrintf("\t\tWarning: unknown value prefix '%s'\n",
                        prefix);
         *target = 0;
         break;
      }
   }
   else
   {
      char *endptr;
      long val;
      double dval;

      // see if it is a number
      if(strchr(value, '.')) // has a decimal point?
      {
         dval = strtod(value, &endptr);

         // convert result to fixed-point
         val = (fixed_t)(dval * FRACUNIT);
      }
      else
      {
         // 11/11/03: use strtol to support hex and oct input
         val = strtol(value, &endptr, 0);
      }

      // haleyjd 04/02/08:
      // no? then try certain namespaces in a predefined order of precedence
      if(*endptr != '\0')
      {
         int temp;
         sfxinfo_t *sfx;
         edf_string_t *str;
         deh_bexptr *dp;
         
         if((temp = E_ThingNumForName(value)) != NUMMOBJTYPES)   // thingtype?
            E_AssignMiscThing(target, temp);
         else if((temp = E_StateNumForName(value)) != NUMSTATES) // frame?
            E_AssignMiscState(target, temp);
         else if((sfx = E_EDFSoundForName(value)) != NULL)       // sound?
            E_AssignMiscSound(target, sfx);
         else if((str = E_StringForName(value)) != NULL)         // string?
            E_AssignMiscString(target, str, value);
         else if((dp = D_GetBexPtr(value)) != NULL)              // bexptr???
            E_AssignMiscBexptr(target, dp, value);
         else                                                    // try a keyword!
            *target = E_ValueForKeyword(value);
      }
      else
         *target = val;
   }
}

enum
{
   NSPEC_NEXT,
   NSPEC_PREV,
   NSPEC_THIS,
   NSPEC_NULL,
   NUM_NSPEC_KEYWDS
};

static const char *nspec_keywds[NUM_NSPEC_KEYWDS] =
{
   "next", "prev", "this", "null"
};

//
// E_SpecialNextState
//
// 11/07/03:
// Returns a frame number for a special nextframe value.
//
static int E_SpecialNextState(const char *string, int framenum)
{
   int i, nextnum = 0;
   const char *value = string + 1;

   i = E_StrToNumLinear(nspec_keywds, NUM_NSPEC_KEYWDS, value);

   switch(i)
   {
   case NSPEC_NEXT:
      if(framenum == NUMSTATES - 1) // can't do it
      {
         E_EDFLoggedErr(2, "E_SpecialNextState: invalid frame #%d\n",
                        NUMSTATES);
      }
      nextnum = framenum + 1;
      break;
   case NSPEC_PREV:
      if(framenum == 0) // can't do it
         E_EDFLoggedErr(2, "E_SpecialNextState: invalid frame -1\n");
      nextnum = framenum - 1;
      break;
   case NSPEC_THIS:
      nextnum = framenum;
      break;
   case NSPEC_NULL:
      nextnum = NullStateNum;
      break;
   default: // ???
      E_EDFLoggedErr(2, "E_SpecialNextState: invalid specifier '%s'\n",
                     value);
   }

   return nextnum;
}

//
// E_StateNextFrame
//
// Isolated code to process the frame nextframe field.
//
static void E_StateNextFrame(const char *tempstr, int i)
{
   int tempint = 0;

   // 11/07/03: allow special values in the nextframe field
   if(tempstr[0] == '@')
   {
      tempint = E_SpecialNextState(tempstr, i);
   }
   else if((tempint = E_StateNumForName(tempstr)) == NUMSTATES)
   {
      char *endptr = NULL;
      long result;

      result = strtol(tempstr, &endptr, 0);
      if(*endptr == '\0')
      {
         // check for DeHackEd num specification;
         // the resulting value must be a valid frame deh number
         tempint = E_GetStateNumForDEHNum(result);
      }
      else      
      {
         // error
         E_EDFLoggedErr(2, 
            "E_ProcessState: frame '%s': bad nextframe '%s'\n",
            states[i].name, tempstr);

      }
   }

   states[i].nextstate = tempint;
}

//
// E_StatePtclEvt
//
// Isolated code to process the frame particle event field.
//
static void E_StatePtclEvt(const char *tempstr, int i)
{
   int tempint = 0;

   while(tempint != P_EVENT_NUMEVENTS &&
         strcasecmp(tempstr, particleEvents[tempint].name))
   {
      ++tempint;
   }
   if(tempint == P_EVENT_NUMEVENTS)
   {
      E_EDFLoggedErr(2, 
         "E_ProcessState: frame '%s': bad ptclevent '%s'\n",
         states[i].name, tempstr);
   }

   states[i].particle_evt = tempint;
}

// haleyjd 04/03/08: hack for function-style arguments to action function
static boolean in_action;
static boolean early_args_found;
static boolean early_args_end;


//
// E_CmpTokenizer
//
// haleyjd 06/24/04:
// A lexer function for the frame cmp field.
// Used by E_ProcessCmpState below.
//
static char *E_CmpTokenizer(const char *text, int *index, qstring_t *token)
{
   char c;
   int state = 0;

   // if we're already at the end, return NULL
   if(text[*index] == '\0')
      return NULL;

   M_QStrClear(token);

   while((c = text[*index]) != '\0')
   {
      *index += 1;
      switch(state)
      {
      case 0: // default state
         switch(c)
         {
         case ' ':
         case '\t':
            continue;  // skip whitespace
         case '"':
            state = 1; // enter quoted part
            continue;
         case '\'':
            state = 2; // enter quoted part (single quote support)
            continue;
         case '|':     // end of current token
         case ',':     // 03/01/05: added by user request
            return M_QStrBuffer(token);
         case '(':
            if(in_action)
            {
               early_args_found = true;
               return M_QStrBuffer(token);
            }
            M_QStrPutc(token, c);
            continue;
         case ')':
            if(in_action && early_args_found)
            {
               early_args_end = true;
               continue;
            }
            // fall through
         default:      // everything else == part of value
            M_QStrPutc(token, c);
            continue;
         }
      case 1: // in quoted area (double quotes)
         if(c == '"') // end of quoted area
            state = 0;
         else
            M_QStrPutc(token, c); // everything inside is literal
         continue;
      case 2: // in quoted area (single quotes)
         if(c == '\'') // end of quoted area
            state = 0;
         else
            M_QStrPutc(token, c); // everything inside is literal
         continue;
      default:
         E_EDFLoggedErr(0, "E_CmpTokenizer: internal error - undefined lexer state\n");
      }
   }

   // return final token, next call will return NULL
   return M_QStrBuffer(token);
}

// macros for E_ProcessCmpState:

// NEXTTOKEN: calls E_CmpTokenizer to get the next token

#define NEXTTOKEN() curtoken = E_CmpTokenizer(value, &tok_index, &buffer)

// DEFAULTS: tests if the string value is either NULL or equal to "*"

#define DEFAULTS(value)  (!(value) || (value)[0] == '*')

//
// E_ProcessCmpState
//
// Code to process a compressed state definition. Compressed state
// definitions are just a string with each frame field in a set order,
// delimited by pipes. This is very similar to DDF's frame specification,
// and has been requested by multiple users.
//
// Compressed format:
// "sprite|spriteframe|fullbright|tics|action|nextframe|ptcl|misc|args"
//
// haleyjd 04/03/08: An alternate syntax is also now supported:
// "sprite|spriteframe|fullbright|tics|action(args,...)|nextframe|ptcl|misc"
//
// This new syntax for argument specification is preferred, and the old
// one is now deprecated.
//
// Fields at the end can be left off. "*" in a field means to use
// the normal default value.
//
// haleyjd 06/24/04: rewritten to use a finite-state-automaton lexer, 
// making the format MUCH more flexible than it was under the former 
// strtok system. The E_CmpTokenizer function above performs the 
// lexing, returning each token in the qstring provided to it.
//
static void E_ProcessCmpState(const char *value, int i)
{
   qstring_t buffer;
   char *curtoken = NULL;
   int tok_index = 0, j;

   // first things first, we have to initialize the qstring
   M_QStrInitCreate(&buffer);

   // initialize tokenizer variables
   in_action = false;
   early_args_found = false;
   early_args_end = false;

   // process sprite
   NEXTTOKEN();
   if(DEFAULTS(curtoken))
      states[i].sprite = blankSpriteNum;
   else
      E_StateSprite(curtoken, i);

   // process spriteframe
   NEXTTOKEN();
   if(DEFAULTS(curtoken))
      states[i].frame = 0;
   else
   {
      // call the value-parsing callback explicitly
      if(E_SpriteFrameCB(NULL, NULL, curtoken, &(states[i].frame)) == -1)
      {
         E_EDFLoggedErr(2, 
            "E_ProcessCmpState: frame '%s': bad spriteframe '%s'\n",
            states[i].name, curtoken);
      }

      // haleyjd 09/22/07: if blank sprite, force to frame 0
      if(states[i].sprite == blankSpriteNum)
         states[i].frame = 0;
   }

   // process fullbright
   NEXTTOKEN();
   if(DEFAULTS(curtoken) == 0)
   {
      if(curtoken[0] == 't' || curtoken[0] == 'T')
         states[i].frame |= FF_FULLBRIGHT;
   }

   // process tics
   NEXTTOKEN();
   if(DEFAULTS(curtoken))
      states[i].tics = 1;
   else
      states[i].tics = strtol(curtoken, NULL, 0);

   // process action
   in_action = true;
   NEXTTOKEN();
   if(DEFAULTS(curtoken))
      states[i].action = NULL;
   else
      E_StateAction(curtoken, i);

   // haleyjd 04/03/08: check for early args found by tokenizer
   if(early_args_found)
   {
      int argcount = 0;

      for(j = 0; j < NUMSTATEARGS; ++j)
         states[i].args[j] = 0;

      // process args
      while(!early_args_end)
      {
         NEXTTOKEN();
         if(argcount < NUMSTATEARGS)
         {
            if(DEFAULTS(curtoken))
               states[i].args[argcount] = 0;
            else
               E_ParseMiscField(curtoken, &(states[i].args[argcount]));
         }
         ++argcount;
      }
   }

   in_action = false;

   // process nextframe
   NEXTTOKEN();
   if(DEFAULTS(curtoken))
      states[i].nextstate = NullStateNum;
   else
      E_StateNextFrame(curtoken, i);

   // process particle event
   NEXTTOKEN();
   if(DEFAULTS(curtoken))
      states[i].particle_evt = 0;
   else
      E_StatePtclEvt(curtoken, i);

   // process misc1, misc2
   NEXTTOKEN();
   if(DEFAULTS(curtoken))
      states[i].misc1 = 0;
   else
      E_ParseMiscField(curtoken, &(states[i].misc1));

   NEXTTOKEN();
   if(DEFAULTS(curtoken))
      states[i].misc2 = 0;
   else
      E_ParseMiscField(curtoken, &(states[i].misc2));

   if(!early_args_found) // do not do if early args specified
   {
      // process args
      for(j = 0; j < NUMSTATEARGS; ++j)
      {
         NEXTTOKEN();
         if(DEFAULTS(curtoken))
            states[i].args[j] = 0;
         else
            E_ParseMiscField(curtoken, &(states[i].args[j]));
      }
   }

   early_args_found = early_args_end = false;

   // free the qstring
   M_QStrFree(&buffer);
}

#undef NEXTTOKEN
#undef DEFAULTS

// IS_SET: this macro tests whether or not a particular field should
// be set. When applying deltas, we should not retrieve defaults.

#undef  IS_SET
#define IS_SET(name) (def || cfg_size(framesec, (name)) > 0)

//
// E_ProcessState
//
// Generalized code to process the data for a single state
// structure. Doubles as code for frame and framedelta.
//
static void E_ProcessState(int i, cfg_t *framesec, boolean def)
{
   int j;
   int tempint;
   char *tempstr;

   // 11/14/03:
   // In definitions only, see if the cmp field is defined. If so,
   // we go into it with E_ProcessCmpState above, and ignore most
   // other fields in the frame block.
   if(def)
   {
      if(cfg_size(framesec, ITEM_FRAME_CMP) > 0)
      {
         tempstr = cfg_getstr(framesec, ITEM_FRAME_CMP);
         
         E_ProcessCmpState(tempstr, i);
         def = false; // process remainder as if a frame delta
         goto hitcmp;
      }
   }

   // process sprite
   if(IS_SET(ITEM_FRAME_SPRITE))
   {
      tempstr = cfg_getstr(framesec, ITEM_FRAME_SPRITE);

      E_StateSprite(tempstr, i);
   }

   // process spriteframe
   if(IS_SET(ITEM_FRAME_SPRFRAME))
      states[i].frame = cfg_getint(framesec, ITEM_FRAME_SPRFRAME);

   // haleyjd 09/22/07: if sprite == blankSpriteNum, force to frame 0
   if(states[i].sprite == blankSpriteNum)
      states[i].frame = 0;

   // check for fullbright
   if(IS_SET(ITEM_FRAME_FULLBRT))
   {
      if(cfg_getbool(framesec, ITEM_FRAME_FULLBRT) == cfg_true)
         states[i].frame |= FF_FULLBRIGHT;
   }

   // process tics
   if(IS_SET(ITEM_FRAME_TICS))
      states[i].tics = cfg_getint(framesec, ITEM_FRAME_TICS);

   // resolve codepointer
   if(IS_SET(ITEM_FRAME_ACTION))
   {
      tempstr = cfg_getstr(framesec, ITEM_FRAME_ACTION);

      E_StateAction(tempstr, i);
   }

   // process nextframe
   if(IS_SET(ITEM_FRAME_NEXTFRAME))
   {
      tempstr = cfg_getstr(framesec, ITEM_FRAME_NEXTFRAME);
      
      E_StateNextFrame(tempstr, i);
   }

   // process particle event
   if(IS_SET(ITEM_FRAME_PTCLEVENT))
   {
      tempstr = cfg_getstr(framesec, ITEM_FRAME_PTCLEVENT);

      E_StatePtclEvt(tempstr, i);
   }

   // 03/30/05: the following fields are now also allowed in cmp frames
hitcmp:
      
   // misc field parsing (complicated)

   if(IS_SET(ITEM_FRAME_MISC1))
   {
      tempstr = cfg_getstr(framesec, ITEM_FRAME_MISC1);
      E_ParseMiscField(tempstr, &(states[i].misc1));
   }

   if(IS_SET(ITEM_FRAME_MISC2))
   {
      tempstr = cfg_getstr(framesec, ITEM_FRAME_MISC2);
      E_ParseMiscField(tempstr, &(states[i].misc2));
   }

   // args field parsing (even more complicated, but similar)
   // Note: deltas can only set the entire args list at once, not
   // just parts of it.
   if(IS_SET(ITEM_FRAME_ARGS))
   {
      tempint = cfg_size(framesec, ITEM_FRAME_ARGS);
      for(j = 0; j < NUMSTATEARGS; ++j)
         states[i].args[j] = 0;
      for(j = 0; j < tempint && j < NUMSTATEARGS; ++j)
      {
         tempstr = cfg_getnstr(framesec, ITEM_FRAME_ARGS, j);
         E_ParseMiscField(tempstr, &(states[i].args[j]));
      }
   }
}

//
// E_ProcessStates
//
// Resolves and loads all information for the state_t structures.
//
void E_ProcessStates(cfg_t *cfg)
{
   int i;

   E_EDFLogPuts("\t* Processing frame data\n");

   for(i = 0; i < NUMSTATES; ++i)
   {
      cfg_t *framesec = cfg_getnsec(cfg, EDF_SEC_FRAME, i);

      E_ProcessState(i, framesec, true);

      E_EDFLogPrintf("\t\tFinished frame %s(#%d)\n", states[i].name, i);
   }
}

//
// E_ProcessStateDeltas
//
// Does processing for framedelta sections, which allow cascading
// editing of existing frames. The framedelta shares most of its
// fields and processing code with the frame section.
//
void E_ProcessStateDeltas(cfg_t *cfg)
{
   int i, numdeltas;

   E_EDFLogPuts("\t* Processing frame deltas\n");

   numdeltas = cfg_size(cfg, EDF_SEC_FRMDELTA);

   E_EDFLogPrintf("\t\t%d framedelta(s) defined\n", numdeltas);

   for(i = 0; i < numdeltas; ++i)
   {
      const char *tempstr;
      int stateNum;
      cfg_t *deltasec = cfg_getnsec(cfg, EDF_SEC_FRMDELTA, i);

      // get state to edit
      if(!cfg_size(deltasec, ITEM_DELTA_NAME))
         E_EDFLoggedErr(2, "E_ProcessFrameDeltas: framedelta requires name field\n");

      tempstr = cfg_getstr(deltasec, ITEM_DELTA_NAME);
      stateNum = E_GetStateNumForName(tempstr);

      E_ProcessState(stateNum, deltasec, false);

      E_EDFLogPrintf("\t\tApplied framedelta #%d to %s(#%d)\n",
                     i, states[stateNum].name, stateNum);
   }
}

//=============================================================================
//
// DECORATE State Parser
//
// haleyjd 06/16/09: EDF now supports DECORATE-format state definitions inside
// thingtype "states" fields, which accept heredoc strings. The processing
// of those strings is done here, where they are turned into states.
//

// parsing

// parser state enumeration
// FIXME: tenative, based on initial draw-up of FSA
enum
{
   PSTATE_EXPECTLABEL,
   PSTATE_EXPECTSTATEORKW,
   PSTATE_EXPECTSPRITE,
   PSTATE_EXPECTFRAMES,
   PSTATE_EXPECTTICS,
   PSTATE_EXPECTACTION,
   PSTATE_EXPECTPAREN,
   PSTATE_EXPECTARG,
   PSTATE_EXPECTCOMMAORPAREN,
   PSTATE_EXPECTPLUS,
   PSTATE_EXPECTOFFSET,
   PSTATE_NUMSTATES
};

// parser state structure
typedef struct pstate_s
{
   int state; // state of the parser, as defined by the above enumeration
   qstring_t *linebuffer;  // qstring to use as line buffer
   qstring_t *tokenbuffer; // qstring to use as token buffer
   int index; // current index into line buffer for tokenization
} pstate_t;

// tokenization

// token types
enum
{
   TOKEN_LABEL,   // [A-Za-z.]+':'
   TOKEN_KEYWORD, // loop, stop, wait, goto
   TOKEN_PLUS,    // '+'
   TOKEN_LPAREN,  // '('
   TOKEN_COMMA,   // ','
   TOKEN_RPAREN,  // ')'
   TOKEN_TEXT,    // anything else (numbers, strings, etc.)
   TOKEN_EOL      // end of line
};

// tokenizer states
enum
{
   TSTATE_SCAN, // scanning for start of a token
   TSTATE_DONE  // finished; return token to parser
};

// tokenizer state structure
typedef struct tkstate_s
{
   int state;        // current state of tokenizer
   int i;            // index into string
   int tokentype;    // token type, once decided upon
   int tokenerror;   // token error code
   qstring_t *line;  // line text
   qstring_t *token; // token text
} tkstate_t;

//=============================================================================
//
// Tokenizer Callbacks
//

static void DoTokenStateScan(tkstate_t *tks)
{
}

// tokenizer callback type
typedef void (*tksfunc_t)(tkstate_t *);

// tokenizer state function table
static tksfunc_t tstatefuncs[] =
{
   DoTokenStateScan, // scanning for start of a token
};

//
// E_GetDSToken
//
// Gets the next token from the DECORATE states string.
//
static int E_GetDSToken(pstate_t *ps)
{
   tkstate_t tks;

   // set up tokenizer state - transfer in parser state details
   tks.state = TSTATE_SCAN;
   tks.i     = ps->index;
   tks.line  = ps->linebuffer;
   tks.token = ps->tokenbuffer;

   M_QStrClear(tks.token);

   while(tks.state != TSTATE_DONE)
   {
#ifdef RANGECHECK
      if(tks.state < 0 || tks.state >= TSTATE_DONE)
         I_Error("E_GetDSToken: Internal error: undefined state\n");
#endif

      tstatefuncs[tks.state](&tks);

      // step to next character
      ++tks.i;
   }

   /*
   // determine keyword status for text tokens
   if(tks.tokentype == TOKEN_TEXT)
      tks.tokentype = TextToKeyword(tks.token->buffer);
   */

   // write back string index
   ps->index = tks.i;

   return tks.tokentype;
}


// EOF

