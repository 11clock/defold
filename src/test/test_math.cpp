#include <stdint.h>
#include <gtest/gtest.h>
#include "dlib/math.h"

TEST(dmMath, ConstantsDefined)
{
    ASSERT_NEAR(0.0f, cosf(M_PI_2), 0.0000001f);
}

TEST(dmMath, MinMax)
{
    ASSERT_EQ(1, dmMath::Min(1,2));
    ASSERT_EQ(1, dmMath::Min(2,1));
    ASSERT_EQ(2, dmMath::Max(1,2));
    ASSERT_EQ(2, dmMath::Max(2,1));
}

TEST(dmMath, Clamp)
{
    ASSERT_EQ(1, dmMath::Clamp(1, 0, 2));
    ASSERT_EQ(0, dmMath::Clamp(-1, 0, 2));
    ASSERT_EQ(2, dmMath::Clamp(3, 0, 2));
}

TEST(dmMath, Bezier)
{
    float delta = 0.05f;
    float epsilon = 0.00001f;

    ASSERT_EQ(2.0f, dmMath::LinearBezier(0.0f, 2.0f, 4.0f));
    ASSERT_EQ(3.0f, dmMath::LinearBezier(0.5f, 2.0f, 4.0f));
    ASSERT_EQ(4.0f, dmMath::LinearBezier(1.0f, 2.0f, 4.0f));
    ASSERT_NEAR(dmMath::LinearBezier(delta, 2.0f, 4.0f) - dmMath::LinearBezier(0.0f, 2.0f, 4.0f),
            dmMath::LinearBezier(1.0f, 2.0f, 4.0f) - dmMath::LinearBezier(1.0f - delta, 2.0f, 4.0f),
            epsilon);
    ASSERT_NEAR(dmMath::LinearBezier(delta, 2.0f, 4.0f) - dmMath::LinearBezier(0.0f, 2.0f, 4.0f),
            dmMath::LinearBezier(0.5f, 2.0f, 4.0f) - dmMath::LinearBezier(0.5f - delta, 2.0f, 4.0f),
            epsilon);
    ASSERT_NEAR(dmMath::LinearBezier(0.5f + delta, 2.0f, 4.0f) - dmMath::LinearBezier(0.5f, 2.0f, 4.0f),
            dmMath::LinearBezier(1.0f, 2.0f, 4.0f) - dmMath::LinearBezier(1.0f - delta, 2.0f, 4.0f),
            epsilon);

    ASSERT_EQ(2.0f, dmMath::QuadraticBezier(0.0f, 2.0f, 3.0f, 2.0f));
    ASSERT_EQ(2.5f, dmMath::QuadraticBezier(0.5f, 2.0f, 3.0f, 2.0f));
    ASSERT_EQ(2.0f, dmMath::QuadraticBezier(1.0f, 2.0f, 3.0f, 2.0f));
    ASSERT_NEAR(dmMath::QuadraticBezier(delta, 2.0f, 3.0f, 2.0f) - dmMath::QuadraticBezier(0.0f, 2.0f, 3.0f, 2.0f),
            dmMath::QuadraticBezier(1.0f - delta, 2.0f, 3.0f, 2.0f) - dmMath::QuadraticBezier(1.0f, 2.0f, 3.0f, 2.0f),
            epsilon);
    ASSERT_GT(dmMath::QuadraticBezier(delta, 2.0f, 3.0f, 2.0f) - dmMath::QuadraticBezier(0.0f, 2.0f, 3.0f, 2.0f),
            dmMath::QuadraticBezier(0.5f, 2.0f, 3.0f, 2.0f) - dmMath::QuadraticBezier(0.5f - delta, 2.0f, 3.0f, 2.0f));
    ASSERT_LT(dmMath::QuadraticBezier(0.5f, 2.0f, 3.0f, 2.0f) - dmMath::QuadraticBezier(0.5f + delta, 2.0f, 3.0f, 2.0f),
            dmMath::QuadraticBezier(1.0f - delta, 2.0f, 3.0f, 2.0f) - dmMath::QuadraticBezier(1.0f, 2.0f, 3.0f, 2.0f));

    ASSERT_EQ(2.0f, dmMath::CubicBezier(0.0f, 2.0f, 2.0f, 4.0f, 4.0f));
    ASSERT_EQ(3.0f, dmMath::CubicBezier(0.5f, 2.0f, 2.0f, 4.0f, 4.0f));
    ASSERT_EQ(4.0f, dmMath::CubicBezier(1.0f, 2.0f, 2.0f, 4.0f, 4.0f));
    ASSERT_NEAR(dmMath::CubicBezier(delta, 2.0f, 2.0f, 4.0f, 4.0f) - dmMath::CubicBezier(0.0f, 2.0f, 2.0f, 4.0f, 4.0f),
            dmMath::CubicBezier(1.0f, 2.0f, 2.0f, 4.0f, 4.0f) - dmMath::CubicBezier(1.0f - delta, 2.0f, 2.0f, 4.0f, 4.0f),
            epsilon);
    ASSERT_LT(dmMath::CubicBezier(delta, 2.0f, 2.0f, 4.0f, 4.0f) - dmMath::CubicBezier(0.0f, 2.0f, 2.0f, 4.0f, 4.0f),
            dmMath::CubicBezier(0.5f, 2.0f, 2.0f, 4.0f, 4.0f) - dmMath::CubicBezier(0.5f - delta, 2.0f, 2.0f, 4.0f, 4.0f));
    ASSERT_GT(dmMath::CubicBezier(0.5f + delta, 2.0f, 2.0f, 4.0f, 4.0f) - dmMath::CubicBezier(0.5f, 2.0f, 2.0f, 4.0f, 4.0f),
            dmMath::CubicBezier(1.0f, 2.0f, 2.0f, 4.0f, 4.0f) - dmMath::CubicBezier(1.0f - delta, 2.0f, 2.0f, 4.0f, 4.0f));
}

