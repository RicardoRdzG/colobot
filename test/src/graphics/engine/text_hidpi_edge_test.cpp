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
 * \file test/src/graphics/engine/text_hidpi_edge_test.cpp
 * \brief Edge case tests for HiDPI text rendering
 * 
 * Tests edge cases and error conditions for:
 * - Commit c790fe49a: Fix small font in HiRes displays (GetNextTilePos guard)
 * - Commit 90d37119e: Use dynamic font texture size
 */

#include "text_test_helpers.h"
#include "app/app.h"
#include "common/system/system.h"

#include <gtest/gtest.h>
#include <hippomocks.h>

#include <memory>

using namespace Gfx;
using namespace HippoMocks;

/**
 * \class CTextHiDPIEdgeTest
 * \brief Test fixture for CText edge case tests
 */
class CTextHiDPIEdgeTest : public testing::Test
{
protected:
    CTextHiDPIEdgeTest();
    ~CTextHiDPIEdgeTest() noexcept override;

    void SetUp() override;
    void TearDown() override;

    std::unique_ptr<CTextWrapper> m_text;
    MockRepository m_mocks;
    CEngine* m_engine = nullptr;
};

CTextHiDPIEdgeTest::CTextHiDPIEdgeTest()
    : m_engine(nullptr)
{}

CTextHiDPIEdgeTest::~CTextHiDPIEdgeTest() noexcept
{}

void CTextHiDPIEdgeTest::SetUp()
{
    m_engine = m_mocks.Mock<CEngine>();
    m_text = std::make_unique<CTextWrapper>(m_engine);
}

void CTextHiDPIEdgeTest::TearDown()
{
    m_text.reset();
}

// ============================================================================
// Edge Case Tests for GetNextTilePos
// ============================================================================

/**
 * \test GetNextTilePos_ZeroTileSize_HandledGracefully
 * \brief Tests that zero tile size doesn't crash
 * 
 * Edge case: tileSize of (0, 0) should not cause division by zero
 * or other crashes. The std::max(1, ...) guards should prevent this.
 */
TEST_F(CTextHiDPIEdgeTest, GetNextTilePos_ZeroTileSize_HandledGracefully)
{
    FontTexture ft;
    ft.id = 0;
    ft.tileSize = {0, 0};  // Invalid size
    ft.textureSize = {256, 256};
    ft.freeSlots = 0;
    
    // Should not crash
    auto result = m_text->GetNextTilePosForTest(ft);
    
    // Result should be (0, 0) with the guards
    EXPECT_EQ(result.x, 0);
    EXPECT_EQ(result.y, 0);
}

/**
 * \test GetNextTilePos_ZeroTextureSize_HandledGracefully
 * \brief Tests that zero texture size doesn't crash
 * 
 * Edge case: Texture size of (0, 0) should not cause division by zero.
 */
TEST_F(CTextHiDPIEdgeTest, GetNextTilePos_ZeroTextureSize_HandledGracefully)
{
    FontTexture ft;
    ft.id = 0;
    ft.tileSize = {64, 64};
    ft.textureSize = {0, 0};  // Invalid size
    ft.freeSlots = 0;
    
    // Should not crash
    auto result = m_text->GetNextTilePosForTest(ft);
    
    // Result should be (0, 0) with the guards
    EXPECT_EQ(result.x, 0);
    EXPECT_EQ(result.y, 0);
}

/**
 * \test GetNextTilePos_NegativeTileSize_HandledGracefully
 * \brief Tests that negative tile size doesn't crash
 *
 * Edge case: negative tileSize is invalid input; CreateFontTexture asserts
 * tileSize > 0, so this should never reach GetNextTilePos in production.
 * Verify the guard (std::max(1, tileSize)) prevents a division-by-zero crash.
 */
TEST_F(CTextHiDPIEdgeTest, GetNextTilePos_NegativeTileSize_HandledGracefully)
{
    FontTexture ft;
    ft.id = 0;
    ft.tileSize = {-64, -64};  // Invalid negative size
    ft.textureSize = {256, 256};
    ft.freeSlots = 0;

    // Should not crash - std::max(1, -64) = 1 prevents division by zero
    // We only check for no-crash; the return value is undefined for invalid input.
    (void)m_text->GetNextTilePosForTest(ft);
    SUCCEED();
}

/**
 * \test GetNextTilePos_NegativeTextureSize_HandledGracefully
 * \brief Tests that negative texture size doesn't crash
 * 
 * Edge case: Negative texture size should not cause crashes.
 */
TEST_F(CTextHiDPIEdgeTest, GetNextTilePos_NegativeTextureSize_HandledGracefully)
{
    FontTexture ft;
    ft.id = 0;
    ft.tileSize = {64, 64};
    ft.textureSize = {-256, -256};  // Invalid negative size
    ft.freeSlots = 0;
    
    // Should not crash
    auto result = m_text->GetNextTilePosForTest(ft);
    
    // Should produce valid result (or 0,0)
    EXPECT_GE(result.x, 0);
    EXPECT_GE(result.y, 0);
}

