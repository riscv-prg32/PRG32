$ErrorActionPreference = "Stop"

idf.py -B build-qemu -D SDKCONFIG=build-qemu/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.qemu set-target esp32c3
Write-Host "QEMU input: focus this terminal; arrows or W/A/S/D = joystick 1, Enter/Space = SELECT, J/Z = A, K/X = B"
idf.py -B build-qemu -D SDKCONFIG=build-qemu/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.qemu qemu --graphics monitor
