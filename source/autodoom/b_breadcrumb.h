// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Copyright(C) 2015 Ioan Chera
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
//      Bot breadcrumb experiment
//
//-----------------------------------------------------------------------------

#ifndef B_BREADCRUMB_H_
#define B_BREADCRUMB_H_

#include "../m_collection.h"
#include "../m_fixed.h"

struct v2fixed_t;

class Breadcrumb : public ZoneObject
{
public:
    struct Link
    {
        int v[2];
    };

    Breadcrumb(fixed_t stepDist) : m_stepDist(stepDist)
    {
    }

    void Reset();
    void TryAdd(fixed_t x, fixed_t y);

    const PODCollection<Link>& edges() const { return m_edges; }
    const PODCollection<v2fixed_t>& dots() const { return m_dots; }

private:

    void RebuildMinCostTree();
   
    const int m_stepDist;

    PODCollection<v2fixed_t> m_dots;
    PODCollection<Link> m_edges;
};

#endif