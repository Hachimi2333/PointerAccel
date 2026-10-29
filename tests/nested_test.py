#!/usr/bin/env python3
"""Isolated KWin integration test; never connects to the desktop's D-Bus session."""
import ast
import json
import os
import signal
import selectors
from pathlib import Path
import subprocess
import sys
import tempfile
import time

def inside(build):
    with tempfile.TemporaryDirectory(prefix="pointeraccel-test-") as temporary:
        env=os.environ.copy()
        for variable,folder in [("XDG_CONFIG_HOME","config"),("XDG_DATA_HOME","data"),("XDG_STATE_HOME","state"),("XDG_CACHE_HOME","cache")]:
            env[variable]=str(Path(temporary)/folder)
        env["POINTERACCEL_ISOLATED_TEST"]="1"
        env["QT_PLUGIN_PATH"]=str(build/"teststage/qt6/plugins")
        initial_kwin=Path(env["XDG_CONFIG_HOME"])/"kwinrc"
        initial_kwin.parent.mkdir(parents=True,exist_ok=True)
        initial_kwin.write_text("[Plugins]\npointeraccel_kwinEnabled=true\n")
        log=open(Path(temporary)/"kwin.log","w+")
        socket=f"pointeraccel-test-{os.getpid()}"
        compositor=subprocess.Popen(["/usr/bin/kwin_wayland","--virtual","--socket",socket,
            "--width","1280","--height","800","--scale","1.25","--no-lockscreen","--no-global-shortcuts",
            "--no-kactivities","--inputmethod","/bin/true","--xwayland"],env=env,stdout=log,stderr=log)
        def call(path,method,*args):
            result=subprocess.run(["gdbus","call","--session","--dest","org.kde.KWin","--object-path",path,"--method",method,*args],
                                  capture_output=True,text=True,timeout=10)
            if result.returncode: raise RuntimeError(result.stderr.strip())
            result=result.stdout.strip()
            if result=="()":return None
            if result in ("(true,)","(false,)"): return result=="(true,)"
            value=ast.literal_eval(result)[0]
            return json.loads(value) if isinstance(value,str) and value.startswith("{") else value
        def rpc(name,*args): return call("/org/kde/KWin/PointerAccel","org.kde.KWin.PointerAccel1."+name,*args)
        try:
            for _ in range(100):
                try:
                    call("/KWin","org.kde.KWin.activeOutputName");break
                except RuntimeError:
                    if compositor.poll() is not None: raise RuntimeError("Nested KWin exited")
                    time.sleep(.1)
            else: raise RuntimeError("Nested KWin startup timed out")
            env["WAYLAND_DISPLAY"]=socket
            with tempfile.TemporaryFile(mode="w+") as scale_log:
                scale=subprocess.run(["kscreen-doctor","output.Virtual-0.scale.1.25"],env=env,stdout=scale_log,stderr=scale_log,timeout=10)
                scale_log.seek(0);scale_text=scale_log.read()
            assert scale.returncode==0,scale_text
            time.sleep(.1)
            # Exercise startup loading through the normal KWin plugin manager.
            assert rpc("Status")["loaded"]
            time.sleep(.1)
            assert rpc("Status")["active"],rpc("Status")
            base=time.monotonic_ns()//1000
            reports=[{"device":i%2,"x":x,"y":y,"dx":x*3.125,"dy":y*3.125,"time":base+i*125}
                     for i,(x,y) in enumerate([(1,1),(2,-1),(12,5),(-4,0),(-1,-1),(0,0),(1,-2)])]
            reports.insert(3,{"device":2,"x":.25,"y":-.5,"dx":.75,"dy":-1.25,"time":base+300})
            observed=rpc("TestReports",json.dumps({"reports":reports}))
            assert observed["observed"]==observed["expected"],observed
            assert rpc("Diagnostics")["processed"]==len(reports)-1
            assert rpc("Diagnostics")["last"]["dpi"]==120,rpc("Diagnostics")
            assert rpc("TestHotplug")["active"]
            replugged=rpc("TestReports",json.dumps({"reports":reports[:2]}))
            assert replugged["observed"]==replugged["expected"]
            assert not rpc("SetTemporaryEnabled","false")["active"]
            native=rpc("TestReports",json.dumps({"reports":reports[:2]}))
            assert len(native["observed"])==1,native
            assert native["observed"][0]["raw_x"]==sum(r["x"] for r in reports[:2])
            assert rpc("SetTemporaryEnabled","true")["active"]
            corrupt=rpc("TestReports",json.dumps({"reports":reports[:2],"corrupt":True}))
            assert not corrupt["status"]["active"] and not corrupt["status"]["healthy"],corrupt
            assert len(corrupt["observed"])==1,corrupt
            shutdown=rpc("Shutdown")
            assert shutdown["healthy"] and not shutdown["active"],shutdown
            assert rpc("ApplyConfig")["active"]
            protocol_reports=[r for r in reports if r["device"]!=2 and (r["x"] or r["y"])]
            client_env=env|{k:v for k,v in rpc("TestEnvironment").items() if v}
            client_env["WAYLAND_DISPLAY"]=socket
            for client in ["pointeraccel-raw-wayland","pointeraccel-raw-x11"]:
                probe=subprocess.Popen([str(build/client)],env=client_env,stdout=subprocess.PIPE,stderr=log,bufsize=0)
                selector=selectors.DefaultSelector();selector.register(probe.stdout,selectors.EVENT_READ)
                try:
                    assert selector.select(8),f"{client} did not start"
                    assert probe.stdout.readline().strip()==b"READY",f"{client} startup failed"
                    time.sleep(.2)
                    rpc("TestReports",json.dumps({"reports":protocol_reports}))
                    samples=[];deadline=time.monotonic()+3
                    # XWayland may batch events; the raw sum must remain unchanged.
                    while time.monotonic()<deadline:
                        if not selector.select(.1):
                            if samples:break
                            continue
                        line=probe.stdout.readline()
                        if not line:break
                        samples.append(json.loads(line))
                    for axis in ["x","y"]:
                        assert sum(s["raw_"+axis] for s in samples)==sum(r[axis] for r in protocol_reports),(client,samples)
                    samples=[s for s in samples if s["raw_x"] or s["raw_y"]]  # The explicit test warp sends a zero marker.
                    assert [(s["raw_x"],s["raw_y"]) for s in samples]==[(r["x"],r["y"]) for r in protocol_reports],(client,samples)
                finally:
                    probe.terminate()
                    try:probe.wait(timeout=3)
                    except subprocess.TimeoutExpired:probe.kill();probe.wait()
                    selector.close();probe.stdout.close()
            config_path=Path(env["XDG_CONFIG_HOME"])/"pointeraccel/config.json"
            config_path.parent.mkdir(parents=True,exist_ok=True)
            configuration={"enabled":True,"pointer_speed":14,"enhance_pointer_precision":False,"excluded_devices":[],"trace":True}
            config_path.write_text(json.dumps(configuration));assert rpc("ApplyConfig")["active"]
            many=[{"device":i%2,"x":1,"y":-1,"dx":3.125,"dy":-3.125,"time":base+10000+i*125} for i in range(5000)]
            for offset in range(0,len(many),500):
                traced=rpc("TestReports",json.dumps({"reports":many[offset:offset+500]}))
                assert traced["observed"]==traced["expected"]
            assert rpc("Diagnostics")["trace_samples"]==4096
            first=next(d for d in rpc("Devices")["devices"] if d["name"]=="pa-test-0")
            configuration["excluded_devices"]=[first["id"]];configuration["trace"]=False
            config_path.write_text(json.dumps(configuration));rpc("ApplyConfig")
            excluded=rpc("TestReports",json.dumps({"reports":reports[:2]}))
            assert excluded["observed"]==excluded["expected"]
            assert rpc("Diagnostics")["trace_samples"]==0
            call("/Plugins","org.kde.KWin.Plugins.UnloadPlugin","pointeraccel_kwin")
            assert compositor.poll() is None
            print("nested KWin: load, RELRO interception, per-report motion, shared mouse state, touchpad passthrough, 125% scale, disable, mismatch fallback, Wayland/XWayland raw motion, re-enable, unload: PASS")
        except Exception:
            log.flush();log.seek(0);print(log.read(),file=sys.stderr);raise
        finally:
            compositor.terminate()
            try:compositor.wait(timeout=10)
            except subprocess.TimeoutExpired:compositor.kill();compositor.wait()
            log.close()

if __name__=="__main__":
    build=Path(sys.argv[1]).resolve()
    if "--inside" in sys.argv: inside(build)
    else:
        with tempfile.TemporaryFile(mode="w+") as log:
            process=subprocess.Popen(["dbus-run-session","--",sys.executable,__file__,str(build),"--inside"],
                                     stdout=log,stderr=log,start_new_session=True)
            try: result=process.wait(timeout=35)
            finally:
                try:os.killpg(process.pid,signal.SIGTERM)
                except ProcessLookupError:pass
            log.seek(0);print(log.read(),end="")
            sys.exit(result)
