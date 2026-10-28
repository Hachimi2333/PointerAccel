#!/usr/bin/env python3
"""Runs the real install/uninstall scripts in a temporary home and fake system directory."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

source=Path(__file__).resolve().parents[1]
build=Path(sys.argv[1]).resolve()
def snapshot(root):
    return {str(p.relative_to(root)): ("link",os.readlink(p)) if p.is_symlink() else ("file",hashlib.sha256(p.read_bytes()).hexdigest())
            for p in root.rglob("*") if p.is_symlink() or p.is_file()}
with tempfile.TemporaryDirectory(prefix="pointeraccel-install-") as temporary:
    home=Path(temporary)/"home";home.mkdir()
    system=Path(temporary)/"system"
    env=os.environ|{"HOME":str(home),"XDG_CONFIG_HOME":str(home/".config"),"XDG_DATA_HOME":str(home/".local/share"),
        "XDG_STATE_HOME":str(home/".local/state"),"XDG_CACHE_HOME":str(home/".cache"),
        "DBUS_SESSION_BUS_ADDRESS":"unix:path=/nonexistent-pointeraccel-test","POINTERACCEL_TEST_NO_BUILD":"1"}
    kwin=home/".config/kwinrc";kwin.parent.mkdir();original=b"[Windows]\nFocusPolicy=ClickToFocus\n\n[Plugins]\notherPluginEnabled=true\n"
    kwin.write_bytes(original)
    def run(arguments,expected=0,extra=None):
        result=subprocess.run([str(a) for a in arguments],env=env|(extra or {}),text=True,capture_output=True,timeout=15)
        assert result.returncode==expected,(arguments,result.returncode,result.stdout,result.stderr)
        return result.stdout
    install=["bash",source/"install.sh","--test-system",system,"--build-dir",build]
    uninstall=["bash",source/"uninstall.sh","--offline","--test-system",system]
    before=snapshot(Path(temporary))
    run(install,1,{"POINTERACCEL_TEST_FAIL_AFTER_MODULE":"1"})
    assert snapshot(Path(temporary))==before,"failed first install did not roll back"
    run(install)
    cli=home/".local/bin/pointeraccel"
    run([cli,"config","set","pointer-speed","14/20"])
    run([cli,"config","set","enhance-pointer-precision","off"])
    assert json.loads(run([cli,"config","get","--json"]))["pointer_speed"]==14
    run([cli,"config","set","pointer-speed","21/20"],2)
    installed=snapshot(Path(temporary))
    run(install,1,{"POINTERACCEL_TEST_FAIL_AFTER_MODULE":"1"})
    assert snapshot(Path(temporary))==installed,"failed upgrade did not restore previous installation"
    run(install)
    assert json.loads(run([cli,"config","get","--json"]))["pointer_speed"]==14
    run(uninstall)
    assert kwin.read_bytes()==original,"kwinrc was not restored byte-for-byte"
    assert snapshot(Path(temporary))==before,"uninstall left owned files"
    run(uninstall)
    # Preserve new, unrelated KWin changes and a configuration that predates installation.
    config=home/".config/pointeraccel/config.json";config.parent.mkdir()
    initial={"enabled":False,"pointer_speed":7,"enhance_pointer_precision":True,"excluded_devices":[],"trace":False}
    config.write_text(json.dumps(initial));original_config=config.read_bytes()
    run(install)
    kwin.write_bytes(kwin.read_bytes()+b"\n[UserChange]\nKeepThis=true\n")
    run(uninstall)
    assert b"KeepThis=true" in kwin.read_bytes() and b"pointeraccel_kwinEnabled" not in kwin.read_bytes()
    assert config.read_bytes()==original_config
    # A changed installed file must be detected before any uninstall mutation.
    run(install)
    document=home/".local/share/pointeraccel/README.md";document.write_text("changed by user")
    conflicted=snapshot(Path(temporary))
    run(uninstall,1)
    assert snapshot(Path(temporary))==conflicted
    print("installer: first install, repeated install, failure rollback, upgrade rollback, configuration preservation, offline uninstall, conflict protection: PASS")