TEST(dmMath, Select)
{
    float a = 1.0f;
    float b = 2.0f;
    ASSERT_EQ(a, dmMath::Select(0.0f, a, b));
    ASSERT_EQ(a, dmMath::Select(1.0f, a, b));
    ASSERT_EQ(b, dmMath::Select(-1.0f, a, b));
}

// out is [min, max, mean]
void TestRand(float (*rand)(), float* out)
{
    uint32_t iterations = 100000;
    float min = 0.5f;
    float max = 0.5f;
    float sum = 0.0f;
    for (uint32_t i = 0; i < iterations; ++i)
    {
        float r = rand();
        if (min > r)
            min = r;
        if (max < r)
            max = r;
        sum += r;
    }
    out[0] = min;
    out[1] = max;
    out[2] = sum / iterations;
}

TEST(dmMath, Rand)
{
    // windows don't use this so we can't have it in the functions :(
    srand(0);

    float out[3];

    TestRand(dmMath::Rand01, out);
    printf("%.6f %.6f %.6f\n", out[0], out[0], out[0]);
    ASSERT_NEAR(0.0f, out[0], 0.0001f);
    ASSERT_NEAR(1.0f, out[1], 0.0001f);
    ASSERT_NEAR(0.5f, out[2], 0.001f);

    TestRand(dmMath::RandOpen01, out);
    printf("%.6f %.6f %.6f\n", out[0], out[0], out[0]);
    ASSERT_NEAR(0.0f, out[0], 0.0001f);
    ASSERT_NEAR(1.0f, out[1], 0.0001f);
    ASSERT_GT(1.0f, out[1]);
    ASSERT_NEAR(0.5f, out[2], 0.001f);

    TestRand(dmMath::Rand11, out);
    printf("%.6f %.6f %.6f\n", out[0], out[0], out[0]);
    ASSERT_NEAR(-1.0f, out[0], 0.0005f);
    ASSERT_NEAR(1.0f, out[1], 0.0005f);
    ASSERT_NEAR(0.0f, out[2], 0.01f);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
