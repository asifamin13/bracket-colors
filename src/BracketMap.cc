/*
 *      BracketMap.cc
 *
 *      Copyright 2013 Asif Amin <asifamin@utexas.edu>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stack>
#include <set>

#include "BracketMap.h"


// -----------------------------------------------------------------------------
    BracketMap::BracketMap()
/*
    Constructor
----------------------------------------------------------------------------- */
{

}


// -----------------------------------------------------------------------------
    BracketMap::~BracketMap()
/*
    Destructor
----------------------------------------------------------------------------- */
{

}


// -----------------------------------------------------------------------------
    void BracketMap::Update(Index index, Length length)
/*

----------------------------------------------------------------------------- */
{
    auto it = mBracketMap.find(index);
    if (it != mBracketMap.end()) {
        auto &bracket = it->second;
        GetLength(bracket) = length;
    }
    else {
        mBracketMap.insert(
            std::make_pair(index, std::make_tuple(length, 0))
        );
    }
}



// -----------------------------------------------------------------------------
    void BracketMap::ComputeOrder()
/*

----------------------------------------------------------------------------- */
{
    std::stack<Index> orderStack;

    for (auto &it : mBracketMap) {

        const Index &startIndex = it.first;
        Bracket &bracket = it.second;
        Length length = GetLength(bracket);
        Index endPos = startIndex + length;

        if (length == UNDEFINED) {
            // Invalid brackets
            GetOrder(bracket) = UNDEFINED;
            continue;
        }


        if (orderStack.size() == 0) {
            // First bracket
            orderStack.push(endPos);
        }
        else if (startIndex > orderStack.top()) {
            // not nested
            while(orderStack.size() > 0 and orderStack.top() < startIndex) {
                orderStack.pop();
            }
            orderStack.push(endPos);
        }
        else {
            // nested bracket
            orderStack.push(endPos);
        }

        GetOrder(bracket) = orderStack.size() - 1;
    }
}



// -----------------------------------------------------------------------------
    void BracketMap::Show()
/*

----------------------------------------------------------------------------- */
{   g_debug("%s: Showing bracket map ...", __FUNCTION__);

    for (const auto it : mBracketMap) {

        const Index &startIndex = it.first;
        const Bracket &bracket = it.second;

        Length length = std::get<0>(bracket);
        Order order = std::get<1>(bracket);

        Index end = -1;
        if (length > 0) {
            end = startIndex + length;
        }

        g_debug(
            "%s: Bracket at %d, Length: %d, End: %d, Order: %d",
            __FUNCTION__, startIndex, length, end, order
        );
    }

    g_debug("%s: ... Finished showing bracket map", __FUNCTION__);
}

