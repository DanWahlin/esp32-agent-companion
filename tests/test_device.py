import time
import unittest
from tools.device import NonResettingSerial, read_frame_end


class Port:
    def __init__(self, chunks):
        self.chunks = iter(chunks)

    def read_until(self, _):
        return next(self.chunks, b"")


class FrameProtocolTests(unittest.TestCase):
    def test_monitor_does_not_drive_reset_lines(self):
        port = NonResettingSerial(port=None)
        port._update_dtr_state()
        port._update_rts_state()

    def test_lf_and_crlf(self):
        for ending in (b"\n", b"\r\n"):
            read_frame_end(Port([b"\n", b"END_FRAME" + ending]), time.monotonic() + 1)

    def test_partial_serial_reads(self):
        read_frame_end(Port([b"\nE", b"ND_", b"FRAME", b"\r\n"]), time.monotonic() + 1)

    def test_rejects_unexpected_output(self):
        with self.assertRaises(RuntimeError):
            read_frame_end(Port([b"\nCAPTURE_ERROR timed out\n"]), time.monotonic() + 1)

    def test_times_out(self):
        with self.assertRaises(TimeoutError):
            read_frame_end(Port([]), time.monotonic() - 1)


if __name__ == "__main__":
    unittest.main()
