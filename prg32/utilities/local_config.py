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
