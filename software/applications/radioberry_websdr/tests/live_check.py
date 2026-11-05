"""Read-only LAN measurement of the WebSDR firmware. Python standard library only.
Never tunes, flashes or resets the board. Not a dependency of the demo itself.
"""
import argparse
import json
import math
import statistics
import sys
import time
import urllib.request
import urllib.error

sys.dont_write_bytecode = True
import spectrum_format as reference

def measure(url, seconds):
    timings = []
    def get(path):
        started = time.monotonic()
        with urllib.request.urlopen(url.rstrip("/") + path, timeout=3) as response:
            data = response.read()
            code = response.status
        timings.append((time.monotonic() - started) * 1000)
        return code, data
    def status():
        return json.loads(get("/api/status")[1])
    confirmed = status()
    if confirmed.get("synthetic") is not False:
        raise RuntimeError("Board draait synthetische firmware; deze meting vereist echte RX-data.")
    start_diag = json.loads(get("/api/diagnostics")[1])
    start = time.monotonic()
    packets = gaps = empty = 0
    latest = first = None
    page_times, averages = [], set()
    latest_levels = None
    while time.monotonic() - start < seconds:
        tick = time.monotonic()
        if packets % 30 == 0:
            page_start = time.monotonic()
            code, page = get("/")
            if code != 200 or not page.startswith(b"<!doctype html>"):
                raise AssertionError("Embedded page response")
            page_times.append((time.monotonic() - page_start) * 1000)
        code, packet = get("/api/spectrum")
        if code == 204:
            empty += 1
        else:
            h, levels = reference.decode(packet)
            if h["flags"] & 1 or h["fft_size"] != 512 or h["window"] != 1 or not h["average_count"]:
                raise AssertionError("Real FFT metadata")
            if (h["stream_id"], h["config_id"], h["center_hz"]) != (
                    confirmed["stream_id"], confirmed["config_id"], confirmed["frequency_hz"]):
                # Another browser may tune during this read-only observation.
                confirmed = status()
                latest = first = None
                continue
            if latest is None or reference.newer(h["sequence"], latest["sequence"]):
                first = h if first is None else first
                latest = h
                latest_levels = levels
                averages.add(h["average_count"])
                packets += 1
                gaps += bool(h["flags"] & 2)
        time.sleep(max(0, .1 - (time.monotonic() - tick)))
    elapsed = time.monotonic() - start
    end_diag = json.loads(get("/api/diagnostics")[1])
    delta = lambda key: (end_diag[key] - start_diag[key]) & 0xffffffff
    if not packets:
        raise AssertionError("No fresh real spectrum during observation")
    try:
        iq_code, iq_data = get("/api/iq")
        iq = json.loads(iq_data) if iq_code == 200 else None
    except urllib.error.HTTPError as error:
        if error.code != 404:
            raise
        iq = None  # Firmware from before the raw IQ measurement endpoint.
    if iq:
        rms = math.hypot(iq["i_rms_counts"], iq["q_rms_counts"])
        iq["complex_rms_dbfs"] = round(20 * math.log10(rms / iq["full_scale_counts"]), 2) if rms else None
    report = {
        "url": url, "seconds": round(elapsed, 2), "fresh_frames_received": packets,
        "frames_produced": delta("frames"), "averaging_counts": sorted(averages),
        "empty_responses": empty, "input_gap_frames": gaps,
        "dma_stalls_delta": delta("rx_stalls"), "tag_errors_delta": delta("tag_errors"),
        "invalid_fft_delta": delta("invalid_fft"),
        "approx_iq_pairs_per_second": round(delta("rx_pairs") / elapsed),
        "http_mean_ms": round(statistics.mean(timings), 2), "http_max_ms": round(max(timings), 2),
        "page_load_ms": [round(value, 2) for value in page_times],
        "diagnostics": end_diag,
        "last_spectrum_db": {"min": min(latest_levels), "median": statistics.median(latest_levels),
                             "max": max(latest_levels)},
        "raw_iq": iq,
        "last_header": {key: value.decode("ascii") if isinstance(value, bytes) else value
                        for key, value in latest.items()},
    }
    print(json.dumps(report, indent=2))
    if delta("rx_stalls") or delta("tag_errors") or delta("invalid_fft"):
        raise AssertionError("RX discontinuity under LAN load; inspect the reported counters")
    return report

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", required=True, help="Board base URL, for example http://<board-ip>")
    parser.add_argument("--seconds", type=float, default=20)
    args = parser.parse_args()
    if args.seconds < 1 or args.seconds > 3600:
        parser.error("--seconds must be between 1 and 3600")
    try:
        measure(args.url, args.seconds)
    except (RuntimeError, AssertionError, OSError, ValueError) as error:
        parser.exit(1, str(error) + "\n")
