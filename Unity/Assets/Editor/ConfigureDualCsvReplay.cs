
#if UNITY_EDITOR
using System;
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;

public static class ConfigureDualCsvReplay
{
    [MenuItem("Tools/Human Motion/STEP 1/Configure Dual CSV Replay")]
    public static void Configure()
    {
        if (EditorApplication.isPlayingOrWillChangePlaymode)
        {
            EditorUtility.DisplayDialog("Dual CSV Replay", "Play Mode를 종료한 뒤 실행하세요.", "OK");
            return;
        }

        const string scenePath = "Assets/Scenes/DualRobotDemo.unity";
        if (!File.Exists(scenePath))
        {
            EditorUtility.DisplayDialog("Dual CSV Replay", "DualRobotDemo.unity를 찾지 못했습니다.", "OK");
            return;
        }

        EditorSceneManager.OpenScene(scenePath, OpenSceneMode.Single);

        GameObject leftGo = GameObject.Find("RobotArm_L");
        GameObject rightGo = GameObject.Find("RobotArm_R");
        GameObject systemGo = GameObject.Find("DualRobotSystem");

        if (leftGo == null || rightGo == null || systemGo == null)
            throw new InvalidOperationException("RobotArm_L / RobotArm_R / DualRobotSystem required.");

        RobotArmController left = leftGo.GetComponent<RobotArmController>();
        RobotArmController right = rightGo.GetComponent<RobotArmController>();
        if (left == null || right == null)
            throw new InvalidOperationException("RobotArmController missing.");

        ForearmArmController leftForearm = leftGo.GetComponent<ForearmArmController>();
        ForearmArmController rightForearm = rightGo.GetComponent<ForearmArmController>();
        if (leftForearm == null)
            leftForearm = Undo.AddComponent<ForearmArmController>(leftGo);
        if (rightForearm == null)
            rightForearm = Undo.AddComponent<ForearmArmController>(rightGo);

        // Disable the old single-arm UDP listener so port 5005 has one owner.
        foreach (var oldReceiver in UnityEngine.Object.FindObjectsByType<UdpJointCommandReceiver>(
                     FindObjectsInactive.Include, FindObjectsSortMode.None))
        {
            Undo.RecordObject(oldReceiver, "Disable single-arm UDP receiver");
            oldReceiver.enabled = false;
            EditorUtility.SetDirty(oldReceiver);
        }

        // Disable scripted dual controller during CSV replay so it cannot overwrite either arm.
        foreach (var demo in UnityEngine.Object.FindObjectsByType<DualArmDemoController>(
                     FindObjectsInactive.Include, FindObjectsSortMode.None))
        {
            Undo.RecordObject(demo, "Disable scripted dual controller");
            demo.enabled = false;
            EditorUtility.SetDirty(demo);
        }

        foreach (var ui in UnityEngine.Object.FindObjectsByType<DualArmStatusUI>(
                     FindObjectsInactive.Include, FindObjectsSortMode.None))
        {
            Undo.RecordObject(ui, "Disable scripted status UI");
            ui.enabled = false;
            EditorUtility.SetDirty(ui);
        }

        DualUdpJointCommandReceiver receiver =
            systemGo.GetComponent<DualUdpJointCommandReceiver>();

        if (receiver == null)
            receiver = Undo.AddComponent<DualUdpJointCommandReceiver>(systemGo);

        Undo.RecordObject(receiver, "Configure dual UDP receiver");
        receiver.leftArm = leftForearm;
        receiver.rightArm = rightForearm;
        receiver.port = 5005;
        receiver.enabled = true;

        Undo.RecordObject(left, "Disable legacy arm controller");
        Undo.RecordObject(right, "Disable legacy arm controller");
        left.enabled = false;
        right.enabled = false;

        EditorUtility.SetDirty(receiver);
        EditorUtility.SetDirty(left);
        EditorUtility.SetDirty(right);
        EditorUtility.SetDirty(leftForearm);
        EditorUtility.SetDirty(rightForearm);

        EditorSceneManager.SaveScene(SceneManager.GetActiveScene());

        Selection.activeGameObject = systemGo;

        EditorUtility.DisplayDialog(
            "Dual CSV Replay",
            "설정 완료.\n\n" +
            "• RobotArm_L/R에 ForearmArmController 추가\n" +
            "• 기존 6축 RobotArmController OFF\n" +
            "• 기존 single UDP receiver OFF\n" +
            "• scripted controller OFF\n" +
            "• Dual UDP receiver 1개 / port 5005\n" +
            "• Human RIGHT → RobotArm_L\n" +
            "• Human LEFT → RobotArm_R\n\n" +
            "각 팔의 Forearm pivot 계층과 Inspector 참조를 설정한 뒤 Play하세요.",
            "OK");
    }
}
#endif
