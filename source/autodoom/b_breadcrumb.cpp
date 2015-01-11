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

#include <algorithm>
#include "../z_zone.h"
#include "b_breadcrumb.h"
#include "../m_vector.h"
#include "../p_maputl.h"

void Breadcrumb::Reset()
{
    m_dots.makeEmpty();
    m_edges.makeEmpty();
}

void Breadcrumb::TryAdd(fixed_t x, fixed_t y)
{
    fixed_t dist;
    //fixed_t mindist = D_MAXINT;
    //int mini = -1;

    for (int i = (int)m_dots.getLength() - 1; i >= 0; --i)
    {
        const v2fixed_t& v = m_dots[i];
        dist = P_AproxDistance(v.x - x, v.y - y);
        if (dist < m_stepDist)
            return; // exit
        //if (dist < mindist)
        //{
        //    mini = i;
        //    mindist = dist;
        //}
    }

    // okay
    v2fixed_t newv;
    newv.x = x;
    newv.y = y;
    m_dots.add(newv);

    RebuildMinCostTree();

    //if (mini >= 0)
    //{
    //    Link newl;
    //    newl.v[0] = mini;
    //    newl.v[1] = m_dots.getLength() - 1;
    //    m_edges.add(newl);
    //}
}

void Breadcrumb::RebuildMinCostTree()
{
    struct LinkDist
    {
        Link link;
        fixed_t len;
        bool operator < (const LinkDist& o) const 
        {
            return len < o.len;
        }
    };

    if (m_dots.getLength() < 2)
        return;

    int n = (int)m_dots.getLength();
    PODCollection<LinkDist> allLinks;
    LinkDist ld;
    for (int i = 0; i < n - 1; ++i)
    {
        for (int j = i + 1; j < n; ++j)
        {
            ld.link.v[0] = i;
            ld.link.v[1] = j;
            ld.len = P_AproxDistance(m_dots[i], m_dots[j]);
            allLinks.add(ld);
        }
    }
    std::sort(allLinks.begin(), allLinks.end());
    m_edges.makeEmpty();
    for (int i = 0; i < (int)m_dots.getLength() - 1; ++i)
    {
        m_edges.add(allLinks[i].link);
    }
}