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
 * \file test/src/graphics/engine/engine_hidpi_test.cpp
 * \brief Unit tests for HiDPI mouse scaling in CEngine
 * 
 * Tests the actual code changes from:
 * - Commit d37850772: Fix small mouse pointer in Hires displays
 * 
 * Tests the CalculateMouseScale() helper method that was extracted
 * from DrawMouse() for testability.
 */

#include "graphics/engine/engine.h"
#include "app/app.h"
#include "common/system/system.h"

#include <gtest/gtest.h>
#include <hippomocks.h>

#include <memory>
#include <cmath>

using namespace Gfx;
using namespace HippoMocks;

/**
 * \class CEngineWrapper
 * \brief Wrapper exposing protected CEngine methods for testing
 * 
 * This wrapper inherits from CEngine and exposes protected methods
 * as public for testing purposes. This is the standard Colobot
 * pattern for testing protected methods.
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
 * \class CEngineHiDPITest
 * \brief Test fixture for CEngine HiDPI mouse scaling tests
 */
class CEngineHiDPITest : public testing::Test
{
protected:
    CEngineHiDPITest();
    ~CEngineHiDPITest() noexcept override;

    void SetUp() override;
    void TearDown() override;

    std::unique_ptr<CEngineWrapper> m_engine;
    MockRepository m_mocks;
    CApplication* m_app = nullptr;
    CSystemUtils* m_systemUtils = nullptr;
};

CEngineHiDPITest::CEngineHiDPITest()
    : m_app(nullptr)
    , m_systemUtils(nullptr)
{}

CEngineHiDPITest::~CEngineHiDPITest() noexcept
{}

void CEngineHiDPITest::SetUp()
{
    m_app = m_mocks.Mock<CApplication>();
    m_systemUtils = m_mocks.Mock<CSystemUtils>();
    m_engine = std::make_unique<CEngineWrapper>(m_app, m_systemUtils);
}

void CEngineHiDPITest::TearDown()
{
    m_engine.reset();
}

// ============================================================================
// Tests for commit d37850772: Mouse pointer scaling
// ============================================================================

/**
 * \test CalculateMouseScale_800x600_Returns1_0
 * \brief Tests that reference size returns scale of 1.0
 * 
 * At the reference size (800x600), the mouse should not be scaled.
 */
TEST_F(CEngineHiDPITest, CalculateMouseScale_800x600_Returns1_0)
{
    glm::ivec2 windowSize(800, 600);
    glm::ivec2 baseMouseSize(32, 32);
    glm::ivec2 hotPoint(8, 8);
    
    auto result = m_engine->CalculateMouseScaleForTest(windowSize, baseMouseSize, hotPoint);
    
    EXPECT_FLOAT_EQ(result.scale, 1.0f);
    EXPECT_EQ(result.scaledSize.x, 32);
    EXPECT_EQ(result.scaledSize.y, 32);
    EXPECT_EQ(result.scaledHotPoint.x, 8);
    EXPECT_EQ(result.scaledHotPoint.y, 8);
}

/**
 * \test CalculateMouseScale_1600x1200_Returns2_0
 * \brief Tests that 2x resolution returns scale of 2.0
 * 
 * At 1600x1200 (2x reference), mouse should be scaled 2x.
 */
TEST_F(CEngineHiDPITest, CalculateMouseScale_1600x1200_Returns2_0)
{
    glm::ivec2 windowSize(1600, 1200);
    glm::ivec2 baseMouseSize(32, 32);
    glm::ivec2 hotPoint(8, 8);
    
    auto result = m_engine->CalculateMouseScaleForTest(windowSize, baseMouseSize, hotPoint);
    
    // Scale = length(1600,1200) / length(800,600) = 2000 / 1000 = 2.0
    EXPECT_FLOAT_EQ(result.scale, 2.0f);
    EXPECT_EQ(result.scaledSize.x, 64);
    EXPECT_EQ(result.scaledSize.y, 64);
    EXPECT_EQ(result.scaledHotPoint.x, 16);
    EXPECT_EQ(result.scaledHotPoint.y, 16);
}

/**
 * \test CalculateMouseScale_1920x1080_ReturnsCorrectScale
 * \brief Tests 1920x1080 resolution (common monitor size)
 */
TEST_F(CEngineHiDPITest, CalculateMouseScale_1920x1080_ReturnsCorrectScale)
{
    glm::ivec2 windowSize(1920, 1080);
    glm::ivec2 baseMouseSize(32, 32);
    glm::ivec2 hotPoint(8, 8);
    
    auto result = m_engine->CalculateMouseScaleForTest(windowSize, baseMouseSize, hotPoint);
    
    // Scale = length(1920,1080) / length(800,600)
    // = sqrt(1920^2 + 1080^2) / sqrt(800^2 + 600^2)
    // = sqrt(3686400 + 1166400) / sqrt(640000 + 360000)
    // = sqrt(4852800) / sqrt(1000000)
    // = 2202.909 / 1000
    // ≈ 2.2029
    float expectedScale = std::sqrt(1920*1920 + 1080*1080) / std::sqrt(800*800 + 600*600);
    EXPECT_NEAR(result.scale, expectedScale, 0.001f);
    EXPECT_EQ(result.scaledSize.x, static_cast<int>(32 * expectedScale));
    EXPECT_EQ(result.scaledSize.y, static_cast<int>(32 * expectedScale));
}

/**
 * \test CalculateMouseScale_640x480_ClampedTo1_0
 * \brief Tests that small resolutions are clamped to scale 1.0
 * 
 * At 640x480 (smaller than reference), scale should be clamped to 1.0
 * to prevent cursor from becoming smaller than base size.
 */
