/*
 * This file is part of the Colobot: Gold Edition source code
 * Copyright (C) 2001-2023, Daniel Roux, EPSITEC SA & TerranovaTeam
 * http://epsitec.ch; http://colobot.info; http://github.com/colobot
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see http://gnu.org/licenses
 */

#include "CBot/CBotInstr/CBotStringCharExpr.h"

#include "CBot/CBotStack.h"
#include "CBot/CBotCStack.h"
#include "CBot/CBotToken.h"
#include "CBot/CBotEnums.h"

namespace CBot
{

////////////////////////////////////////////////////////////////////////////////
// CBotVarStringChar

CBotVarStringChar::CBotVarStringChar(CBotVarString* parent, int index)
    : CBotVarString(CBotToken("$sc"))
    , m_parent(parent)
    , m_index(index)
{
    const std::string& s = parent->GetValString();
    if (index >= 0 && index < static_cast<int>(s.size()))
        CBotVarString::SetValString(std::string(1, s[index]));
}

void CBotVarStringChar::SetValString(const std::string& val)
{
    std::string s = m_parent->GetValString();
    if (m_index >= 0 && m_index < static_cast<int>(s.size()) && !val.empty())
        s[m_index] = val[0];
    m_parent->SetValString(s);
    CBotVarString::SetValString(val.empty() ? "" : std::string(1, val[0]));
}

////////////////////////////////////////////////////////////////////////////////
// CBotStringCharExpr

CBotStringCharExpr::CBotStringCharExpr()
{
}

CBotStringCharExpr::~CBotStringCharExpr()
{
    delete m_expr;
}

////////////////////////////////////////////////////////////////////////////////
bool CBotStringCharExpr::ExecuteVar(CBotVar* &pVar, CBotCStack* &pile)
{
    // Compile-time type check: a character of a string is also a string.
    // pVar already points to the CBotTypString parent — leave it unchanged.
    if (pVar == nullptr || pVar->GetType() != CBotTypString)
    {
        pile->SetError(CBotErrBadIndex, m_token.GetEnd());
        return false;
    }
    if (m_next3 != nullptr) return m_next3->ExecuteVar(pVar, pile);
    return true;
}

////////////////////////////////////////////////////////////////////////////////
bool CBotStringCharExpr::ExecuteVar(CBotVar* &pVar, CBotStack* &pile,
                                     CBotToken* prevToken, bool bStep, bool bExtend)
{
    CBotStack* pj = pile;

    if (pVar == nullptr || pVar->GetType() != CBotTypString)
    {
        pile->SetError(CBotErrBadIndex, prevToken);
        return pj->Return(pile);
    }

    pile = pile->AddStack();

    if (pile->GetState() == 0)
    {
        if (!m_expr->Execute(pile)) return false;
        pile->IncState();
    }

    CBotVar* p = pile->GetVar();
    if (p == nullptr || p->GetType() > CBotTypDouble)
    {
        pile->SetError(CBotErrBadIndex, prevToken);
        return pj->Return(pile);
    }

    int n = p->GetValInt();
    const std::string s = pVar->GetValString();

    if (n < 0 || n >= static_cast<int>(s.size()))
    {
        pile->SetError(CBotErrOutArray, prevToken);
        return pj->Return(pile);
    }

    if (bExtend)
    {
        // Write path: return a proxy that propagates assignment back to parent.
        m_proxy = std::make_unique<CBotVarStringChar>(
            static_cast<CBotVarString*>(pVar), n);
        pVar = m_proxy.get();
    }
    else
    {
        // Read path: set proxy to single-char string for the caller to copy.
        m_proxy = std::make_unique<CBotVarStringChar>(
            static_cast<CBotVarString*>(pVar), n);
        pVar = m_proxy.get();
    }

    if (m_next3 != nullptr &&
        !m_next3->ExecuteVar(pVar, pile, prevToken, bStep, bExtend)) return false;

    return true;
}

////////////////////////////////////////////////////////////////////////////////
void CBotStringCharExpr::RestoreStateVar(CBotStack* &pile, bool bMain)
{
    pile = pile->RestoreStack();
    if (pile == nullptr) return;

    if (bMain && pile->GetState() == 0)
    {
        m_expr->RestoreState(pile, true);
        return;
    }

    if (m_next3)
        m_next3->RestoreStateVar(pile, bMain);
}

std::map<std::string, CBotInstr*> CBotStringCharExpr::GetDebugLinks()
{
    auto links = CBotInstr::GetDebugLinks();
    links["m_expr"] = m_expr;
    return links;
}

} // namespace CBot
