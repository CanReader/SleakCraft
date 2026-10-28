#ifndef _CAMERA_POSE_HPP_
#define _CAMERA_POSE_HPP_

/// A fixed camera placement for Debug captures: world position plus yaw and
/// pitch in degrees, where positive pitch looks down.
/// @ingroup player
struct CameraPose {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float yawDeg = 0.0f;
    float pitchDeg = 0.0f;
};

/// Number of floats in a CameraPose, as written on the command line.
#define CAMERA_POSE_FIELD_COUNT \
    static_cast<int>(sizeof(CameraPose) / sizeof(float))

#endif
