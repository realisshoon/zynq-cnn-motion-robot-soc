"""Dual 개발 시작 시점과 보존 대상 SHA256 / 외부 저장소 전체 파일을 비교한다."""
import hashlib
import json
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
repo = root.parents[1]/"zynq-cnn-motion-robot-soc"
baseline = json.loads((root/"Validation/dual_integrity_baseline.json").read_text())
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
changed = [name for name,sha in baseline["unity"].items() if digest(root/name)!=sha]
current = {p.relative_to(repo).as_posix():digest(p) for p in repo.rglob("*") if p.is_file() and ".git" not in p.relative_to(repo).parts}
external_changed = sorted(k for k in baseline["repo"].keys() | current.keys() if baseline["repo"].get(k)!=current.get(k))
head = subprocess.check_output(["git","-C",str(repo),"rev-parse","HEAD"],text=True).strip()
assert not changed, "보존 대상 변경: "+str(changed)
assert not external_changed and head==baseline["head"], "경고: 외부 Git 저장소 변경: "+str(external_changed)
text = ("# 보존 검증\n\n"
        f"- PASS: 저장된 G51 Main 및 핵심 스크립트 {len(baseline['unity'])}개 SHA256 불변\n"
        f"- PASS: 외부 저장소 작업 파일 {len(current)}개 SHA256 / 파일 목록 불변\n"
        f"- PASS: 외부 HEAD 불변 `{head}`\n"
        "- Main 기준은 미저장 G51 작업을 보존 저장한 시점입니다. Dual 구현은 이후 Main을 수정하지 않았습니다.\n")
(root/"Validation/DualIntegrityValidation.md").write_text(text,encoding="utf-8")
print(text)
