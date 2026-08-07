# PlatformIO Post-Build-Hook: kopiert die fertige firmware.bin zusaetzlich unter
# einem Zeitstempel-Namen  <YYYY-MM-DD-HHMM>.bin  nach firmware/bin/.
# So bleibt zu jedem Build ein benanntes Artefakt fuer OTA-Uploads erhalten.
import datetime
import os
import shutil

Import("env")  # noqa: F821  (von PlatformIO bereitgestellt)


def _copy_named(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    src = os.path.join(build_dir, "firmware.bin")
    if not os.path.exists(src):
        return
    out_dir = os.path.join(env.subst("$PROJECT_DIR"), "bin")
    os.makedirs(out_dir, exist_ok=True)
    ts = datetime.datetime.now().strftime("%Y-%m-%d-%H%M")
    dst = os.path.join(out_dir, ts + ".bin")
    shutil.copyfile(src, dst)
    print("==> Firmware benannt kopiert: %s" % dst)


env.AddPostAction("$BUILD_DIR/firmware.bin", _copy_named)  # noqa: F821
