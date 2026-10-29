#!/usr/bin/env python3
"""Runs the real install/uninstall scripts in a temporary home and fake system directory."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import signal
import time

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
        "DBUS_SESSION_BUS_ADDRESS":"unix:path=/nonexistent-pointeraccel-test","POINTERACCEL_TEST_NO_BUILD":"1",
        "POINTERACCEL_ISOLATED_TEST":"1","POINTERACCEL_TEST_SYSTEM_ROOT":str(system)}
    kwin=home/".config/kwinrc";kwin.parent.mkdir();original=b"[Windows]\nFocusPolicy=ClickToFocus\n\n[Plugins]\notherPluginEnabled=true\n"
    kwin.write_bytes(original)
    def run(arguments,expected=0,extra=None):
        result=subprocess.run([str(a) for a in arguments],env=env|(extra or {}),text=True,capture_output=True,timeout=15)
        assert result.returncode==expected,(arguments,result.returncode,result.stdout,result.stderr)
        return result.stdout
    install=["bash",source/"install.sh","--test-system",system,"--build-dir",build]
    uninstall=["bash",source/"uninstall.sh","--offline","--test-system",system]
    before=snapshot(Path(temporary))
    def interrupt_and_recover(expected_snapshot):
        paused=Path(temporary)/"paused"
        process=subprocess.Popen([str(a) for a in install],env=env|{"POINTERACCEL_TEST_PAUSE_AFTER_MODULE":str(paused)},
                                 stdout=subprocess.PIPE,stderr=subprocess.PIPE,start_new_session=True)
        try:
            deadline=time.monotonic()+10
            while not paused.exists() and time.monotonic()<deadline:
                assert process.poll() is None,"installer exited before interruption point"
                time.sleep(.05)
            assert paused.exists(),"installer did not reach interruption point"
        finally:
            os.killpg(process.pid,signal.SIGKILL);process.communicate(timeout=5)
        paused.unlink()
        assert (system/"records/pending").exists()
        run(install,1)
        run(install+["--recover"])
        assert snapshot(Path(temporary))==expected_snapshot,"interrupted install recovery lost original files"
    interrupt_and_recover(before)
    paused=Path(temporary)/"committed-pause"
    process=subprocess.Popen([str(a) for a in install],env=env|{"POINTERACCEL_TEST_PAUSE_AFTER_COMMIT":str(paused)},
                             stdout=subprocess.PIPE,stderr=subprocess.PIPE,start_new_session=True)
    try:
        deadline=time.monotonic()+10
        while not paused.exists() and time.monotonic()<deadline:
            assert process.poll() is None
            time.sleep(.05)
        assert paused.exists()
    finally:
        os.killpg(process.pid,signal.SIGKILL);process.communicate(timeout=5)
    paused.unlink()
    assert not (system/"records/pending").exists()
    run(install+["--recover"])
    assert (system/"plugins/pointeraccel_kwin.so").exists(),"recovery removed a committed module"
    run(uninstall)
    assert snapshot(Path(temporary))==before
    run(install,1,{"POINTERACCEL_TEST_FAIL_AFTER_MODULE":"1"})
    assert snapshot(Path(temporary))==before,"failed first install did not roll back"
    run(install)
    cli=home/".local/bin/pointeraccel"
    run([cli,"config","set","pointer-speed","14/20"])
    run([cli,"config","set","enhance-pointer-precision","off"])
    assert json.loads(run([cli,"config","get","--json"]))["pointer_speed"]==14
    run([cli,"config","set","pointer-speed","21/20"],2)
    installed=snapshot(Path(temporary))
    interrupt_and_recover(installed)
    run(install,1,{"POINTERACCEL_TEST_FAIL_AFTER_MODULE":"1"})
    assert snapshot(Path(temporary))==installed,"failed upgrade did not restore previous installation"
    run(install,1,{"POINTERACCEL_TEST_FAIL_BEFORE_COMMIT":"1"})
    assert snapshot(Path(temporary))==installed,"late upgrade failure did not restore both manifests and services"
    run(install)
    assert json.loads(run([cli,"config","get","--json"]))["pointer_speed"]==14
    # Recreate the legacy layout (direct KWin autostart, two-column ownership,
    # no compatibility record/service/snapshot) and migrate without replacing
    # the original backup or the user's settings.
    state=home/".local/state/pointeraccel";data=home/".local/share/pointeraccel"
    backup=(state/"backups/kwin.json").read_bytes()
    removed=[data/"source.tar.gz",data/"scripts/maintenance.py",state/"session.json",
             home/".config/systemd/user/pointeraccel-session.service",
             home/".config/systemd/user/plasma-workspace.target.wants/pointeraccel-session.service"]
    manifest=state/"manifest.tsv"
    manifest.write_text(''.join(line+'\n' for line in manifest.read_text().splitlines() if line.split('\t')[-1] not in {str(path) for path in removed}))
    for path in removed:path.unlink()
    (system/"records/compatibility.json").unlink()
    owner=system/"records/owner.tsv";owner.write_text('\t'.join(owner.read_text().strip().split('\t')[:2])+'\n')
    digest=run([cli,"internal-plugin","enable"]).strip();(state/"autostart.sha256").write_text(digest+'\n')
    run(install)
    assert (state/"backups/kwin.json").read_bytes()==backup
    assert b"pointeraccel_kwinEnabled=false" in kwin.read_bytes()
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
