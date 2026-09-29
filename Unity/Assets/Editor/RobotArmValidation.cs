using System;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

/// <summary>독립 preview Scene에서 실제 Unity Transform을 검사한다. 사용자 Scene의 자세는 변경하지 않는다.</summary>
public static class RobotArmValidation
{
    [MenuItem("Tools/Human Motion/Validate Robot Arm")]
    public static void Run()
    {
        var lines = new List<string>
        {
            "# Robot Arm 검증 결과", "",
            "실행 UTC: " + DateTime.UtcNow.ToString("O"),
            "Unity: " + Application.unityVersion,
            "기준 소스: dev/robot @ f7d8c0495383642ef0401571fe26e289d9765cf6", "",
            "독립 preview Scene에서 Builder 및 실제 Transform을 실행한 결과입니다. 하드웨어 검증은 아닙니다.", ""
        };
        var scene = EditorSceneManager.NewPreviewScene();
        try
        {
            var c = RobotArmBuilder.BuildInScene(scene, false);
            var joints = new[] { c.baseYaw, c.shoulderPitch, c.elbowPitch, c.wristPitch, c.wristRoll };
            string[] paths = { "Base_Yaw", "Base_Yaw/Shoulder_Pitch", "Base_Yaw/Shoulder_Pitch/Elbow_Pitch",
                "Base_Yaw/Shoulder_Pitch/Elbow_Pitch/Wrist_Pitch", "Base_Yaw/Shoulder_Pitch/Elbow_Pitch/Wrist_Pitch/Wrist_Roll" };
            for (int i = 0; i < joints.Length; i++)
            {
                Require(c.transform.Find(paths[i]) == joints[i].pivot, "Pivot reference/path " + paths[i]);
                Require(joints[i].pivot.GetComponent<Renderer>() == null, "Pivot에는 mesh가 없어야 함");
                Require(joints[i].pivot.localScale == Vector3.one, "Pivot scale=1");
            }
            Require(c.GetComponentsInChildren<Transform>(true).Length == 15, "정확한 Hierarchy 노드 수");
            Require(c.fingerLeft.parent == c.fingerRight.parent && c.fingerLeft.parent.parent == c.wristRoll.pivot, "Gripper 부모 연결");
            Pass(lines, "Hierarchy / 자동 reference");

            var command = JointCommandData.Neutral;
            Require(c.ApplyCommand(command), "neutral 적용");
            foreach (var joint in joints) Rotation(joint.pivot.localRotation, Quaternion.identity, "Neutral 0 deg");
            Vector3 neutralTip = c.fingerLeft.position;
            Pass(lines, "Neutral");

            command.base_deg = 120f;
            c.ApplyCommand(command);
            Rotation(c.baseYaw.pivot.localRotation, Quaternion.Euler(0, 30, 0), "Base +30");
            for (int i = 1; i < joints.Length; i++) Rotation(joints[i].pivot.localRotation, Quaternion.identity, "하위 local 불변");
            Vector3 relative = neutralTip - c.baseYaw.pivot.position;
            Vector3 expected = new Vector3(relative.x * Mathf.Cos(Mathf.PI / 6) + relative.z * 0.5f,
                relative.y, -relative.x * 0.5f + relative.z * Mathf.Cos(Mathf.PI / 6));
            Position(c.fingerLeft.position, c.baseYaw.pivot.position + expected, "Base의 하위 전체 world 회전");
            Pass(lines, "Base (Test A)");

            c.inputMode = RobotArmController.InputMode.UDP;
            c.testCommand = JointCommandData.Neutral;
            Require(!c.ApplyTestCommand(), "UDP Mode에서 Manual 적용 금지");
            Rotation(c.baseYaw.pivot.localRotation, Quaternion.Euler(0, 30, 0), "Manual이 UDP 자세를 덮어쓰지 않음");
            c.inputMode = RobotArmController.InputMode.Manual;
            Require(c.ApplyTestCommand(), "Manual Mode 복귀");
            Rotation(c.baseYaw.pivot.localRotation, Quaternion.identity, "Manual Neutral 적용");
            Pass(lines, "Manual / UDP 입력 분리");

            command = JointCommandData.Neutral;
            command.shoulder_deg = 120f;
            command.elbow_deg = 70f;
            c.ApplyCommand(command);
            Rotation(c.shoulderPitch.pivot.localRotation, Quaternion.Euler(30, 0, 0), "Shoulder +30");
            Rotation(c.elbowPitch.pivot.localRotation, Quaternion.Euler(-20, 0, 0), "Elbow -20");
            Rotation(c.elbowPitch.pivot.rotation, Quaternion.Euler(10, 0, 0), "Elbow world +10");
            Position(c.elbowPitch.pivot.position, new Vector3(0, 0.7f + 1.1f * Mathf.Cos(Mathf.PI / 6), 0.55f), "Elbow world 위치");
            Pass(lines, "Shoulder/Elbow (Test B)");

            command = JointCommandData.Neutral;
            command.wrist_pitch_deg = 110;
            command.wrist_roll_deg = 130;
            c.ApplyCommand(command);
            Rotation(c.wristPitch.pivot.localRotation, Quaternion.Euler(20, 0, 0), "Wrist Pitch +20");
            Rotation(c.wristRoll.pivot.localRotation, Quaternion.Euler(0, 40, 0), "Wrist Roll +40");
            Rotation(c.wristRoll.pivot.rotation, Quaternion.Euler(20, 0, 0) * Quaternion.Euler(0, 40, 0), "Wrist 조합");
            Rotation(c.elbowPitch.pivot.localRotation, Quaternion.identity, "Wrist의 상위 관절 불변");
            Pass(lines, "Wrist (Test C)");

            command = JointCommandData.Neutral;
            command.gripper_norm = 0;
            c.ApplyCommand(command);
            Position(c.fingerLeft.localPosition, new Vector3(-0.04f, 0.15f, 0), "CLOSE left");
            Position(c.fingerRight.localPosition, new Vector3(0.04f, 0.15f, 0), "CLOSE right");
            command.gripper_norm = 1;
            c.ApplyCommand(command);
            Position(c.fingerLeft.localPosition, new Vector3(-0.2f, 0.15f, 0), "OPEN left");
            Position(c.fingerRight.localPosition, new Vector3(0.2f, 0.15f, 0), "OPEN right");
            command.gripper_norm = 0.5f;
            c.ApplyCommand(command);
            Position(c.fingerLeft.localPosition, new Vector3(-0.12f, 0.15f, 0), "중간 opening");
            Pass(lines, "Gripper (Test D)");

            command.base_deg = 120;
            c.ApplyCommand(command);
            var transforms = c.GetComponentsInChildren<Transform>(true);
            var positions = Array.ConvertAll(transforms, t => t.localPosition);
            var rotations = Array.ConvertAll(transforms, t => t.localRotation);
            command = JointCommandData.Neutral;
            command.valid = false;
            c.testCommand = command;
            Require(!c.ApplyTestCommand(), "invalid 거부");
            for (int i = 0; i < transforms.Length; i++)
            {
                Position(transforms[i].localPosition, positions[i], "invalid 위치 유지");
                Rotation(transforms[i].localRotation, rotations[i], "invalid 회전 유지");
            }
            command.valid = true;
            command.elbow_deg = float.NaN;
            Require(!c.ApplyCommand(command), "NaN Transform 쓰기 방지");
            Rotation(c.baseYaw.pivot.localRotation, Quaternion.Euler(0, 30, 0), "부분 적용 없음");
            Pass(lines, "valid=false HOLD / 비유한 수 거부");

            c.baseYaw.neutral_deg = 100;
            c.baseYaw.axis = RobotArmController.VisualAxis.Z;
            c.baseYaw.direction = RobotArmController.VisualDirection.Negative;
            command = JointCommandData.Neutral;
            command.base_deg = 120;
            c.ApplyCommand(command);
            c.ApplyCommand(command);
            Rotation(c.baseYaw.pivot.localRotation, Quaternion.Euler(0, 0, -20), "축/방향/중립 조정, 누적 회전 없음");
            c.gameObject.SetActive(false);
            Require(RobotArmBuilder.BuildInScene(scene, false) == c, "비활성 RobotArm 중복 방지");
            Require(c.baseYaw.neutral_deg == 100, "재실행 설정 보존");
            Require(scene.rootCount == 1, "중복 root 없음");
            Pass(lines, "Inspector 설정 / 재실행 / 비활성 중복 방지");

            TestPartialAndUndo(lines);
            Pass(lines, "Compile (Unity Editor에서 컴파일된 코드 실행)");
            lines.Add("\n전체 결과: PASS");
            Debug.Log("RobotArm validation: ALL PASS. Validation/RobotArmValidation.md 참조.");
        }
        catch (Exception exception)
        {
            lines.Add("\n전체 결과: FAIL — " + exception.Message);
            Debug.LogException(exception);
        }
        finally
        {
            EditorSceneManager.ClosePreviewScene(scene);
            string folder = Path.GetFullPath(Path.Combine(Application.dataPath, "../Validation"));
            Directory.CreateDirectory(folder);
            lines.Add("\n## 실행한 소스 SHA256\n");
            foreach (string file in new[] { "Scripts/JointCommandData.cs", "Scripts/RobotArmController.cs", "Editor/RobotArmBuilder.cs", "Editor/RobotArmValidation.cs" })
            {
                using (var sha = SHA256.Create())
                    lines.Add("- `" + file + "`: `" + BitConverter.ToString(sha.ComputeHash(File.ReadAllBytes(Path.Combine(Application.dataPath, file)))).Replace("-", "") + "`");
            }
            File.WriteAllLines(Path.Combine(folder, "RobotArmValidation.md"), lines, new UTF8Encoding(false));
        }
    }

