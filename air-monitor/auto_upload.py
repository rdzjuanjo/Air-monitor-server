#!/usr/bin/env python3
"""
Script para PlatformIO: Auto-upload de firmware + filesystem  
Hace que 'pio run -t upload' suba automáticamente el filesystem después del firmware
"""

Import("env")
import subprocess
import sys
import time
import os

def upload_filesystem(source, target, env):
    print("\n🔥 AUTO-UPLOAD: Subiendo LittleFS después del firmware...")

    # Pequeña pausa para que el puerto serie se libere tras el upload del firmware
    time.sleep(2)

    # Ruta al ejecutable de PlatformIO (mismo Python que usa el proceso actual)
    pio_exe = os.path.join(os.path.dirname(sys.executable), "platformio")
    if not os.path.isfile(pio_exe):
        # Fallback: buscar en PATH
        pio_exe = "platformio"

    pioenv = env.get("PIOENV", "esp32dev")
    project_dir = env.subst("$PROJECT_DIR")

    result = subprocess.run(
        [pio_exe, "run", "--target", "uploadfs", "--environment", pioenv],
        cwd=project_dir,
    )

    if result.returncode == 0:
        print("✅ Filesystem subido exitosamente!")
    else:
        print(f"❌ Error al subir filesystem (código {result.returncode})")

# Agregar acción después del upload de firmware
env.AddPostAction("upload", upload_filesystem)

print("🚀 Configurado: 'upload' incluirá LittleFS automáticamente")
