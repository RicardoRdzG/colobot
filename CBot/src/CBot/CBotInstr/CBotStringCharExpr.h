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

#pragma once

#include "CBot/CBotInstr/CBotInstr.h"
#include "CBot/CBotVar/CBotVarString.h"

#include <memory>

namespace CBot
{

/**
 * \brief Proxy variable that maps character writes back to the parent string.
 *
 * Lives as a member of CBotStringCharExpr and is reset on every execution.
 * SetValString() replaces the character at m_index in the parent string.
 */
class CBotVarStringChar : public CBotVarString
{
public:
    CBotVarStringChar(CBotVarString* parent, int index);

    void SetValString(const std::string& val) override;

private:
    CBotVarString* m_parent;
    int            m_index;
};

/**
 * \brief Instruction accessing a character of a string — string[n]
 *
 * Read: returns a one-character string copy.
 * Write: routes the assignment back into the parent string via CBotVarStringChar.
 */
class CBotStringCharExpr : public CBotInstr
{
public:
    CBotStringCharExpr();
    ~CBotStringCharExpr() override;

    bool ExecuteVar(CBotVar* &pVar, CBotCStack* &pile) override;

    bool ExecuteVar(CBotVar* &pVar, CBotStack* &pile, CBotToken* prevToken,
                    bool bStep, bool bExtend) override;

    void RestoreStateVar(CBotStack* &pj, bool bMain) override;

protected:
    const std::string GetDebugName() override { return "CBotStringCharExpr"; }
    std::map<std::string, CBotInstr*> GetDebugLinks() override;

private:
    CBotInstr*                        m_expr  = nullptr;
    std::unique_ptr<CBotVarStringChar> m_proxy;

    friend class CBotExprVar;
    friend class CBotLeftExpr;
};

} // namespace CBot