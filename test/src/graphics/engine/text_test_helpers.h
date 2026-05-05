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

#include "graphics/engine/engine.h"
#include "graphics/engine/text.h"

namespace Gfx
{

/**
 * \class CTextWrapper
 * \brief Exposes protected CText methods for unit testing.
 *
 * Uses the wrapper-class pattern (same as CApplicationWrapper in app_test.cpp)
 * rather than friend declarations, which do not survive the TEST_F macro's
 * implicit derivation.
 */
class CTextWrapper : public CText
{
public:
    explicit CTextWrapper(CEngine* engine)
        : CText(engine)
    {}

    glm::ivec2 GetNextTilePosForTest(const FontTexture& ft)
    {
        return GetNextTilePos(ft);
    }

    static uint64_t PackTileSizeForTest(const glm::ivec2& tileSize)
    {
        return PackTileSize(tileSize);
    }
};

} // namespace Gfx