/**
 * \test GetNextTilePos_VeryLargeTileSize_HandledGracefully
 * \brief Tests very large tile size relative to texture
 * 
 * Edge case: Tile size larger than texture should not crash.
 * This tests the fix from commit c790fe49a.
 */
TEST_F(CTextHiDPIEdgeTest, GetNextTilePos_VeryLargeTileSize_HandledGracefully)
{
    FontTexture ft;
    ft.id = 0;
    ft.tileSize = {10000, 10000};  // Much larger than texture
    ft.textureSize = {256, 256};
    ft.freeSlots = 0;
    
    // Should not crash - this is the original bug fix
    auto result = m_text->GetNextTilePosForTest(ft);
    
    EXPECT_EQ(result.x, 0);
    EXPECT_EQ(result.y, 0);
}

/**
 * \test GetNextTilePos_AsymmetricSizes_HandledGracefully
 * \brief Tests asymmetric tile/texture sizes
 */
TEST_F(CTextHiDPIEdgeTest, GetNextTilePos_AsymmetricSizes_HandledGracefully)
{
    FontTexture ft;
    ft.id = 0;
    ft.tileSize = {64, 128};  // Tall tile
    ft.textureSize = {1024, 256};  // Wide texture
    ft.freeSlots = 0;  // First tile
    
    // Should not crash
    auto result = m_text->GetNextTilePosForTest(ft);
    
    // With 1024x256 texture and 64x128 tiles:
    // horizontalTiles = 1024/64 = 16, verticalTiles = 256/128 = 2
    // totalTiles = 16*2 = 32, tileNumber = 32 - 0 = 32
    // verticalTileIndex = 32 / 16 = 2, horizontalTileIndex = 32 % 16 = 0
    // Position: (0 * 64, 2 * 128) = (0, 256)
    // Note: This is actually beyond texture bounds, but that's the calculation
    EXPECT_EQ(result.x, 0);
    EXPECT_EQ(result.y, 256);  // Beyond texture height
}

/**
 * \test GetNextTilePos_MaxTextureSize_2048
 * \brief Tests with maximum texture size (2048x2048)
 * 
 * Commit 90d37119e caps texture size at 2048.
 */
TEST_F(CTextHiDPIEdgeTest, GetNextTilePos_MaxTextureSize_2048)
{
    FontTexture ft;
    ft.id = 0;
    ft.tileSize = {64, 64};  // 32x32 = 1024 tiles
    ft.textureSize = {2048, 2048};
    ft.freeSlots = 1024;  // First tile (totalTiles - freeSlots = 1024 - 1024 = 0)
    
    auto result = m_text->GetNextTilePosForTest(ft);
    
    // First tile should be at (0, 0)
    EXPECT_EQ(result.x, 0);
    EXPECT_EQ(result.y, 0);
    
    // Test last tile
    ft.freeSlots = 1;  // Last tile (1024 - 1 = 1023)
    result = m_text->GetNextTilePosForTest(ft);
    
    // With 2048x2048 and 64x64 tiles: 32x32 grid
    // Tile 1023: row = 1023/32 = 31, col = 1023%32 = 31
    // Position: (31*64, 31*64) = (1984, 1984)
    EXPECT_EQ(result.x, 1984);
    EXPECT_EQ(result.y, 1984);
}

// ============================================================================
// Edge Case Tests for Mouse Scaling
// ============================================================================

/**
 * \class CEngineWrapper
 * \brief Wrapper exposing protected CEngine methods for testing
 */
class CEngineWrapper : public CEngine
{
public:
    CEngineWrapper(CApplication* app, CSystemUtils* systemUtils)
        : CEngine(app, systemUtils)
    {}
    
    MouseScaleData CalculateMouseScaleForTest(
        glm::ivec2 windowSize,
        glm::ivec2 baseMouseSize,
        glm::ivec2 hotPoint
    )
    {
        return CalculateMouseScale(windowSize, baseMouseSize, hotPoint);
    }
};

/**
 * \class CEngineHiDPIEdgeTest
 * \brief Test fixture for CEngine edge case tests
 */
class CEngineHiDPIEdgeTest : public testing::Test
{
protected:
    CEngineHiDPIEdgeTest();
    ~CEngineHiDPIEdgeTest() noexcept override;

    void SetUp() override;
    void TearDown() override;

    std::unique_ptr<CEngineWrapper> m_engine;
    MockRepository m_mocks;
    CApplication* m_app = nullptr;
    CSystemUtils* m_systemUtils = nullptr;
};

CEngineHiDPIEdgeTest::CEngineHiDPIEdgeTest()
    : m_app(nullptr)
    , m_systemUtils(nullptr)
{}

