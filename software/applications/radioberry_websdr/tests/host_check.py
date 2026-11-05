"""Run the actual C firmware service on the host. Standard library and GCC.
--serve is a localhost-only browser test fixture, not a demo dependency.
"""
import argparse
import ctypes as c
import io
import json
import math
import os
import pathlib
import shutil
import socketserver
import select
import subprocess
import sys
import tempfile
import threading
import time
import zipfile
import uuid

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
import spectrum_format as reference

class Service(c.Structure):
    _fields_ = [(name, c.c_uint32) for name in
                ("frequency", "config_id", "stream_id", "sequence", "capture_ms")] + [
        ("frame", c.c_uint8 * 560), ("scratch", c.c_char * 2049),
        ("synthetic", c.c_bool), ("frame_ready", c.c_bool),
        ("tune", c.c_void_p), ("tune_context", c.c_void_p), ("diagnostics", c.c_uint32 * 10),
        ("input", c.c_uint32 * 15), ("gain", c.c_void_p), ("rx_gain_db", c.c_int32),
        ("agc_mode",c.c_uint32),("agc_revision",c.c_uint32),("agc_threshold_dbfs",c.c_int32),("agc",c.c_void_p)]

class Response(c.Structure):
    _fields_ = [("status", c.c_uint), ("type", c.c_char_p), ("body", c.c_void_p),
                ("length", c.c_size_t), ("storage", c.c_uint8 * 560)]

class Audio(c.Structure):
    _fields_ = [("tone", c.c_float * 3), ("stream_id", c.c_uint32),
                ("sequence", c.c_uint32), ("next_ms", c.c_uint32), ("first_sample", c.c_uint64)]

