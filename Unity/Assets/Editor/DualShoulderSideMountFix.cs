
#if UNITY_EDITOR
using System;
using UnityEditor;
using UnityEngine;

public static class DualShoulderSideMountFix
{
    [MenuItem("Tools/Human Motion/Dual Robot/Fix Shoulder Side Mounts")]
    public static void FixShoulderSideMounts()
    {
        if (EditorApplication.isPlaying)
        {
            EditorUtility.DisplayDialog(
                "Shoulder Side Mount Fix",
                "Play Mode를 종료한 뒤 실행하세요.",
                "OK");
            return;
        }

        GameObject left = GameObject.Find("RobotArm_L");
        GameObject right = GameObject.Find("RobotArm_R");
        GameObject centerFrame = GameObject.Find("CenterFrame");

        if (left == null || right == null || centerFrame == null)
        {
            EditorUtility.DisplayDialog(
                "Shoulder Side Mount Fix",
                "RobotArm_L / RobotArm_R / CenterFrame 중 하나를 찾지 못했습니다.",
                "OK");
            return;
        }

        Transform leftShoulder = FindRecursive(left.transform, "Shoulder_Pitch");
        Transform rightShoulder = FindRecursive(right.transform, "Shoulder_Pitch");

        if (leftShoulder == null || rightShoulder == null)
        {
            EditorUtility.DisplayDialog(
                "Shoulder Side Mount Fix",
                "Shoulder_Pitch를 찾지 못했습니다. 내부 관절 이름은 수정하지 않습니다.",
                "OK");
            return;
        }

        Renderer beamRenderer = FindHorizontalBeamRenderer(centerFrame.transform);
        if (beamRenderer == null)
        {
            EditorUtility.DisplayDialog(
                "Shoulder Side Mount Fix",
                "CenterFrame 아래에서 HorizontalBeam renderer를 찾지 못했습니다.",
                "OK");
            return;
        }

        Bounds b = beamRenderer.bounds;

        // Beam 좌/우 '옆면 중심'을 mount target으로 사용한다.
        Vector3 leftTarget = new Vector3(b.min.x, b.center.y, b.center.z);
        Vector3 rightTarget = new Vector3(b.max.x, b.center.y, b.center.z);

        Undo.IncrementCurrentGroup();
        int group = Undo.GetCurrentGroup();
        Undo.SetCurrentGroupName("Align Shoulder Pivots To Beam Side Faces");

        Undo.RecordObject(left.transform, "Move RobotArm_L by shoulder");
        Undo.RecordObject(right.transform, "Move RobotArm_R by shoulder");

        // IMPORTANT:
        // Robot root rotation / scale / joint hierarchy는 건드리지 않는다.
        // 오직 root position만 이동해서 Shoulder_Pitch world pivot을 beam side에 맞춘다.
        left.transform.position += leftTarget - leftShoulder.position;
        right.transform.position += rightTarget - rightShoulder.position;

        EditorUtility.SetDirty(left);
        EditorUtility.SetDirty(right);

        Undo.CollapseUndoOperations(group);

        Selection.objects = new UnityEngine.Object[] { left, right };

        EditorUtility.DisplayDialog(
            "Shoulder Side Mount Fix",
            "완료.\n\n" +
            "Shoulder_Pitch 축 중심을 HorizontalBeam 좌/우 옆면 중심에 맞췄습니다.\n" +
            "- Robot root Rotation 변경 없음\n" +
            "- Scale 변경 없음\n" +
            "- 내부 Pivot hierarchy 변경 없음\n" +
            "- Position만 이동\n\n" +
            "원치 않으면 Ctrl+Z 한 번으로 되돌릴 수 있습니다.",
            "OK");
    }

    private static Renderer FindHorizontalBeamRenderer(Transform centerFrame)
    {
        // 이름이 명확하면 우선 사용.
        Transform named = FindRecursive(centerFrame, "HorizontalBeam");
        if (named != null)
        {
            Renderer nr = named.GetComponent<Renderer>();
            if (nr != null) return nr;

            nr = named.GetComponentInChildren<Renderer>(true);
            if (nr != null) return nr;
        }

        // Codex가 다른 이름을 썼더라도 CenterFrame에서 가장 X 방향으로 긴 renderer를 beam으로 선택.
        Renderer[] rs = centerFrame.GetComponentsInChildren<Renderer>(true);
        Renderer best = null;
        float bestScore = -1f;

        foreach (Renderer r in rs)
        {
            Bounds b = r.bounds;
            if (b.size.x <= 0f) continue;

            // 수평으로 길수록 높은 점수.
            float score = b.size.x / Mathf.Max(0.001f, b.size.y + b.size.z);
            if (score > bestScore)
            {
                bestScore = score;
                best = r;
            }
        }

        return best;
    }

    private static Transform FindRecursive(Transform parent, string name)
    {
        if (parent.name == name) return parent;

        foreach (Transform child in parent)
        {
            Transform found = FindRecursive(child, name);
            if (found != null) return found;
        }

        return null;
    }
}
#endif
