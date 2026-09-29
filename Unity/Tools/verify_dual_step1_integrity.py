"""Dual scene의 mode 한 항목 외에는 저장된 hierarchy/visual이 불변인지 검사한다."""
from pathlib import Path
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
before = (root / "Validation/DualRobotDemo.before-dual-step1.unity.txt").read_text(encoding="utf-8-sig")
after = (root / "Assets/Scenes/DualRobotDemo.unity").read_text(encoding="utf-8-sig")
anchor = "  rightReceiver: {fileID: 1297670558}\n  transfer: {fileID: 2062820213}\n  mode: "
assert before.count(anchor + "0") == 1
assert before.replace(anchor + "0", anchor + "1") == after, "mode 외 scene 변경"
subprocess.run([sys.executable, str(root / "Tools/verify_dual_integrity.py")], check=True)
blocks = re.split(r"^--- !u!", after, flags=re.M)[1:]
names, transforms = {}, {}
for block in blocks:
    kind, identity = re.match(r"(\d+) &(\d+)", block).groups()
    if kind == "1":
        names[identity] = re.search(r"^  m_Name: (.*)$", block, re.M).group(1)
    if kind == "4":
        go = re.search(r"m_GameObject: \{fileID: (\d+)\}", block).group(1)
        parent = re.search(r"m_Father: \{fileID: (\d+)\}", block).group(1)
        transforms[identity] = (go, parent)
system = next(k for k, (go, _) in transforms.items() if names.get(go) == "DualRobotSystem")
lines = []
def tree(identity, depth=0):
    go, _ = transforms[identity]
    lines.append("  " * depth + names.get(go, go))
    if depth < 2:
        for key, (_, parent) in transforms.items():
            if parent == identity:
                tree(key, depth+1)
tree(system)
report = "# Dual STEP 1 보존 검증\n\n- PASS: mode 0→1 외 Dual scene 전체 text 불변\n- PASS: Main 및 핵심 9개 파일, 외부 작업 파일/HEAD 불변\n\n```text\n" + "\n".join(lines) + "\n```\n"
(root / "Validation/DualStep1").mkdir(exist_ok=True)
(root / "Validation/DualStep1/Integrity.md").write_text(report, encoding="utf-8")
print(report)