def build(directory):
    compiler = os.environ.get("CC") or "gcc"
    gcc = shutil.which(compiler)
    if not gcc:
        raise RuntimeError(
            f"Host C compiler {compiler!r} not found. Install GCC on PATH or set CC "
            "to the host compiler executable (without flags).")
    assets = directory / "assets.c"
    subprocess.run([sys.executable, str(ROOT / "embed_assets.py"), str(assets)], check=True)
    dll = directory / ("service.dll" if sys.platform == "win32" else "service.so")
    common = [gcc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT),
              "-I", str(ROOT.parents[1] / "components/dsp/include")]
    dsp_dll = directory / ("spectrum.dll" if sys.platform == "win32" else "spectrum.so")
    audio_sources = [str(ROOT / "websdr_audio.c"), str(ROOT.parents[1] / "components/dsp/src/tone.c"),
                     str(ROOT / "websdr_receiver.c"), str(ROOT.parents[1] / "components/dsp/src/am.c"),
                     str(ROOT.parents[1] / "components/dsp/src/ssb.c"),str(ROOT.parents[1] / "components/dsp/src/agc.c"),
                     str(ROOT.parents[1] / "components/dsp/src/receiver.c")]
    spectrum_sources = [str(ROOT / "websdr_spectrum.c"),
                        str(ROOT.parents[1] / "components/dsp/src/spectrum.c"),
                        str(ROOT.parents[1] / "components/dsp/src/fft.c")]
    subprocess.run(common + audio_sources + spectrum_sources + ["-shared", "-fPIC", str(ROOT / "websdr_service.c"),
        str(ROOT / "websdr_radio_config.c"),
        str(ROOT / "websdr_iq.c"), str(ROOT / "tests/iq_check.c"),
        str(ROOT / "tests/spectrum_check.c"), str(ROOT / "tests/audio_check.c"), str(ROOT / "tests/am_check.c"),
        str(ROOT / "tests/ssb_check.c"), str(ROOT / "tests/agc_check.c"), str(assets), "-lm", "-o", str(dsp_dll)], check=True)
    spectrum = c.CDLL(str(dsp_dll))
    result = spectrum.rb_test_spectrum()
    assert result == 0, f"Production spectrum/live service failed at C test line {result}"
    result = spectrum.rb_test_iq()
    assert result == 0, f"Production IQ failed at C test line {result}"
    result = spectrum.rb_test_audio()
    assert result == 0, f"Production PCM failed at C test line {result}"
    result=spectrum.rb_test_am()
    assert result==0,f"Production AM failed at C test line {result}"
    result=spectrum.rb_test_ssb()
    assert result==0,f"Production SSB failed at C test line {result}"
    result=spectrum.rb_test_agc()
    assert result==0,f"Production AGC failed at C test line {result}"
    if sys.platform == "win32":
        import _ctypes
        _ctypes.FreeLibrary(spectrum._handle)
    subprocess.run(common + audio_sources + spectrum_sources + ["-shared", "-fPIC", str(ROOT / "websdr_service.c"),
        str(ROOT / "websdr_radio_config.c"),
        str(ROOT / "tests/spectrum_check.c"), str(assets),
                             "-lm", "-o", str(dll)], check=True)
    mock = directory / ("transport.dll" if sys.platform == "win32" else "transport.so")
    subprocess.run(common + audio_sources + ["-shared", "-fPIC", "-I", str(ROOT / "tests/mock"),
        str(ROOT / "websdr_service.c"), str(ROOT / "websdr_http.c"), str(ROOT / "websdr_iq.c"),
        str(ROOT / "tests/transport_check.c"), str(assets), "-lm", "-o", str(mock)], check=True)
    transport = c.CDLL(str(mock))
    assert transport.rb_test_transport() == 0
    if sys.platform == "win32":
        import _ctypes
        _ctypes.FreeLibrary(transport._handle)
    vendor = ROOT.parents[1] / "libraries/ioLibrary_Driver/Ethernet"
    recv_dll = directory / ("wiznet-recv.dll" if sys.platform == "win32" else "wiznet-recv.so")
    subprocess.run(common + ["-shared", "-fPIC", "-D_WIZCHIP_=5500", "-I", str(vendor),
        "-Wno-unused-variable", "-Wno-unused-but-set-variable", "-Wno-misleading-indentation",
        "-Wno-unused-parameter", "-Wno-unused-function",
        str(vendor / "socket.c"), str(ROOT / "tests/wiznet_recv_check.c"),
        "-o", str(recv_dll)], check=True)
    recv_lib = c.CDLL(str(recv_dll))
    recv_result = recv_lib.rb_test_wiznet_recv()
    assert recv_result == 0, f"Actual WIZnet recv regression: case {recv_result}"
    if sys.platform == "win32":
        _ctypes.FreeLibrary(recv_lib._handle)
    lib = c.CDLL(str(dll))
    lib.rb_web_init.argtypes = [c.POINTER(Service), c.c_uint32, c.c_uint32]
    lib.rb_web_generate.argtypes = [c.POINTER(Service), c.c_uint32]
    lib.rb_web_init_live.argtypes = [c.POINTER(Service), c.c_uint32, c.c_void_p, c.c_void_p]
    lib.rb_test_live_generate.argtypes = [c.POINTER(Service), c.c_uint32]
    lib.rb_web_request.argtypes = [c.POINTER(Service), c.c_char_p, c.c_size_t,
                                   c.c_uint32, c.POINTER(Response)]
    lib.rb_web_request.restype = c.c_int
    lib.rb_web_audio_open.argtypes = [c.POINTER(Audio), c.c_uint32, c.c_uint32]
    lib.rb_web_audio_read.argtypes = [c.POINTER(Audio), c.c_uint32, c.c_void_p]
    lib.rb_web_audio_read.restype = c.c_bool
    iq_dll = directory / ("iq-fixture.dll" if sys.platform == "win32" else "iq-fixture.so")
    subprocess.run(common + audio_sources + ["-shared", "-fPIC", str(ROOT / "tests/am_fixture.c"), str(ROOT / "websdr_iq.c"),
        str(ROOT / "tests/iq_fixture.c"), "-lm", "-o", str(iq_dll)], check=True)
    lib._iq_lib = iq = c.CDLL(str(iq_dll))
    iq.rb_test_iq_fixture_open.argtypes = [c.c_uint32]
    iq.rb_test_iq_fixture_open.restype = c.c_bool
    iq.rb_test_iq_fixture_read.argtypes = [c.c_void_p]
    iq.rb_test_iq_fixture_read.restype = c.c_bool
    iq.rb_test_iq_fixture_produce.argtypes = [c.c_uint32,c.c_uint32,c.c_uint32]
    iq.rb_test_iq_fixture_gap.argtypes = [c.c_uint32]
    iq.rb_test_iq_fixture_init()
    iq.rb_test_am_fixture_init()
    iq.rb_test_am_fixture_open.argtypes=[c.c_uint32]
    iq.rb_test_am_fixture_open.restype=c.c_bool
    iq.rb_test_rx_fixture_open.argtypes=[c.c_uint32,c.c_uint8]
    iq.rb_test_rx_fixture_open.restype=c.c_bool
    iq.rb_test_rx_fixture_agc.argtypes=[c.c_uint32,c.c_int32]
    iq.rb_test_rx_fixture_agc.restype=c.c_bool
    iq.rb_test_am_fixture_read.argtypes=[c.c_void_p]
    iq.rb_test_am_fixture_read.restype=c.c_bool
    iq.rb_test_am_fixture_produce.argtypes=[c.c_uint32,c.c_uint32]
    return lib