TEST_F(CEngineHiDPITest, CalculateMouseScale_640x480_ClampedTo1_0)
{
    glm::ivec2 windowSize(640, 480);
    glm::ivec2 baseMouseSize(32, 32);
    glm::ivec2 hotPoint(8, 8);
    
    auto result = m_engine->CalculateMouseScaleForTest(windowSize, baseMouseSize, hotPoint);
    
    // Without clamping: scale = 800 / 1000 = 0.8
    // With clamping: scale = max(0.8, 1.0) = 1.0
    EXPECT_FLOAT_EQ(result.scale, 1.0f);
    EXPECT_EQ(result.scaledSize.x, 32);
    EXPECT_EQ(result.scaledSize.y, 32);
    EXPECT_EQ(result.scaledHotPoint.x, 8);
    EXPECT_EQ(result.scaledHotPoint.y, 8);
}

/**
 * \test CalculateMouseScale_ScalesHotPoint
 * \brief Tests that hot point scales proportionally
 */
TEST_F(CEngineHiDPITest, CalculateMouseScale_ScalesHotPoint)
{
    glm::ivec2 windowSize(1600, 1200);
    glm::ivec2 baseMouseSize(32, 32);
    glm::ivec2 hotPoint(12, 16);  // Non-uniform hot point
    
    auto result = m_engine->CalculateMouseScaleForTest(windowSize, baseMouseSize, hotPoint);
    
    EXPECT_FLOAT_EQ(result.scale, 2.0f);
    EXPECT_EQ(result.scaledHotPoint.x, 24);  // 12 * 2.0
    EXPECT_EQ(result.scaledHotPoint.y, 32);  // 16 * 2.0
}

/**
 * \test CalculateMouseScale_ScalesShadowOffset
 * \brief Tests that shadow offset scales proportionally
 */
TEST_F(CEngineHiDPITest, CalculateMouseScale_ScalesShadowOffset)
{
    glm::ivec2 windowSize(1600, 1200);
    glm::ivec2 baseMouseSize(32, 32);
    glm::ivec2 hotPoint(8, 8);
    
    auto result = m_engine->CalculateMouseScaleForTest(windowSize, baseMouseSize, hotPoint);
    
    // Shadow offset should be (4, 3) * scale = (8, 6)
    EXPECT_EQ(result.shadowOffset.x, 8);
    EXPECT_EQ(result.shadowOffset.y, 6);
}

/**
 * \test CalculateMouseScale_ScalesMouseSize
 * \brief Tests that mouse size scales proportionally
 */
TEST_F(CEngineHiDPITest, CalculateMouseScale_ScalesMouseSize)
{
    glm::ivec2 windowSize(2400, 1800);  // 3x reference
    glm::ivec2 baseMouseSize(32, 32);
    glm::ivec2 hotPoint(8, 8);
    
    auto result = m_engine->CalculateMouseScaleForTest(windowSize, baseMouseSize, hotPoint);
    
    EXPECT_FLOAT_EQ(result.scale, 3.0f);
    EXPECT_EQ(result.scaledSize.x, 96);  // 32 * 3.0
    EXPECT_EQ(result.scaledSize.y, 96);  // 32 * 3.0
}

/**
 * \test CalculateMouseScale_PreservesAspectRatio
 * \brief Tests that scaling preserves aspect ratio
 */
TEST_F(CEngineHiDPITest, CalculateMouseScale_PreservesAspectRatio)
{
    // Test with widescreen 16:9 aspect ratio
    glm::ivec2 windowSize(1920, 1080);
    glm::ivec2 baseMouseSize(32, 32);
    glm::ivec2 hotPoint(8, 8);
    
    auto result = m_engine->CalculateMouseScaleForTest(windowSize, baseMouseSize, hotPoint);
    
    // Scaled size should maintain square aspect ratio
    EXPECT_EQ(result.scaledSize.x, result.scaledSize.y);
    
    // Scaled hot point should maintain original aspect ratio
    EXPECT_EQ(result.scaledHotPoint.x, result.scaledHotPoint.y);
}

/**
 * \test CalculateMouseScale_3840x2160_4K
 * \brief Tests 4K resolution scaling
 */
TEST_F(CEngineHiDPITest, CalculateMouseScale_3840x2160_4K)
{
    glm::ivec2 windowSize(3840, 2160);
    glm::ivec2 baseMouseSize(32, 32);
    glm::ivec2 hotPoint(8, 8);
    
    auto result = m_engine->CalculateMouseScaleForTest(windowSize, baseMouseSize, hotPoint);
    
    // Scale = length(3840,2160) / length(800,600)
    // = sqrt(3840^2 + 2160^2) / 1000
    // = sqrt(14745600 + 4665600) / 1000
    // = sqrt(19411200) / 1000
    // = 4405.815 / 1000
    // ≈ 4.406
    float expectedScale = std::sqrt(3840*3840 + 2160*2160) / std::sqrt(800*800 + 600*600);
    EXPECT_NEAR(result.scale, expectedScale, 0.001f);
    
    // Verify all components scale correctly
    EXPECT_EQ(result.scaledSize.x, static_cast<int>(32 * expectedScale));
    EXPECT_EQ(result.scaledSize.y, static_cast<int>(32 * expectedScale));
    EXPECT_EQ(result.scaledHotPoint.x, static_cast<int>(8 * expectedScale));
    EXPECT_EQ(result.scaledHotPoint.y, static_cast<int>(8 * expectedScale));
    EXPECT_EQ(result.shadowOffset.x, static_cast<int>(4 * expectedScale));
    EXPECT_EQ(result.shadowOffset.y, static_cast<int>(3 * expectedScale));
}
