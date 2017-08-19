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
// Purpose: ANIMDEFS lump, from Hexen. Replaces ANIMATED.
// Authors: Ioan Chera
//

#include "z_zone.h"
#include "m_collection.h"
#include "w_wad.h"
#include "xl_animdefs.h"
#include "xl_scripts.h"

Collection<XLAnimDef> xldefs;
PODCollection<xlpicdef_t> xlpics;

//
// Switch definition
//
class XLSwitchDef : public ZoneObject
{
public:
   qstring name;
   qstring onname;
   qstring sound;
};

static Collection<XLSwitchDef> xlswitches;

//
// ANIMDEFS parser
//
class XLAnimDefsParser final : public XLParser
{
   static bool (XLAnimDefsParser::*States[])(XLTokenizer &);

   //
   // The states
   //
   enum
   {
      STATE_EXPECTITEM,    // before "flat", "texture" or "pic"
      STATE_EXPECTDEFNAME, // before initial pic name
      STATE_EXPECTPICNUM,  // before "pic" number
      STATE_EXPECTPICOP,   // before "pic" operator ("tics" or "rand")
      STATE_EXPECTDUR,     // expect duration after "tics" or "rand"
      STATE_EXPECTDUR2,    // second duration, if after rand.
      STATE_EXPECTRANGENAME,
      STATE_EXPECTRANGETICSOP,
      STATE_EXPECTRANGEDUR,
      STATE_EXPECTSWITCH,  // switch state. Use a separate set of states here.
   };

   enum
   {
      SWSTATE_SWITCHNAME,
      SWSTATE_SWITCHDIR,
      SWSTATE_ATTRIBNAME,
      SWSTATE_ONNAME,
      SWSTATE_TICS,  // only to be able to parse ZDoom stuff with "tics 0"
      SWSTATE_TICSVAL,
      SWSTATE_SOUNDNAME,
   };

   bool doStateExpectItem(XLTokenizer &);
   bool doStateExpectDefName(XLTokenizer &);
   bool doStateExpectPicNum(XLTokenizer &);
   bool doStateExpectPicOp(XLTokenizer &);
   bool doStateExpectDur(XLTokenizer &);
   bool doStateExpectDur2(XLTokenizer &);
   bool doStateExpectRangeName(XLTokenizer &);
   bool doStateExpectRangeTicsOp(XLTokenizer &);
   bool doStateExpectRangeDur(XLTokenizer &);
   bool doStateExpectSwitch(XLTokenizer &);

   int state;  // current state
   int swstate;   // switch state
   Collection<XLAnimDef> defs;
   PODCollection<xlpicdef_t> pics;
   Collection<XLSwitchDef> switches;

   XLAnimDef *curdef;
   xlpicdef_t *curpic;
   XLSwitchDef *curswitch;
   bool inwarp;

   bool doToken(XLTokenizer &token) override;
   void startLump() override;
   void initTokenizer(XLTokenizer &tokenizer) override;
   void onEOF(bool early) override;
public:
   XLAnimDefsParser() : XLParser("ANIMDEFS"), state(STATE_EXPECTITEM),
   swstate(), curdef(), curpic(), curswitch(), inwarp()
   {
   }
};

//
// State table
//
bool (XLAnimDefsParser::* XLAnimDefsParser::States[])(XLTokenizer &) =
{
   &XLAnimDefsParser::doStateExpectItem,
   &XLAnimDefsParser::doStateExpectDefName,
   &XLAnimDefsParser::doStateExpectPicNum,
   &XLAnimDefsParser::doStateExpectPicOp,
   &XLAnimDefsParser::doStateExpectDur,
   &XLAnimDefsParser::doStateExpectDur2,
   &XLAnimDefsParser::doStateExpectRangeName,
   &XLAnimDefsParser::doStateExpectRangeTicsOp,
   &XLAnimDefsParser::doStateExpectRangeDur,
   &XLAnimDefsParser::doStateExpectSwitch,
};

