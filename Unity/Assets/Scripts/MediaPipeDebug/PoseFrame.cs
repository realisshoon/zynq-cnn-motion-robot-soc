using System;
using UnityEngine;

namespace HumanMotion.MediaPipeDebug
{
    [Serializable]
    public struct PoseLandmark
    {
        public Vector3 position;
        public float visibility, presence;
        public bool HasPosition => Finite(position.x) && Finite(position.y) && Finite(position.z);
        public static bool Finite(float x) => !float.IsNaN(x) && !float.IsInfinity(x);
    }

    [Serializable]
    public sealed class PoseFrame
    {
        public long frameId;
        public double timeSec, sourceFps;
        public bool valid;
        // 순서: 해부학적 Left S/E/W, Right S/E/W. 화면 좌우로 재정렬하지 않는다.
        public PoseLandmark[] landmarks = new PoseLandmark[6];
    }

    public interface IPoseSource
    {
        PoseFrame Current { get; }
        event Action<PoseFrame> FrameChanged;
    }

    [Serializable]
    public sealed class MediaPipeCoordinateMapping
    {
        public enum Axis { X, Y, Z }
        public Axis unityX = Axis.X, unityY = Axis.Y, unityZ = Axis.Z;
        public float xSign = 1f, ySign = -1f, zSign = -1f;
        [Min(0.0001f)] public float scale = 1f;
        public Vector3 translation = Vector3.zero;

        public Vector3 MediaPipeToUnity(Vector3 p) => translation + scale * new Vector3(
            p[(int)unityX] * xSign, p[(int)unityY] * ySign, p[(int)unityZ] * zSign);
    }
}
