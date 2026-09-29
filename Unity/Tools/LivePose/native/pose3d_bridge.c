#include <math.h>
#include <stdint.h>
#include <string.h>

#include "human_target_angle/pose_mapping.h"
#include "pose_mapping_internal.h"

#ifdef _WIN32
#define BRIDGE_API __declspec(dllexport)
#else
#define BRIDGE_API
#endif

/*
 * side = 0 : Human LEFT
 * side = 1 : Human RIGHT
 *
 * out_xyz[0..2]   = shoulder_l x,y,z
 * out_xyz[3..5]   = shoulder_r x,y,z
 * out_xyz[6..8]   = elbow      x,y,z
 * out_xyz[9..11]  = wrist      x,y,z
 *
 * LEFT / RIGHT는 완전히 독립된 PoseMappingContext를 유지한다.
 *
 * 중요:
 * elbow roll/pitch도 반드시 해당 side context가 보유한
 * stable BodyFrame을 사용해 계산한다.
 * 서로 다른 LEFT/RIGHT reconstruction의 shoulder를 섞지 않는다.
 */

typedef struct {
    float elbow_roll_unwrapped_deg;
    float elbow_pitch_deg;

    float raw_elbow_roll_deg;

    uint8_t angle_valid;
    uint8_t elbow_roll_initialized;
    uint8_t elbow_roll_singular;

    /*
     * 이번 pose3d_update()에서 새 각도가 계산됐는지.
     * 1 = FRESH
     * 0 = 이전값 HOLD 또는 아직 없음
     */
    uint8_t fresh;
} BridgeAngleState;


static PoseMappingContext g_ctx[2];

static BridgeAngleState g_angle[2];

static uint8_t g_initialized[2] = {
    0U,
    0U
};


static float bridge_wrap180(float d)
{
    d = fmodf(d + 180.0f, 360.0f);

    if (d < 0.0f)
        d += 360.0f;

    return d - 180.0f;
}


static void init_context(int side)
{
    memset(
        &g_ctx[side],
        0,
        sizeof(g_ctx[side])
    );

    memset(
        &g_angle[side],
        0,
        sizeof(g_angle[side])
    );

    g_ctx[side].initialized = 1U;

    g_initialized[side] = 1U;
}


/*
 * dev/robot:
 * human_target_angle/forearm_mapping.c
 * fm_calculate_angles()
 *
 * 와 동일한 elbow roll / elbow pitch 정의를 사용한다.
 *
 * BodyFrame:
 *   X = anatomical shoulder L -> R
 *   Y = stabilized body up
 *   Z = X cross Y
 *
 * Forearm:
 *   f = Wrist - Elbow
 *
 * elbow_roll:
 *   atan2(fx, fz)
 *
 * elbow_pitch:
 *   atan2(fy, sqrt(fx^2 + fz^2))
 */
static int calculate_elbow_angles(
    int side,
    float dt_filter_sec
)
{
    PoseMappingContext *ctx;
    BridgeAngleState *state;

    Vec3 forearm;
    Vec3 body_x;
    Vec3 body_y;
    Vec3 body_z;

    float fx;
    float fy;
    float fz;

    float horizontal;

    float yaw;
    float pitch;

    ctx = &g_ctx[side];
    state = &g_angle[side];

    if (pm_get_stable_body_frame(
            ctx,
            &body_x,
            &body_y,
            &body_z) != 0)
        return -1;

    /*
     * 해당 side reconstruction의
     * Elbow -> Wrist vector.
     */
    forearm = pm_vsub(
        ctx->wrist_3d,
        ctx->elbow_3d
    );

    if (pm_vnormalize(&forearm) != 0)
        return -1;

    fx = pm_vdot(
        forearm,
        body_x
    );

    fy = pm_vdot(
        forearm,
        body_y
    );

    fz = pm_vdot(
        forearm,
        body_z
    );

    horizontal = hypotf(
        fx,
        fz
    );

    /*
     * forearm_mapping.c와 같은
     * azimuth singularity hysteresis.
     *
     * ENTER = 0.02
     * LEAVE = 0.04
     */
    state->elbow_roll_singular =
        horizontal <
        (
            state->elbow_roll_singular
            ? 0.04f
            : 0.02f
        );

    pitch =
        atan2f(
            fy,
            horizontal
        ) * PM_RAD_TO_DEG;

    /*
     * Roll은 pole이 아닐 때만 갱신한다.
     */
    if (!state->elbow_roll_singular)
    {
        yaw =
            bridge_wrap180(
                atan2f(
                    fx,
                    fz
                ) * PM_RAD_TO_DEG
            );

        state->raw_elbow_roll_deg =
            yaw;

        if (!state->elbow_roll_initialized)
        {
            state->elbow_roll_unwrapped_deg =
                yaw;
        }
        else
        {
            state->elbow_roll_unwrapped_deg =
                pm_filter_angle_continuous(
                    state->elbow_roll_unwrapped_deg,
                    yaw,
                    PM_JOINT_ANGLE_TAU_SEC,
                    PM_JOINT_DEADBAND_DEG,
                    dt_filter_sec,
                    1U
                );
        }

        state->elbow_roll_initialized =
            1U;
    }

    /*
     * Pitch는 [-90,+90], circular angle 아님.
     */
    if (!state->angle_valid)
    {
        state->elbow_pitch_deg =
            pitch;
    }
    else
    {
        state->elbow_pitch_deg =
            pm_filter_angle_continuous(
                state->elbow_pitch_deg,
                pitch,
                PM_JOINT_ANGLE_TAU_SEC,
                PM_JOINT_DEADBAND_DEG,
                dt_filter_sec,
                0U
            );
    }

    state->angle_valid = 1U;
    state->fresh = 1U;

    return 0;
}