CEngineHiDPIEdgeTest::~CEngineHiDPIEdgeTest() noexcept
{}

void CEngineHiDPIEdgeTest::SetUp()
{
    m_app = m_mocks.Mock<CApplication>();
    m_systemUtils = m_mocks.Mock<CSystemUtils>();
    m_engine = std::make_unique<CEngineWrapper>(m_app, m_systemUtils);
}

void CEngineHiDPIEdgeTest::TearDown()
{
    m_engine.reset();
}

/**
 * \test CalculateMouseScale_ZeroWindowSize_Returns1_0
 * \brief Tests that zero window size doesn't crash
 */
TEST_F(CEngineHiDPIEdgeTest, CalculateMouseScale_ZeroWindowSize_Returns1_0)
{
    glm::ivec2 windowSize(0, 0);
    glm::ivec2 baseMouseSize(32, 32);
    glm::ivec2 hotPoint(8, 8);
    
    // Should not crash, should clamp to 1.0
    auto result = m_engine->CalculateMouseScaleForTest(windowSize, baseMouseSize, hotPoint);
    
    // Length of (0,0) is 0, so scale = 0 / 1000 = 0, clamped to 1.0
    EXPECT_FLOAT_EQ(result.scale, 1.0f);
    EXPECT_EQ(result.scaledSize.x, 32);
    EXPECT_EQ(result.scaledSize.y, 32);
}

/**
 * \test CalculateMouseScale_NegativeWindowSize_Returns1_0
 * \brief Tests that negative window size doesn't crash
 */
TEST_F(CEngineHiDPIEdgeTest, CalculateMouseScale_NegativeWindowSize_Returns1_0)
{
    glm::ivec2 windowSize(-800, -600);
    glm::ivec2 baseMouseSize(32, 32);
    glm::ivec2 hotPoint(8, 8);
    
    // Should not crash
    auto result = m_engine->CalculateMouseScaleForTest(windowSize, baseMouseSize, hotPoint);
    
    // Length of negative vector is positive, so scale will be positive
    // length(-800,-600) = length(800,600) = 1000, so scale = 1000/1000 = 1.0
    EXPECT_FLOAT_EQ(result.scale, 1.0f);
}

/**
 * \test CalculateMouseScale_ZeroHotPoint_ReturnsZeroOffset
 * \brief Tests that zero hot point works correctly
 */
TEST_F(CEngineHiDPIEdgeTest, CalculateMouseScale_ZeroHotPoint_ReturnsZeroOffset)
{
    glm::ivec2 windowSize(1600, 1200);
    glm::ivec2 baseMouseSize(32, 32);
    glm::ivec2 hotPoint(0, 0);  // Zero hot point
    
    auto result = m_engine->CalculateMouseScaleForTest(windowSize, baseMouseSize, hotPoint);
    
    EXPECT_FLOAT_EQ(result.scale, 2.0f);
    EXPECT_EQ(result.scaledHotPoint.x, 0);
    EXPECT_EQ(result.scaledHotPoint.y, 0);
}

/**
 * \test CalculateMouseScale_ZeroBaseMouseSize_ReturnsZeroSize
 * \brief Tests that zero base mouse size works correctly
 */
TEST_F(CEngineHiDPIEdgeTest, CalculateMouseScale_ZeroBaseMouseSize_ReturnsZeroSize)
{
    glm::ivec2 windowSize(1600, 1200);
    glm::ivec2 baseMouseSize(0, 0);  // Zero size
    glm::ivec2 hotPoint(8, 8);
    
    auto result = m_engine->CalculateMouseScaleForTest(windowSize, baseMouseSize, hotPoint);
    
    EXPECT_FLOAT_EQ(result.scale, 2.0f);
    EXPECT_EQ(result.scaledSize.x, 0);
    EXPECT_EQ(result.scaledSize.y, 0);
}

/**
 * \test CalculateMouseScale_VeryLargeWindowSize_ClampedReasonably
 * \brief Tests very large window size (8K)
 */
TEST_F(CEngineHiDPIEdgeTest, CalculateMouseScale_VeryLargeWindowSize_ClampedReasonably)
{
    glm::ivec2 windowSize(7680, 4320);  // 8K resolution
    glm::ivec2 baseMouseSize(32, 32);
    glm::ivec2 hotPoint(8, 8);
    
    auto result = m_engine->CalculateMouseScaleForTest(windowSize, baseMouseSize, hotPoint);
    
    // Scale should be around 8.8 for 8K
    float expectedScale = std::sqrt(7680*7680 + 4320*4320) / std::sqrt(800*800 + 600*600);
    EXPECT_NEAR(result.scale, expectedScale, 0.01f);
    
    // Mouse should scale proportionally but remain usable
    EXPECT_GT(result.scaledSize.x, 32);
    EXPECT_GT(result.scaledSize.y, 32);
}
