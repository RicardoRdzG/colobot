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

/**
 * \file test/src/graphics/engine/text_atlas_sizing_test.cpp
 * \brief Unit tests for per-atlas texture sizing in CText
 * 
 * Tests the implementation from Phase 1 of CJK font support:
 * - FontTexture struct with textureSize field
 * - Per-atlas sizing logic in GetNextTilePos()
 */

#include "text_test_helpers.h"

#include <gtest/gtest.h>
#include <hippomocks.h>

#include <memory>

using namespace Gfx;
using namespace HippoMocks;

/**
 * \class CTextAtlasSizingTest
 * \brief Test fixture for per-atlas sizing tests
 */
class CTextAtlasSizingTest : public testing::Test
{
protected:
    CTextAtlasSizingTest()
        : m_engine(nullptr)
    {}

    ~CTextAtlasSizingTest() noexcept override {}

    void SetUp() override
    {
        m_engine = m_mocks.Mock<CEngine>();
        m_text = std::make_unique<CTextWrapper>(m_engine);
    }

    void TearDown() override
    {
        m_text.reset();
    }

    std::unique_ptr<CTextWrapper> m_text;
    MockRepository m_mocks;
    CEngine* m_engine;
};

// ============================================================================
// Tests for Per-Atlas Sizing
// ============================================================================

/**
 * \test GetNextTilePos_UsesAtlasTextureSize
 * \brief Tests that GetNextTilePos uses FontTexture::textureSize
 */
TEST_F(CTextAtlasSizingTest, GetNextTilePos_UsesAtlasTextureSize)
{
    FontTexture ft;
    ft.id = 0;
    ft.tileSize = {32, 32};
    ft.textureSize = {512, 512};  // Per-atlas size
    ft.freeSlots = 255;  // First tile
    
    auto result = m_text->GetNextTilePosForTest(ft);
    
    // With 512x512 and 32x32 tiles: 16x16 grid = 256 slots
    // First tile (tileNumber = 256 - 255 = 1): position (32, 0)
    EXPECT_EQ(result.x, 32);
    EXPECT_EQ(result.y, 0);
    
    // Test second tile
    ft.freeSlots = 254;  // Tile 2
    result = m_text->GetNextTilePosForTest(ft);
    
    // Tile 2: row 0, column 2 -> (64, 0)
    EXPECT_EQ(result.x, 64);
    EXPECT_EQ(result.y, 0);
}

/**
 * \test GetNextTilePos_DifferentAtlasSizes
 * \brief Tests multiple atlases with different sizes
 */
TEST_F(CTextAtlasSizingTest, GetNextTilePos_DifferentAtlasSizes)
{
    // Small atlas: 256x256 with 16x16 tiles
    FontTexture smallAtlas;
    smallAtlas.id = 1;
    smallAtlas.tileSize = {16, 16};
    smallAtlas.textureSize = {256, 256};  // 16x16 grid = 256 slots
    smallAtlas.freeSlots = 255;
    
    auto result = m_text->GetNextTilePosForTest(smallAtlas);
    EXPECT_EQ(result.x, 16);
    EXPECT_EQ(result.y, 0);
    
    // Large atlas: 1024x1024 with 64x64 tiles
    FontTexture largeAtlas;
    largeAtlas.id = 2;
    largeAtlas.tileSize = {64, 64};
    largeAtlas.textureSize = {1024, 1024};  // 16x16 grid = 256 slots
    largeAtlas.freeSlots = 255;
    
    result = m_text->GetNextTilePosForTest(largeAtlas);
    EXPECT_EQ(result.x, 64);
    EXPECT_EQ(result.y, 0);
}

/**
 * \test GetNextTilePos_MaxAtlasSize_2048
 * \brief Tests maximum atlas size (2048x2048)
 */
TEST_F(CTextAtlasSizingTest, GetNextTilePos_MaxAtlasSize_2048)
{
    FontTexture ft;
    ft.id = 0;
    ft.tileSize = {64, 64};
    ft.textureSize = {2048, 2048};  // 32x32 grid = 1024 slots
    ft.freeSlots = 1024;  // All slots free, first tile (tileNumber = 0)
    
    auto result = m_text->GetNextTilePosForTest(ft);
    
    // First tile: position (0, 0)
    EXPECT_EQ(result.x, 0);
    EXPECT_EQ(result.y, 0);
    
    // Test last valid tile position
    ft.freeSlots = 1;  // Last slot (tileNumber = 1024 - 1 = 1023)
    result = m_text->GetNextTilePosForTest(ft);
    
    // With 2048x2048 and 64x64 tiles: 32x32 grid
    // Tile 1023: row 31, column 31 -> (31*64, 31*64) = (1984, 1984)
    EXPECT_EQ(result.x, 1984);
    EXPECT_EQ(result.y, 1984);
}