BRIDGE_API void pose3d_reset(void)
{
    init_context(0);
    init_context(1);
}


/*
 * 기존 API 유지.
 *
 * Python의 현재 ctypes 선언과
 * 12-float XYZ buffer를 깨지 않는다.
 */
BRIDGE_API int pose3d_update(
    int side,
    uint32_t frame_id,
    float dt_sec,

    float shoulder_l_x,
    float shoulder_l_y,
    uint8_t shoulder_l_valid,

    float shoulder_r_x,
    float shoulder_r_y,
    uint8_t shoulder_r_valid,

    float elbow_x,
    float elbow_y,
    uint8_t elbow_valid,

    float wrist_x,
    float wrist_y,
    uint8_t wrist_valid,

    float *out_xyz
)
{
    PoseMappingContext *ctx;

    HumanPose2D pose;

    float dt_filter_sec;
    float shoulder_span_px = 0.0f;

    if (
        side < 0 ||
        side > 1 ||
        out_xyz == NULL
    )
        return -1;

    if (!g_initialized[side])
        init_context(side);

    ctx = &g_ctx[side];

    /*
     * 이번 frame에서는 아직 새 angle 없음.
     * reconstruction 성공 후 다시 1로 올라간다.
     */
    g_angle[side].fresh = 0U;

    memset(
        &pose,
        0,
        sizeof(pose)
    );

    pose.frame_id = frame_id;
    pose.valid = 1U;

    pose.shoulder_l.x =
        shoulder_l_x;

    pose.shoulder_l.y =
        shoulder_l_y;

    pose.shoulder_l.valid =
        shoulder_l_valid;


    pose.shoulder_r.x =
        shoulder_r_x;

    pose.shoulder_r.y =
        shoulder_r_y;

    pose.shoulder_r.valid =
        shoulder_r_valid;


    pose.elbow.x =
        elbow_x;

    pose.elbow.y =
        elbow_y;

    pose.elbow.valid =
        elbow_valid;


    pose.wrist.x =
        wrist_x;

    pose.wrist.y =
        wrist_y;

    pose.wrist.valid =
        wrist_valid;


    /*
     * Webcam Mock에는 finger 없음.
     * 없는 정보를 생성하지 않는다.
     */
    pose.finger1.valid = 0U;
    pose.finger2.valid = 0U;


    dt_filter_sec =
        pm_sanitize_filter_dt(
            dt_sec
        );


    /*
     * 실제 Robot C 2D tracking.
     */
    pm_update_all_landmarks(
        ctx,
        &pose,
        dt_filter_sec
    );


    /*
     * Shoulder L/R + 해당 arm Elbow/Wrist가
     * 모두 fresh일 때만 reconstruction.
     */
    if (!pm_major_all_fresh(ctx))
        return 0;


    /*
     * 실제 dev/robot 3D reconstruction.
     */
    if (
        pm_reconstruct_major_pose3d(
            ctx,
            side == 0
                ? POSE_ARM_LEFT
                : POSE_ARM_RIGHT,
            dt_filter_sec,
            &shoulder_span_px
        ) != 0
    )
        return 0;


    /*
     * 각 side의 자기 context 안에서
     * Stable BodyFrame 생성.
     */
    if (
        pm_update_stable_body_frame(
            ctx,
            dt_filter_sec
        ) != 0
    )
        return 0;


    /*
     * 같은 Stable BodyFrame으로
     * elbow roll/pitch 계산.
     */
    if (
        calculate_elbow_angles(
            side,
            dt_filter_sec
        ) != 0
    )
        return 0;


    out_xyz[0] =
        ctx->shoulder_l_3d.x;

    out_xyz[1] =
        ctx->shoulder_l_3d.y;

    out_xyz[2] =
        ctx->shoulder_l_3d.z;


    out_xyz[3] =
        ctx->shoulder_r_3d.x;

    out_xyz[4] =
        ctx->shoulder_r_3d.y;

    out_xyz[5] =
        ctx->shoulder_r_3d.z;


    out_xyz[6] =
        ctx->elbow_3d.x;

    out_xyz[7] =
        ctx->elbow_3d.y;

    out_xyz[8] =
        ctx->elbow_3d.z;


    out_xyz[9] =
        ctx->wrist_3d.x;

    out_xyz[10] =
        ctx->wrist_3d.y;

    out_xyz[11] =
        ctx->wrist_3d.z;


    return 1;
}


/*
 * 새 API.
 *
 * return:
 *   1  = 이번 frame FRESH angle
 *   0  = 이전 정상 angle HOLD
 *  -1  = 아직 정상 angle 자체가 없음
 *
 * out_angles[0] = human elbow_roll_deg
 * out_angles[1] = human elbow_pitch_deg
 *
 * observable:
 *   elbow roll이 현재 pole/singularity가 아니면 1
 */
BRIDGE_API int pose3d_get_elbow_angles(
    int side,
    float *out_angles,
    uint8_t *out_roll_observable
)
{
    BridgeAngleState *state;

    if (
        side < 0 ||
        side > 1 ||
        out_angles == NULL ||
        out_roll_observable == NULL
    )
        return -1;

    state =
        &g_angle[side];

    if (!state->angle_valid)
        return -1;

    out_angles[0] =
        bridge_wrap180(
            state->elbow_roll_unwrapped_deg
        );

    out_angles[1] =
        pm_clampf(
            state->elbow_pitch_deg,
            -90.0f,
            90.0f
        );

    *out_roll_observable =
        state->elbow_roll_singular
        ? 0U
        : 1U;

    return state->fresh
        ? 1
        : 0;
}
