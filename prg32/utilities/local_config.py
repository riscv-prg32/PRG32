import os
from prg32.utilities.env_variables import ROOT_DIR

LOCAL_CONFIG_FILE = ROOT_DIR / "profiles" / "sdkconfig.defaults.local"

def set_local_config(updates: dict):
    lines = []
    if LOCAL_CONFIG_FILE.exists():
        with open(LOCAL_CONFIG_FILE, "r") as f:
            lines = f.readlines()
    
    new_lines = []
    for line in lines:
        line = line.strip()
        if not line or line.startswith("#"):
            new_lines.append(line)
            continue
        
        key = line.split("=")[0]
        if key in updates:
            val = updates.pop(key)
            if val is not None:
                new_lines.append(f"{key}={val}")
        else:
            new_lines.append(line)
            
    for key, val in updates.items():
        if val is not None:
            new_lines.append(f"{key}={val}")
            
    with open(LOCAL_CONFIG_FILE, "w") as f:
        for line in new_lines:
            if line:
                f.write(line + "\n")

def remove_local_config(keys: list):
    if not LOCAL_CONFIG_FILE.exists():
        return
        
    with open(LOCAL_CONFIG_FILE, "r") as f:
        lines = f.readlines()
        
    new_lines = []
    for line in lines:
        line = line.strip()
        if not line or line.startswith("#"):
            new_lines.append(line)
            continue
            
        key = line.split("=")[0]
        if key not in keys:
            new_lines.append(line)
            
    with open(LOCAL_CONFIG_FILE, "w") as f:
        for line in new_lines:
            if line:
                f.write(line + "\n")

def erase_sdkconfigs(target: str) -> None:
    """
    Erases ./sdkconfig and target-specific sdkconfig (build-esp32c6/sdkconfig or build-qemu/sdkconfig)
    to ensure profiles/sdkconfig.defaults.local is always applied correctly.
    """
    for fname in ["sdkconfig", "sdkconfig.old"]:
        (ROOT_DIR / fname).unlink(missing_ok=True)
        if target == "esp32c6":
            (ROOT_DIR / "build-esp32c6" / fname).unlink(missing_ok=True)
        elif target == "qemu":
            (ROOT_DIR / "build-qemu" / fname).unlink(missing_ok=True)
        elif target == "all":
            (ROOT_DIR / "build-esp32c6" / fname).unlink(missing_ok=True)
            (ROOT_DIR / "build-qemu" / fname).unlink(missing_ok=True)

def apply_local_config_to_sdkconfig(target: str) -> None:
    """
    Appends profiles/sdkconfig.defaults.local to the target sdkconfig
    so that local configuration options always prevail over sdkconfig defaults.
    """
    if not LOCAL_CONFIG_FILE.is_file():
        return

    content = LOCAL_CONFIG_FILE.read_text(encoding="utf-8")
    if not content.strip():
        return

    target_files = []
    if target in ["esp32c6", "all"]:
        target_files.append(ROOT_DIR / "build-esp32c6" / "sdkconfig")
    if target in ["qemu", "all"]:
        target_files.append(ROOT_DIR / "build-qemu" / "sdkconfig")

    for tf in target_files:
        if tf.is_file():
            existing = tf.read_text(encoding="utf-8")
            if content.strip() in existing:
                continue
            with open(tf, "a", encoding="utf-8") as f:
                f.write(f"\n# Local overrides from profiles/sdkconfig.defaults.local\n{content}\n")