/**
 * \test GetNextTilePos_EdgeCase_TileLargerThanAtlas
 * \brief Tests tile larger than atlas (edge case)
 */
TEST_F(CTextAtlasSizingTest, GetNextTilePos_EdgeCase_TileLargerThanAtlas)
{
    FontTexture ft;
    ft.id = 0;
    ft.tileSize = {512, 512};  // Larger than atlas
    ft.textureSize = {256, 256};
    ft.freeSlots = 0;
    
    // Should not crash, but result is undefined behavior
    // The calculation will produce invalid values
    auto result = m_text->GetNextTilePosForTest(ft);
    
    // Just verify it doesn't crash
    // Result is undefined when tile doesn't fit
}

// ============================================================================
// Tests for PackTileSize (hash key generation)
// ============================================================================

/**
 * \test PackTileSize_BasicPacking
 * \brief Tests that PackTileSize correctly packs tileSize into uint64
 */
TEST_F(CTextAtlasSizingTest, PackTileSize_BasicPacking)
{
    // Test basic packing: x in high 32 bits, y in low 32 bits
    auto key1 = CTextWrapper::PackTileSizeForTest({16, 32});
    auto key2 = CTextWrapper::PackTileSizeForTest({32, 16});
    
    // Different tile sizes should produce different keys
    EXPECT_NE(key1, key2);
    
    // Same values should produce same key
    auto key3 = CTextWrapper::PackTileSizeForTest({16, 32});
    EXPECT_EQ(key1, key3);
}

/**
 * \test PackTileSize_LargeValues
 * \brief Tests PackTileSize with larger tile sizes
 */
TEST_F(CTextAtlasSizingTest, PackTileSize_LargeValues)
{
    // Test with realistic atlas sizes
    auto key256 = CTextWrapper::PackTileSizeForTest({256, 256});
    auto key512 = CTextWrapper::PackTileSizeForTest({512, 512});
    auto key1024 = CTextWrapper::PackTileSizeForTest({1024, 1024});
    
    // All should be different
    EXPECT_NE(key256, key512);
    EXPECT_NE(key512, key1024);
    EXPECT_NE(key256, key1024);
    
    // Verify bit packing: high 32 bits = x, low 32 bits = y
    // For 256x256: key should have 256 in both halves
    uint32_t high = static_cast<uint32_t>(key256 >> 32);
    uint32_t low = static_cast<uint32_t>(key256 & 0xFFFFFFFF);
    EXPECT_EQ(high, 256);
    EXPECT_EQ(low, 256);
}

/**
 * \test PackTileSize_AsymmetricSizes
 * \brief Tests PackTileSize with different x and y values
 */
TEST_F(CTextAtlasSizingTest, PackTileSize_AsymmetricSizes)
{
    auto key = CTextWrapper::PackTileSizeForTest({64, 128});

    uint32_t high = static_cast<uint32_t>(key >> 32);
    uint32_t low = static_cast<uint32_t>(key & 0xFFFFFFFF);

    EXPECT_EQ(high, 64);
    EXPECT_EQ(low, 128);
}

// ============================================================================
// Tests for PackTileSize hash key properties
// ============================================================================

/**
 * \test PackTileSize_DifferentKeysForDifferentSizes
 * \brief Tests that different tile sizes produce different hash keys
 */
TEST_F(CTextAtlasSizingTest, PackTileSize_DifferentKeysForDifferentSizes)
{
    // These are the hash keys that would be used for different tile sizes
    auto key16 = CTextWrapper::PackTileSizeForTest({16, 16});
    auto key32 = CTextWrapper::PackTileSizeForTest({32, 32});
    auto key64 = CTextWrapper::PackTileSizeForTest({64, 64});

    // All should be unique
    EXPECT_NE(key16, key32);
    EXPECT_NE(key32, key64);
    EXPECT_NE(key16, key64);

    // Verify asymmetric sizes also work correctly
    auto key32x64 = CTextWrapper::PackTileSizeForTest({32, 64});
    auto key64x32 = CTextWrapper::PackTileSizeForTest({64, 32});

    // Swapped dimensions should produce different keys
    EXPECT_NE(key32x64, key64x32);
}