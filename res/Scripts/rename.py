import os
import shutil
import sys


NAME_MAP = {
    "nx": "left",
    "px": "right",
    "ny": "bottom",
    "py": "top",
    "nz": "back",
    "pz": "front",
}


def rename_cubemap_images(folder_path: str) -> None:
    if not os.path.isdir(folder_path):
        raise SystemExit(f"Not a directory: {folder_path}")

    for file_name in os.listdir(folder_path):
        lower_name = file_name.lower()
        for key, new_name in NAME_MAP.items():
            if key not in lower_name:
                continue
            old_path = os.path.join(folder_path, file_name)
            extension = os.path.splitext(file_name)[1]
            new_path = os.path.join(folder_path, f"{new_name}{extension}")
            if os.path.exists(new_path):
                backup_path = os.path.join(folder_path, f"{new_name}_old{extension}")
                shutil.move(new_path, backup_path)
            os.rename(old_path, new_path)
            print(f"{file_name} -> {os.path.basename(new_path)}")
            break


if __name__ == "__main__":
    folder = sys.argv[1] if len(sys.argv) > 1 else input("Cubemap folder: ").strip('"')
    rename_cubemap_images(folder)
