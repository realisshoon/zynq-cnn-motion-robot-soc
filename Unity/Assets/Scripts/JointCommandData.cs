using System;
using UnityEngine;

/// <summary>
/// robot_arm/include/common/robot_types.h의 최종 JointCommand와 의미가 같은 입력.
/// C 구조체의 메모리 배치/전송 형식을 선언하는 타입은 아니다.
/// </summary>
[Serializable]
public struct JointCommandData
{
    [Tooltip("Unity/향후 통신 확장용. 원본 JointCommand에는 없는 선택적 식별자.")]
    public uint frame_id;
    public float base_deg;
    public float shoulder_deg;
    public float elbow_deg;
    public float wrist_pitch_deg;
    public float wrist_roll_deg;
    [Range(0f, 1f)] public float gripper_norm;
    [Tooltip("원본 uint8_t valid의 0=false, nonzero=true 의미에 대응.")]
    public bool valid;

    public static JointCommandData Neutral => new JointCommandData
    {
        base_deg = 90f,
        shoulder_deg = 90f,
        elbow_deg = 90f,
        wrist_pitch_deg = 90f,
        wrist_roll_deg = 90f,
        gripper_norm = 1f,
        valid = true
    };
}
