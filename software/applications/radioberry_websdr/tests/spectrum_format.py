"""Decode the fixed RBS1 spectrum profile shared by the host and LAN checks."""
import struct

_HEADER = struct.Struct("<4sBBH7IHHhHHBB")
_FIELDS = (
    "magic", "version", "flags", "header_bytes", "sequence", "config_id",
    "stream_id", "capture_ms", "center_hz", "sample_rate_hz", "span_hz",
    "bin_count", "fft_size", "db_offset_centi", "db_step_centi",
    "average_count", "window", "reserved",
)


def decode(frame):
    """Return (header, dB levels); reject truncated or incompatible frames."""
    if len(frame) != 560:
        raise ValueError("RBS1 frame must contain 560 bytes")
    header = dict(zip(_FIELDS, _HEADER.unpack_from(frame)))
    if (header["magic"] != b"RBS1" or header["version"] != 1
            or header["header_bytes"] != _HEADER.size):
        raise ValueError("Invalid RBS1 header")
    if (header["flags"] & ~15 or header["reserved"] != 0
            or header["sample_rate_hz"] != 48000 or header["span_hz"] != 48000
            or header["bin_count"] != 512 or header["db_offset_centi"] != -12750
            or header["db_step_centi"] != 50 or header["stream_id"] == 0
            or not 100000 <= header["center_hz"] <= 30000000):
        raise ValueError("Unsupported RBS1 profile")
    if header["flags"] & 1:
        valid_source = (header["fft_size"] == 0 and header["average_count"] == 0
                        and header["window"] == 0)
    else:
        valid_source = (header["fft_size"] == 512 and header["average_count"] >= 1
                        and header["window"] == 1)
    if not valid_source:
        raise ValueError("Inconsistent RBS1 source metadata")
    levels = [(header["db_offset_centi"] + value * header["db_step_centi"]) / 100
              for value in frame[_HEADER.size:]]
    return header, levels


def newer(sequence, previous):
    """Compare uint32 sequence numbers, allowing wraparound, excluding ties."""
    delta = (sequence - previous) & 0xffffffff
    return 0 < delta < 0x80000000