//
// Expect "flat", "texture" or "pic" (only if a flat or texture is already on)
//
bool XLAnimDefsParser::doStateExpectItem(XLTokenizer &token)
{
   const qstring &str = token.getToken();
   if(!str.strCaseCmp("flat"))
   {
      XLAnimDef def;
      def.type = xlanim_flat;
      def.index = pics.getLength();
      defs.add(def);
      curdef = &defs[defs.getLength() - 1];
      state = STATE_EXPECTDEFNAME;
      if(inwarp)
         curdef->rangetics = 65536;
      return true;
   }
   if(!str.strCaseCmp("texture"))
   {
      XLAnimDef def;
      def.type = xlanim_texture;
      def.index = pics.getLength();
      defs.add(def);
      curdef = &defs[defs.getLength() - 1];
      state = STATE_EXPECTDEFNAME;
      if(inwarp)
         curdef->rangetics = 65536;
      return true;
   }
   if(!str.strCaseCmp("pic"))
   {
      if(!curdef || inwarp)
         return false;  // must have a flat or texture animation prepared
      state = STATE_EXPECTPICNUM;
      curpic = &pics.addNew();
      ++curdef->count;
      return true;
   }
   if(!str.strCaseCmp("range"))
   {
      if(!curdef || inwarp)
         return false;
      state = STATE_EXPECTRANGENAME;
      return true;
   }
   if(!str.strCaseCmp("warp"))
   {
      if(inwarp)
         return false;
      inwarp = true;
      state = STATE_EXPECTITEM;
      return true;
   }
   if(!str.strCaseCmp("switch"))
   {
      if(inwarp)
         return false;
      state = STATE_EXPECTSWITCH;
      swstate = SWSTATE_SWITCHNAME;
      switches.add(XLSwitchDef());
      curswitch = &switches[switches.getLength() - 1];
      return true;
   }
   return false;
}

//
// Expect an animation's first pic name
//
bool XLAnimDefsParser::doStateExpectDefName(XLTokenizer &token)
{
   curdef->picname = token.getToken();
   state = STATE_EXPECTITEM;
   if(inwarp)
   {
      inwarp = false;
      curdef->rangename = curdef->picname;
   }
   return true;
}

//
// Checks if token is integer.
//
static bool XL_mustBeInt(XLTokenizer &token, int &result)
{
   char *endptr;
   long index = token.getToken().toLong(&endptr, 0);
   if(*endptr)
      return false;
   result = static_cast<int>(index);
   return true;
}

//
// Expect a pic index
//
bool XLAnimDefsParser::doStateExpectPicNum(XLTokenizer &token)
{
   int index;
   if(!XL_mustBeInt(token, index))
      return false;
   curpic->offset = static_cast<int>(index);
   state = STATE_EXPECTPICOP;
   return true;
}

//
// Expect tics or rand
//
bool XLAnimDefsParser::doStateExpectPicOp(XLTokenizer &token)
{
   const qstring &str = token.getToken();
   if(!str.strCaseCmp("tics"))
   {
      curpic->isRandom = false;
      state = STATE_EXPECTDUR;
      return true;
   }
   if(!str.strCaseCmp("rand"))
   {
      curpic->isRandom = true;
      state = STATE_EXPECTDUR;
      return true;
   }
   return false;
}

//
// Expect duration
//
bool XLAnimDefsParser::doStateExpectDur(XLTokenizer &token)
{
   int tics;
   if(!XL_mustBeInt(token, tics))
      return false;
   if(!curpic->isRandom)
   {
      curpic->tics = tics;
      state = STATE_EXPECTITEM;
      return true;
   }
   curpic->ticsmin = tics;
   state = STATE_EXPECTDUR2;
   return true;
}

//
// Expect second duration for rand
//
bool XLAnimDefsParser::doStateExpectDur2(XLTokenizer &token)
{
   int tics;
   if(!XL_mustBeInt(token, tics))
      return false;
   curpic->ticsmax = tics;
   state = STATE_EXPECTITEM;
   return true;
}

