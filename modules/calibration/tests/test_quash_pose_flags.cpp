#include "quash/QuashPose.h"

#include <gtest/gtest.h>

namespace {

// Build a pose with non-default values across as many fields as practical
// so each test can verify ApplyQuashToPose overwrites all of them rather
// than only the trivially-defaulted ones.
vr::DriverPose_t MakeBusyPose()
{
    vr::DriverPose_t p{};
    p.qWorldFromDriverRotation = { 0.5, 0.5, 0.5, 0.5 };
    p.qDriverFromHeadRotation  = { 0.0, 1.0, 0.0, 0.0 };
    p.qRotation                = { 0.7, 0.0, 0.7, 0.0 };
    p.vecWorldFromDriverTranslation[0] = 1.0;
    p.vecWorldFromDriverTranslation[1] = 2.0;
    p.vecWorldFromDriverTranslation[2] = 3.0;
    p.vecDriverFromHeadTranslation[0] = 4.0;
    p.vecDriverFromHeadTranslation[1] = 5.0;
    p.vecDriverFromHeadTranslation[2] = 6.0;
    p.vecPosition[0]           = 10.0;
    p.vecPosition[1]           = 5.0;
    p.vecPosition[2]           = -3.0;
    p.vecVelocity[0]           = 2.0;
    p.vecVelocity[1]           = -1.0;
    p.vecVelocity[2]           = 0.5;
    p.vecAcceleration[0]       = 0.1;
    p.vecAcceleration[1]       = 9.81;
    p.vecAcceleration[2]       = -0.2;
    p.vecAngularVelocity[0]    = 0.3;
    p.vecAngularVelocity[1]    = 0.4;
    p.vecAngularVelocity[2]    = 1.5;
    p.vecAngularAcceleration[0] = 0.01;
    p.vecAngularAcceleration[1] = 0.02;
    p.vecAngularAcceleration[2] = 0.03;
    p.poseTimeOffset           = 0.05;
    p.poseIsValid              = false;
    p.deviceIsConnected        = false;
    p.result                   = vr::TrackingResult_Uninitialized;
    p.shouldApplyHeadModel     = true;
    p.willDriftInYaw           = true;
    return p;
}

} // namespace

TEST(QuashPoseTest, ParksAtFixedFarAwayPosition)
{
    vr::DriverPose_t pose = MakeBusyPose();
    openvr_pair::common::quash::ApplyQuashToPose(pose);

    EXPECT_DOUBLE_EQ(pose.vecPosition[0], 0.0);
    EXPECT_DOUBLE_EQ(pose.vecPosition[1], openvr_pair::common::quash::kQuashParkY);
    EXPECT_DOUBLE_EQ(pose.vecPosition[2], 0.0);
}

TEST(QuashPoseTest, ZeroesDerivativesAndDriverTransforms)
{
    vr::DriverPose_t pose = MakeBusyPose();
    openvr_pair::common::quash::ApplyQuashToPose(pose);

    EXPECT_DOUBLE_EQ(pose.qRotation.w, 1.0);
    EXPECT_DOUBLE_EQ(pose.qRotation.x, 0.0);
    EXPECT_DOUBLE_EQ(pose.qRotation.y, 0.0);
    EXPECT_DOUBLE_EQ(pose.qRotation.z, 0.0);
    EXPECT_DOUBLE_EQ(pose.qWorldFromDriverRotation.w, 1.0);
    EXPECT_DOUBLE_EQ(pose.qWorldFromDriverRotation.x, 0.0);
    EXPECT_DOUBLE_EQ(pose.qDriverFromHeadRotation.w, 1.0);
    EXPECT_DOUBLE_EQ(pose.qDriverFromHeadRotation.x, 0.0);
    for (int i = 0; i < 3; ++i) {
        EXPECT_DOUBLE_EQ(pose.vecVelocity[i], 0.0);
        EXPECT_DOUBLE_EQ(pose.vecAcceleration[i], 0.0);
        EXPECT_DOUBLE_EQ(pose.vecAngularVelocity[i], 0.0);
        EXPECT_DOUBLE_EQ(pose.vecAngularAcceleration[i], 0.0);
        EXPECT_DOUBLE_EQ(pose.vecWorldFromDriverTranslation[i], 0.0);
        EXPECT_DOUBLE_EQ(pose.vecDriverFromHeadTranslation[i], 0.0);
    }
    EXPECT_DOUBLE_EQ(pose.poseTimeOffset, 0.0);
}

TEST(QuashPoseTest, ForcesConnectedValidOutOfRange)
{
    vr::DriverPose_t pose{};
    pose.deviceIsConnected = false;
    pose.poseIsValid       = false;
    pose.result            = vr::TrackingResult_Uninitialized;

    openvr_pair::common::quash::ApplyQuashToPose(pose);

    EXPECT_TRUE(pose.deviceIsConnected);
    EXPECT_TRUE(pose.poseIsValid);
    EXPECT_EQ(pose.result, vr::TrackingResult_Calibrating_OutOfRange);
}

TEST(QuashPoseTest, OutputIsConstantAcrossArbitraryInputs)
{
    vr::DriverPose_t a = MakeBusyPose();
    vr::DriverPose_t b{};
    b.vecPosition[0] = 999.0;
    b.vecPosition[1] = 999.0;
    b.vecPosition[2] = 999.0;
    b.qRotation = { 0.0, 0.0, 0.0, 1.0 };

    openvr_pair::common::quash::ApplyQuashToPose(a);
    openvr_pair::common::quash::ApplyQuashToPose(b);

    EXPECT_DOUBLE_EQ(a.vecPosition[0], b.vecPosition[0]);
    EXPECT_DOUBLE_EQ(a.vecPosition[1], b.vecPosition[1]);
    EXPECT_DOUBLE_EQ(a.vecPosition[2], b.vecPosition[2]);
    EXPECT_DOUBLE_EQ(a.qRotation.w, b.qRotation.w);
    EXPECT_EQ(a.deviceIsConnected, b.deviceIsConnected);
    EXPECT_EQ(a.poseIsValid, b.poseIsValid);
    EXPECT_EQ(a.result, b.result);
}
