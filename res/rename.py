import os
import shutil

# 映射关系：原名关键字 -> 新名字
name_map = {
    "nx": "left",
    "px": "right",
    "ny": "bottom",
    "py": "top",
    "nz": "back",
    "pz": "front",
}

def rename_cubemap_images(folder_path):
    if not os.path.isdir(folder_path):
        print(f"❌ {folder_path} 不是一个有效的文件夹")
        return

    for file_name in os.listdir(folder_path):
        lower_name = file_name.lower()
        for key, new_name in name_map.items():
            if key in lower_name:  # 匹配 nx, ny, px, py, nz, pz
                old_path = os.path.join(folder_path, file_name)
                ext = os.path.splitext(file_name)[1]  # 保留原始后缀
                new_path = os.path.join(folder_path, f"{new_name}{ext}")

                # 如果存在同名文件，避免覆盖，先备份
                if os.path.exists(new_path):
                    backup_path = new_path.replace(ext, f"_old{ext}")
                    shutil.move(new_path, backup_path)
                    print(f"⚠️ 已存在 {new_path}，备份到 {backup_path}")

                os.rename(old_path, new_path)
                print(f"✅ {file_name} → {os.path.basename(new_path)}")
                break

if __name__ == "__main__":
    folder = input("请输入文件夹路径：").strip('"')
    rename_cubemap_images(folder)