def request(lib, service, data, now=1000):
    r = Response()
    if not lib.rb_web_request(c.byref(service), data, len(data), now, c.byref(r)):
        return None
    return r.status, r.type.decode(), c.string_at(r.body, r.length)

def tune(frequency):
    body = json.dumps({"frequency_hz": frequency}, separators=(",", ":")).encode()
    return (b"POST /api/tune HTTP/1.1\r\nContent-Type: application/json\r\nContent-Length: " +
            str(len(body)).encode() + b"\r\n\r\n" + body)

def gain(value):
    body = json.dumps({"rx_gain_db": value}, separators=(",", ":")).encode()
    return (b"POST /api/gain HTTP/1.1\r\nContent-Type: application/json\r\nContent-Length: " +
            str(len(body)).encode() + b"\r\n\r\n" + body)

def self_check(lib):
    s = Service()
    lib.rb_web_init(c.byref(s), 0x12345678, 1000)
    checks = []
    def check(ok, label):
        if not ok:
            raise AssertionError(label)
        checks.append(label)
    get_frame = b"GET /api/spectrum HTTP/1.1\r\nHost: radio\r\n\r\n"
    result = request(lib, s, get_frame)
    h, levels = reference.decode(result[2])
    check(result[:2] == (200, "application/octet-stream") and len(result[2]) == 560,
          "C firmware frame decodes with the shared RBS1 reference")
    check(h["stream_id"] == 0x12345678 and h["capture_ms"] == 1000 and h["flags"] == 1,
          "Synthetic metadata, byte order, stream and timestamp")
    peak = max(range(512), key=levels.__getitem__)
    expected = 7100000 + 4200 + 1300 * math.sin(1 / 6)
    check(abs(7100000 + (peak - 256) * 93.75 - expected) <= 93.75,
          "Known carrier on exact FFT-bin frequency coordinates")
    old_packet = result[2]
    command = tune(7105000)
    check(all(request(lib, s, command[:n]) is None and s.frequency == 7100000
              for n in range(len(command))), "Every fragmented tune prefix is side-effect-free")
    result = request(lib, s, command, 1200)
    ack = json.loads(result[2])
    check(ack["frequency_hz"] == 7105000 and ack["config_id"] == 2, "Tune confirms applied synthetic configuration")
    h, _ = reference.decode(request(lib, s, get_frame)[2])
    check(h["center_hz"] == 7105000 and h["config_id"] == 2 and h["capture_ms"] == 1200,
          "First new spectrum and tune acknowledgement agree")
    request(lib, s, command, 1300)
    check(s.config_id == 2, "Repeated absolute tune is idempotent")
    check(reference.decode(old_packet)[0]["center_hz"] == 7100000, "Coherent snapshot across tuning")
    for value in (99999, 30000001, -1, 7100.5, "7100000", None, True):
        check(request(lib, s, tune(value))[0] == 400 and s.frequency == 7105000,
              f"Invalid tune rejected without mutation: {value!r}")
    for value in (100000, 30000000):
        check(request(lib, s, tune(value))[0] == 200 and s.frequency == value,
              f"Inclusive frequency boundary {value}")
    cases = [
        (b"GET /missing HTTP/1.1\r\n\r\n", 404),
        (b"DELETE /api/status HTTP/1.1\r\n\r\n", 405),
        (b"POST /api/tune HTTP/1.1\r\n\r\n", 411),
        (b"POST /api/tune HTTP/1.1\r\nContent-Length: 0\r\n\r\n", 415),
        (b"POST /api/tune HTTP/1.1\r\nContent-Length: 129\r\n\r\n", 413),
        (b"GET /api/status HTTP/1.1\r\nContent-Length: 0\r\nContent-Length: 0\r\n\r\n", 400),
        (b"GET /api/status HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n", 400),
        (b"GET /api/status HTTP/1.1\r\nBrokenHeader\r\n\r\n", 400),
        (b"GET /api/status HTTP/1.1\r\n\r\nextra", 400),
        (b"GET /api/status HTTP/1.1\r\nX: \x00\r\n\r\n", 400),
        (b"x" * 2048, 431), (b"x" * 2049, 413),
    ]
    for i, (data, code) in enumerate(cases):
        check(request(lib, s, data)[0] == code, f"HTTP rejection {i}: status {code}")
    check(request(lib, s, b"GET / HTTP/1.1\r\n\r\n")[2].startswith(b"<!doctype html>"), "Embedded offline browser page")
    check(request(lib,s,b"GET /api/iq-stream HTTP/1.1\r\n\r\n")[0]==503,
          "Synthetic spectrum service does not advertise a hardware IQ stream")
    check(request(lib,s,b"GET /api/iq-stream HTTP/1.0\r\n\r\n")[0]==400,
          "Chunked IQ endpoint requires HTTP/1.1")
    for asset in ("iq-probe.html","iq-stream.js","iq-probe.js"):
        check(request(lib,s,f"GET /{asset} HTTP/1.1\r\n\r\n".encode())[0]==200,
              f"Embedded IQ browser asset: {asset}")
    check(request(lib,s,b"GET /iq-probe.html?demo=1 HTTP/1.1\r\n\r\n")[0]==200,
          "Static IQ page accepts preview query without changing API paths")
    source = request(lib, s, b"GET /browser-source.zip HTTP/1.1\r\n\r\n")[2]
    with zipfile.ZipFile(io.BytesIO(source)) as z:
        check({"src/board.js", "src/app.js", "build.ps1", "README.md", "LICENSE-ubersdr.txt"}.issubset(z.namelist()),
              "Editable browser source and GPL notices served")
    s.sequence = 0xffffffff
    lib.rb_web_generate(c.byref(s), 25)
    check(s.sequence == 0 and reference.decode(bytes(s.frame))[0]["capture_ms"] == 25,
          "Unsigned sequence and time wrap")
    gain_request = gain(-12)
    before_config = s.config_id
    check(all(request(lib, s, gain_request[:n]) is None and s.rx_gain_db == 14
              for n in range(len(gain_request))), "Fragmented gain writes do not change the VGA setting")
    for value in (-12, 0, 48, 14):
        result = request(lib, s, gain(value), 1300)
        state = json.loads(result[2])
        before_config += 1
        check(result[0] == 200 and state["rx_gain_db"] == value and
              state["config_id"] == before_config and state["frequency_hz"] == 30000000,
              f"Gain {value} dB acknowledged without retuning")
        check(reference.decode(bytes(s.frame))[0]["config_id"] == before_config,
              "Gain acknowledgement and first synthetic frame have matching configuration")
    request(lib, s, gain(14))
    check(s.config_id == before_config, "Identical gain writes are idempotent")
    for value in (-13, 49, 1.5, "48", True, None, -2147483648, 4294967296):
        check(request(lib, s, gain(value))[0] == 400 and s.rx_gain_db == 14 and s.config_id == before_config,
              f"Invalid gain rejected without mutation: {value!r}")
    for body in (b'{"rx_gain_db":01}', b'{"rx_gain_db":+1}', b'{"rx_gain_db":1e1}',
                 b'{"rx_gain_db":12,"rx_gain_db":48}', b'{"frequency_hz":48}'):
        raw_request = (b"POST /api/gain HTTP/1.1\r\nContent-Type: application/json\r\nContent-Length: " +
                       str(len(body)).encode() + b"\r\n\r\n" + body)
        check(request(lib, s, raw_request)[0] == 400 and s.config_id == before_config,
              f"Strict gain JSON: {body!r}")
    print(json.dumps({"service_checks": len(checks), "fragment_bytes": len(command), "checks": checks}, indent=2), flush=True)

