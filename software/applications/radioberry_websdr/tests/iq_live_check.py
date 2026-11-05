"""Observe one IQ stream plus concurrent HTTP on the LAN. No tuning or flashing.
Close the browser IQ monitor first: the board allows one IQ listener.
"""
import argparse
import json
import math
import statistics
import struct
import socket
import threading
import time
import urllib.request

HEADER = struct.Struct('<4sBBHIIIIQIIIHBB')

def measure(url, seconds, allow_synthetic=False, http_load_enabled=True, quick_ack=False):
    base = url.rstrip('/')
    def get(path):
        with urllib.request.urlopen(base + path, timeout=3) as r:
            return r.status, r.read()
    def diagnostics(): return json.loads(get('/api/diagnostics')[1])
    start_diag = diagnostics()
    errors, http_times, spectra = [], [], []
    stop = threading.Event()
    def http_load():
        turn = 0
        while not stop.is_set():
            started = time.monotonic()
            try:
                code, status = get('/api/status')
                if code != 200: raise ValueError('status response')
                json.loads(status)
                code, spectrum = get('/api/spectrum')
                if code == 200 and (len(spectrum) != 560 or spectrum[:4] != b'RBS1'):
                    raise ValueError('spectrum response')
                spectra.append(code)
                if turn % 5 == 0:
                    code, page = get('/')
                    if code != 200 or not page.startswith(b'<!doctype html>'): raise ValueError('page response')
            except (OSError, ValueError) as error:
                errors.append(str(error))
            http_times.append((time.monotonic() - started) * 1000)
            turn += 1
            stop.wait(max(0, 1 - (time.monotonic() - started)))
    worker = threading.Thread(target=http_load, daemon=True)
    frames = missed_blocks = unexpected_gaps = configurations = 0
    missed_samples = 0
    previous = last = payload = None
    started = time.monotonic()
    try:
        with urllib.request.urlopen(base+'/api/iq-stream', timeout=3) as stream:
            if quick_ack:
                stream.fp.raw._sock.setsockopt(socket.IPPROTO_TCP,socket.TCP_QUICKACK,1)
            if stream.headers.get_content_type() != 'application/x-rbiq' or stream.headers.get('Transfer-Encoding','').lower() != 'chunked':
                raise ValueError('Expected chunked RBI1 stream')
            if http_load_enabled: worker.start()
            while time.monotonic() - started < seconds:
                packet = stream.read(3120)
                if len(packet) != 3120: raise ValueError('Incomplete IQ frame')
                values = HEADER.unpack_from(packet)
                magic,version,flags,hlen,frame_bytes,sid,config,seq,sample,capture,frequency,rate,pairs,bits,layout = values
                if (magic,version,hlen,frame_bytes,rate,pairs,bits,layout) != (b'RBI1',1,48,3120,48000,512,24,0) or flags & ~7 or not sid:
                    raise ValueError('Invalid RBI1 metadata')
                if flags & 2 and not allow_synthetic: raise ValueError('Synthetic IQ; this is not a hardware measurement')
                last = {'stream_id':sid,'config_id':config,'sequence':seq,'first_sample':sample,
                        'capture_ms':capture,'frequency_hz':frequency,'flags':flags}
                if previous:
                    changed = (sid,config) != (previous['stream_id'],previous['config_id'])
                    if changed: configurations += 1
                    if sid == previous['stream_id']:
                        delta = (seq - previous['sequence']) & 0xffffffff
                        if not delta or delta >= 0x80000000 or sample < previous['first_sample']+512:
                            raise ValueError('Duplicate or reversed IQ data')
                        if not changed:
                            missed_blocks += delta-1
                            missed_samples += sample-previous['first_sample']-512
                            unexpected_gaps += bool(flags & 1)
                previous = last; payload = packet[48:]; frames += 1
    finally:
        stop.set()
        if worker.ident: worker.join(timeout=4)
    elapsed = time.monotonic() - started
    end_diag = diagnostics()
    delta_diag = lambda key: (end_diag[key] - start_diag[key]) & 0xffffffff
    counts = [int.from_bytes(payload[n:n+3],'little',signed=True) for n in range(0,len(payload),3)] if payload else []
    power = sum(n*n for n in counts)/512/8388608**2 if counts else 0
    report = {'url':url,'source':'synthetic' if last and last['flags'] & 2 else 'hardware',
              'concurrent_http_load':http_load_enabled,
              'test_socket_quick_ack':quick_ack,
              'seconds':round(elapsed,3),'frames':frames,'received_pairs_per_second':round(frames*512/elapsed,1),
              'payload_mbit_per_second':round(frames*512*48/elapsed/1000000,4),
              'missed_blocks':missed_blocks,'missed_samples':missed_samples,'unexpected_gap_flags':unexpected_gaps,
              'configuration_changes':configurations,'dma_stalls_delta':delta_diag('rx_stalls'),
              'tag_errors_delta':delta_diag('tag_errors'),'invalid_fft_delta':delta_diag('invalid_fft'),
              'http_requests_failed':errors,'http_cycles':len(http_times),
              'http_cycle_mean_ms':round(statistics.mean(http_times),2) if http_times else None,
              'http_cycle_max_ms':round(max(http_times),2) if http_times else None,
              'spectrum_responses':spectra,'last_header':last,
              'last_complex_rms_dbfs':round(10*math.log10(power),2) if power else None}
    print(json.dumps(report,indent=2))
    if not frames or missed_blocks or missed_samples or unexpected_gaps or errors or any(delta_diag(k) for k in ('rx_stalls','tag_errors','invalid_fft')):
        raise ValueError('IQ or concurrent HTTP discontinuity; inspect report')
    return report

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url',required=True,help='Board base URL, for example http://<board-ip>')
    parser.add_argument('--seconds',type=float,default=20)
    parser.add_argument('--allow-synthetic',action='store_true',help='Only for the host test fixture')
    parser.add_argument('--no-http-load',action='store_true',help='Isolate IQ throughput without concurrent page/status/spectrum requests')
    parser.add_argument('--quick-ack',action='store_true',help='Diagnostic only: request immediate TCP ACKs on this test socket, not browser or system settings')
    args=parser.parse_args()
    if not 1 <= args.seconds <= 3600: parser.error('--seconds must be 1..3600')
    try: measure(args.url,args.seconds,args.allow_synthetic,not args.no_http_load,args.quick_ack)
    except (ValueError,OSError) as error: parser.exit(1,str(error)+'\n')