//
// Expect range name
//
bool XLAnimDefsParser::doStateExpectRangeName(XLTokenizer &token)
{
   curdef->rangename = token.getToken();
   state = STATE_EXPECTRANGETICSOP;
   return true;
}

//
// Expect "Tics"
//
bool XLAnimDefsParser::doStateExpectRangeTicsOp(XLTokenizer &token)
{
   if(token.getToken().strCaseCmp("tics"))
      return false;
   state = STATE_EXPECTRANGEDUR;
   return true;
}

//
// Expect range duration
//
bool XLAnimDefsParser::doStateExpectRangeDur(XLTokenizer &token)
{
   int tics;
   if(!XL_mustBeInt(token, tics))
      return false;
   curdef->rangetics = tics;
   state = STATE_EXPECTITEM;
   return true;
}

//
// Within a switch block
//
bool XLAnimDefsParser::doStateExpectSwitch(XLTokenizer &token)
{
   switch(swstate)
   {
      case SWSTATE_SWITCHNAME:
         curswitch->name = token.getToken();
         swstate = SWSTATE_SWITCHDIR;
         break;
      case SWSTATE_SWITCHDIR:
         // Currently "off" is reserved
         if(!token.getToken().strCaseCmp("off"))
            return false;
         swstate = SWSTATE_ATTRIBNAME;
         // Different from "on"? Jump directly to attribname.
         if(token.getToken().strCaseCmp("on"))
            return doToken(token);
         break;
      case SWSTATE_ATTRIBNAME:
         // Currently only "pic" is supported
         if(!token.getToken().strCaseCmp("pic"))
            swstate = SWSTATE_ONNAME;
         else if(!token.getToken().strCaseCmp("sound"))
            swstate = SWSTATE_SOUNDNAME;
         else
         {
            state = STATE_EXPECTITEM;
            swstate = 0;
            return doToken(token);
         }
         break;
      case SWSTATE_ONNAME:
         curswitch->onname = token.getToken();
         swstate = SWSTATE_TICS;
         break;
         // These are just for syntax validation
      case SWSTATE_TICS:
         // Currently tics are not supported are are optional.
         if(token.getToken().strCaseCmp("tics"))
         {
            // If it's not "tics", act like normal
            swstate = SWSTATE_ATTRIBNAME;
            return doToken(token);
         }
         swstate = SWSTATE_TICSVAL;
         break;
      case SWSTATE_TICSVAL:
         swstate = SWSTATE_ATTRIBNAME;
         break;
      case SWSTATE_SOUNDNAME:
         curswitch->sound = token.getToken();
         swstate = SWSTATE_ATTRIBNAME;
         break;
      default:
         return false;
   }
   return true;
}

//
// Parse next token. Goes into one of the pointed functions.
//
bool XLAnimDefsParser::doToken(XLTokenizer &token)
{
   return (this->*States[state])(token);
}

//
// Called at beginning of parsing
//
void XLAnimDefsParser::startLump()
{
   state = STATE_EXPECTITEM;
   swstate = 0;
   defs.assign(xldefs);
   pics.assign(xlpics);
   curdef = nullptr;
   curpic = nullptr;
   curswitch = nullptr;
   inwarp = false;
}

//
// Init tokenizer
//
void XLAnimDefsParser::initTokenizer(XLTokenizer &tokenizer)
{
   tokenizer.setTokenFlags(XLTokenizer::TF_DEFAULT);
}

//
// On EOF
//
void XLAnimDefsParser::onEOF(bool early)
{
   if(!early && state == STATE_EXPECTITEM)
   {
      xldefs.assign(defs);
      xlpics.assign(pics);
      // TODO: display an error on failure
   }
}

//
// Parses the lump
//
void XL_ParseAnimDefs()
{
   XLAnimDefsParser parser;
   parser.parseAll(wGlobalDir);
}

// EOF