def serve(lib, port_file):
    s = Service()
    lib.rb_web_init(c.byref(s), 12345, 1000)
    lock = threading.Lock()
    fixture = {"offline": False, "hold": False, "delay_ms": 0, "malformed": False,
               "active": 0, "max_active": 0, "spectra": 0, "tunes": 0, "statuses": 0, "real": False,
               "iq_active": False,"iq_connections":0,"iq_frames":0,"iq_hold":False}
    iq = lib._iq_lib
    iq_timing = {"epoch":0.0,"produced":0,"owner":None}
    # Hardware acknowledgement stand-in; numerical data still uses production FFT.
    live_tune = c.CFUNCTYPE(c.c_bool, c.c_void_p, c.c_uint32, c.c_uint32)(lambda *_: True)
    live_gain = c.CFUNCTYPE(c.c_bool, c.c_void_p, c.c_int32, c.c_uint32)(
        lambda *_: not fixture.get("reject_gain", False))
    live_agc=c.CFUNCTYPE(c.c_bool,c.c_void_p,c.c_uint32,c.c_int32)(
        lambda _,mode,threshold: not fixture.get("reject_agc",False) and iq.rb_test_rx_fixture_agc(mode,threshold))
    epoch = time.monotonic()
    now = lambda: (1000 + int((time.monotonic() - epoch) * 1000)) & 0xffffffff
    def generate():
        while True:
            with lock:
                if not fixture["hold"]:
                    (lib.rb_test_live_generate if fixture["real"] else lib.rb_web_generate)(c.byref(s), now())
            time.sleep(.1)
    def generate_iq():
        am_timing={"epoch":time.monotonic(),"produced":0,"active":False}
        while True:
            with lock:
                active=fixture.get("am_active",False)
                if active and not am_timing["active"]:
                    am_timing.update(epoch=time.monotonic(),produced=0)
                am_timing["active"]=active
                if active and not fixture.get("audio_hold"):
                    due=int((time.monotonic()-am_timing["epoch"])*48000/512)
                    if due-am_timing["produced"]>16:
                        iq.rb_test_am_fixture_gap();am_timing["produced"]=due-8
                    while am_timing["produced"]<due:
                        iq.rb_test_am_fixture_produce(s.frequency,s.config_id);am_timing["produced"]+=1
                if fixture["iq_active"] and not fixture["iq_hold"]:
                    due = int((time.monotonic()-iq_timing["epoch"])*48000/512)
                    if due-iq_timing["produced"]>16:
                        skipped=due-iq_timing["produced"]-8
                        iq.rb_test_iq_fixture_gap(skipped*512)
                        iq_timing["produced"]+=skipped
                    while iq_timing["produced"]<due:
                        iq.rb_test_iq_fixture_produce(s.frequency,s.config_id,now())
                        iq_timing["produced"]+=1
            time.sleep(.003)
    class Handler(socketserver.BaseRequestHandler):
        def stream_audio(self,am=False,mode=0):
            with lock:
                if fixture.get("audio_active") or fixture["iq_active"]:
                    self.request.sendall(b"HTTP/1.1 409 Busy\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
                    return
                fixture["audio_active"] = True
                if am:
                    iq.rb_test_rx_fixture_open(s.stream_id,mode);iq.rb_test_rx_fixture_agc(s.agc_mode,s.agc_threshold_dbfs);fixture["am_active"]=True
            size=520 if am else 512
            audio, frame = Audio(), (c.c_uint8 * size)()
            if not am:lib.rb_web_audio_open(c.byref(audio), s.stream_id, now())
            try:
                mime=("application/x-rbpcm-"+{0:"am",1:"usb",2:"lsb"}[mode]) if am else "application/x-rbpcm"
                self.request.sendall(("HTTP/1.1 200 OK\r\nContent-Type: "+mime+"\r\nTransfer-Encoding: chunked\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n").encode())
                wire, last_flush = bytearray(), time.monotonic()
                while True:
                    with lock:
                        if fixture["offline"]: return
                        ready = not fixture.get("audio_hold") and (iq.rb_test_am_fixture_read(frame) if am else lib.rb_web_audio_read(c.byref(audio), now(), frame))
                    if ready: wire.extend(f"{size:X}\r\n".encode() + bytes(frame) + b"\r\n")
                    if wire and (len(wire) >= 2920 or time.monotonic() - last_flush >= .12):
                        self.request.sendall(wire); wire.clear(); last_flush = time.monotonic()
                    if select.select([self.request],[],[],.002)[0]: return
            finally:
                with lock:
                    fixture["audio_active"] = False
                    if am:fixture["am_active"]=False;iq.rb_test_am_fixture_close()
        def stream_iq(self):
            owner = object()
            with lock:
                accepted = not fixture["iq_active"] and not fixture.get("audio_active") and iq.rb_test_iq_fixture_open(s.stream_id)
                if accepted:
                    fixture["iq_active"]=True;fixture["iq_connections"]+=1
                    iq_timing.update(epoch=time.monotonic(),produced=0,owner=owner)
            if not accepted:
                self.request.sendall(b"HTTP/1.1 409 Busy\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
                return
            try:
                self.request.sendall(b"HTTP/1.1 200 OK\r\nContent-Type: application/x-rbiq\r\nTransfer-Encoding: chunked\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n")
                frame = (c.c_uint8*3120)()
                while True:
                    with lock:
                        if fixture["offline"] or iq_timing["owner"] is not owner: return
                        ready=iq.rb_test_iq_fixture_read(frame)
                        if ready: fixture["iq_frames"]+=1
                    if ready:
                        self.request.sendall(b"C30\r\n"+bytes(frame)+b"\r\n")
                    elif select.select([self.request],[],[],.005)[0]:
                        # Detect FIN even when the synthetic input is held.
                        return
            finally:
                with lock:
                    if iq_timing["owner"] is owner:
                        iq.rb_test_iq_fixture_close();fixture["iq_active"]=False;iq_timing["owner"]=None
        def handle(self):
            self.request.settimeout(4)
            data, counted = b"", False
            try:
                while len(data) <= 2048:
                    chunk = self.request.recv(1024)
                    if not chunk:
                        return
                    data += chunk
                    if b"\r\n\r\n" not in data:
                        continue
                    head, body = data.split(b"\r\n\r\n", 1)
                    path = head.split(b" ")[1].decode()
                    if path.startswith("/__test/"):
                        length = next((int(line.split(b":", 1)[1]) for line in head.split(b"\r\n")
                                       if line.lower().startswith(b"content-length:")), 0)
                        if len(body) < length:
                            continue
                        with lock:
                            if path == "/__test/control":
                                values = json.loads(body)
                                if values.get("real") is True:
                                    lib.rb_web_init_live(c.byref(s), s.stream_id + 1, c.cast(live_tune, c.c_void_p), None)
                                    s.gain = c.cast(live_gain, c.c_void_p).value
                                    s.agc=c.cast(live_agc,c.c_void_p).value
                                if values.pop("reboot", False):
                                    if fixture["real"]:
                                        lib.rb_web_init_live(c.byref(s), s.stream_id + 1, c.cast(live_tune, c.c_void_p), None)
                                        s.gain = c.cast(live_gain, c.c_void_p).value
                                        s.agc=c.cast(live_agc,c.c_void_p).value
                                    else:
                                        lib.rb_web_init(c.byref(s), s.stream_id + 1, now())
                                frequency = values.pop("frequency", None)
                                if frequency is not None:
                                    request(lib, s, tune(frequency), now())
                                gain_db = values.pop("gain", None)
                                if gain_db is not None:
                                    request(lib, s, gain(gain_db), now())
                                skipped = values.pop("iq_skip", None)
                                if skipped is not None: iq.rb_test_iq_fixture_gap(int(skipped))
                                if values.pop("iq_disconnect",False): iq_timing["owner"]=None;fixture["iq_active"]=False;iq.rb_test_iq_fixture_close()
                                fixture.update(values)
                            result = (200, "application/json", json.dumps({**fixture,
                                      "frequency": s.frequency, "stream_id": s.stream_id,
                                      "rx_gain_db": s.rx_gain_db}).encode())
                    else:
                        with lock:
                            result = request(lib, s, data, now())
                            if result is None:
                                continue
                            api = path.startswith("/api/")
                            delay = fixture["delay_ms"] / 1000 if api else 0
                            if api:
                                fixture["active"] += 1
                                fixture["max_active"] = max(fixture["active"], fixture["max_active"])
                                counted = True
                            key = {"/api/status": "statuses", "/api/tune": "tunes", "/api/gain": "gains",
                                   "/api/spectrum": "spectra"}.get(path)
                            if key:
                                fixture[key] = fixture.get(key, 0) + 1
                            if api and fixture["offline"]:
                                result = (503, "application/json", b"{}")
                            elif path == "/api/spectrum" and fixture["malformed"]:
                                result = (200, "application/octet-stream", b"broken")
                        if delay:
                            time.sleep(delay)
                    code, mime, payload = result
                    if code==200 and mime=="application/x-rbiq":
                        self.stream_iq()
                        return
                    if code==200 and mime=="application/x-rbpcm":
                        self.stream_audio()
                        return
                    if code==200 and mime in ("application/x-rbpcm-am","application/x-rbpcm-usb","application/x-rbpcm-lsb"):
                        self.stream_audio(True,{"application/x-rbpcm-am":0,"application/x-rbpcm-usb":1,"application/x-rbpcm-lsb":2}[mime])
                        return
                    header = (f"HTTP/1.1 {code} Result\r\nContent-Type: {mime}\r\nContent-Length: {len(payload)}\r\n"
                              "Cache-Control: no-store\r\nConnection: close\r\n\r\n").encode()
                    self.request.sendall(header + payload)
                    return
            except (OSError, ValueError):
                pass
            finally:
                if counted:
                    with lock:
                        fixture["active"] -= 1
    class Server(socketserver.ThreadingTCPServer):
        allow_reuse_address = True
        daemon_threads = True
    threading.Thread(target=generate, daemon=True).start()
    threading.Thread(target=generate_iq, daemon=True).start()
    with Server(("127.0.0.1", 0), Handler) as server:
        pathlib.Path(port_file).write_text(str(server.server_address[1]), encoding="ascii")
        print(f"Browser test fixture: http://127.0.0.1:{server.server_address[1]}/", flush=True)
        server.serve_forever()

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--serve", action="store_true")
    parser.add_argument("--port-file")
    args = parser.parse_args()
    # Retain generated host libraries for diagnosis, in a uniquely named temp dir.
    directory = pathlib.Path(tempfile.gettempdir()) / ("radioberry-service-" + uuid.uuid4().hex)
    directory.mkdir()
    library = build(directory)
    try:
        self_check(library)
        if args.serve:
            if not args.port_file:
                parser.error("--serve requires --port-file")
            serve(library, args.port_file)
    finally:
        if sys.platform == "win32":
            import _ctypes
            _ctypes.FreeLibrary(library._handle)
            _ctypes.FreeLibrary(library._iq_lib._handle)
