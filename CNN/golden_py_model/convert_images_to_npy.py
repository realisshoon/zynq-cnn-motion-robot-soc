from pathlib import Path
from PIL import Image
import numpy as np

src_dir = Path("images")
out_dir = Path("images_npy")
out_dir.mkdir(exist_ok=True)

for img_path in src_dir.glob("*.*"):
    if img_path.suffix.lower() not in (".jpg", ".jpeg", ".png", ".bmp"):
        continue
    img = Image.open(img_path).convert("RGB")

    # 카메라 원본 크기(1280x720)가 아니면 맞춰서 리사이즈
    if img.size != (1280, 720):
        img = img.resize((1280, 720))

    arr = np.array(img, dtype=np.uint8)
    out_path = out_dir / (img_path.stem + ".npy")
    np.save(out_path, arr)
    print(f"{img_path.name} -> {out_path.name}")