    [MenuItem("Tools/Human Motion/Validate Robot Arm", true)]
    private static bool CanValidate() => !EditorApplication.isPlayingOrWillChangePlaymode && !EditorApplication.isCompiling;

    private static void TestPartialAndUndo(List<string> lines)
    {
        var scene = EditorSceneManager.NewPreviewScene();
        Undo.IncrementCurrentGroup();
        int group = Undo.GetCurrentGroup();
        try
        {
            var root = new GameObject("RobotArm");
            UnityEngine.SceneManagement.SceneManager.MoveGameObjectToScene(root, scene);
            var pivot = new GameObject("Base_Yaw").transform;
            pivot.SetParent(root.transform, false);
            pivot.localPosition = new Vector3(0, 0.75f, 0);
            var c = RobotArmBuilder.BuildInScene(scene, true);
            Require(c.baseYaw.pivot == pivot, "기존 Pivot 재사용");
            Position(pivot.localPosition, new Vector3(0, 0.75f, 0), "기존 Pivot 위치 보존");
            Undo.FlushUndoRecordObjects();
            Undo.RevertAllDownToGroup(group);
            Require(root != null && pivot != null && root.transform.childCount == 1, "Undo 기존 구조 보존");
            Require(root.GetComponent<RobotArmController>() == null, "Undo 추가 Component 제거");
            Pass(lines, "부분 Hierarchy 확장 / Undo 복원");
        }
        finally { EditorSceneManager.ClosePreviewScene(scene); }
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
    private static void Rotation(Quaternion actual, Quaternion expected, string message) =>
        Require(Quaternion.Angle(actual, expected) < 0.05f, message);
    private static void Position(Vector3 actual, Vector3 expected, string message) =>
        Require(Vector3.Distance(actual, expected) < 0.0001f, message);
    private static void Pass(List<string> lines, string test) => lines.Add("- " + test + ": PASS");
}
