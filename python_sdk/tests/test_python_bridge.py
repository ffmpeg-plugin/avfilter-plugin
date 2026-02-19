"""
Unit tests for Python bridge plugin SDK.

These tests verify the Python plugin classes work correctly at the Python level
(without needing the C++ bridge or FFmpeg). They simulate what the C++ bridge
does by creating numpy arrays and calling plugin methods directly.

Run:
    cd ffmpeg_oc_plugins
    python -m pytest python_sdk/tests/test_python_bridge.py -v

Or without pytest:
    python python_sdk/tests/test_python_bridge.py
"""

import sys
import os
import unittest

import numpy as np

# Add parent directories to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "examples"))

from quink_plugin import ProcessPluginBase, DetectPluginBase, ProcessResult


class TestProcessResult(unittest.TestCase):
    """Test ProcessResult enum values match C++ quink::ProcessResult."""

    def test_values(self):
        self.assertEqual(ProcessResult.Ok, 0)
        self.assertEqual(ProcessResult.TryAgain, 1)
        self.assertEqual(ProcessResult.Error, -1)

    def test_int_conversion(self):
        self.assertEqual(int(ProcessResult.Ok), 0)
        self.assertEqual(int(ProcessResult.TryAgain), 1)
        self.assertEqual(int(ProcessResult.Error), -1)


class TestProcessPluginBase(unittest.TestCase):
    """Test ProcessPluginBase default behavior."""

    def test_default_init(self):
        plugin = ProcessPluginBase()
        self.assertTrue(plugin.init("", 1, 1))

    def test_default_flush(self):
        plugin = ProcessPluginBase()
        self.assertFalse(plugin.flush([]))

    def test_default_configure(self):
        plugin = ProcessPluginBase()
        self.assertTrue(plugin.configure([], []))

    def test_process_not_implemented(self):
        plugin = ProcessPluginBase()
        with self.assertRaises(NotImplementedError):
            plugin.process([], [])


class TestDetectPluginBase(unittest.TestCase):
    """Test DetectPluginBase default behavior."""

    def test_default_init(self):
        plugin = DetectPluginBase()
        self.assertTrue(plugin.init("", 1, 1))

    def test_default_flush_detect(self):
        plugin = DetectPluginBase()
        self.assertFalse(plugin.flush_detect(np.zeros((10, 10, 3), dtype=np.uint8)))

    def test_detect_not_implemented(self):
        plugin = DetectPluginBase()
        inp = np.zeros((10, 10, 3), dtype=np.uint8)
        out = np.zeros((10, 10, 3), dtype=np.uint8)
        with self.assertRaises(NotImplementedError):
            plugin.detect(inp, out)


class TestBlurPlugin(unittest.TestCase):
    """Test the example BlurPlugin."""

    @classmethod
    def setUpClass(cls):
        try:
            from blur_plugin import BlurPlugin
            cls.BlurPlugin = BlurPlugin
            cls.has_plugin = True
        except ImportError:
            cls.has_plugin = False

    def setUp(self):
        if not self.has_plugin:
            self.skipTest("BlurPlugin not importable")

    def test_init_default(self):
        plugin = self.BlurPlugin()
        self.assertTrue(plugin.init("", 1, 1))
        self.assertEqual(plugin.ksize, 15)
        self.assertEqual(plugin.scale, 1.0)

    def test_init_params(self):
        plugin = self.BlurPlugin()
        self.assertTrue(plugin.init("ksize=21&scale=0.5", 1, 1))
        self.assertEqual(plugin.ksize, 21)
        self.assertAlmostEqual(plugin.scale, 0.5)

    def test_init_even_ksize(self):
        """Even kernel sizes should be rounded up to odd."""
        plugin = self.BlurPlugin()
        self.assertTrue(plugin.init("ksize=20", 1, 1))
        self.assertEqual(plugin.ksize, 21)

    def test_init_wrong_io(self):
        plugin = self.BlurPlugin()
        self.assertFalse(plugin.init("", 2, 1))

    def test_process(self):
        """Test that process produces a blurred output."""
        plugin = self.BlurPlugin()
        plugin.init("ksize=5", 1, 1)

        # Create a test input with sharp edges
        inp = np.zeros((64, 64, 3), dtype=np.uint8)
        inp[20:40, 20:40] = 255  # White square

        out = np.zeros_like(inp)
        result = plugin.process([inp], [out])

        self.assertEqual(result, ProcessResult.Ok)
        # Output should be different from input (blurred)
        self.assertFalse(np.array_equal(inp, out))
        # Output should not be all zeros
        self.assertGreater(out.sum(), 0)

    def test_process_empty(self):
        plugin = self.BlurPlugin()
        plugin.init("", 1, 1)
        result = plugin.process([], [])
        self.assertEqual(result, ProcessResult.Error)

    def test_flush(self):
        plugin = self.BlurPlugin()
        plugin.init("", 1, 1)
        self.assertFalse(plugin.flush([]))

    def test_configure_default_scale(self):
        plugin = self.BlurPlugin()
        plugin.init("", 1, 1)

        in_cfg = [{"width": 320, "height": 240, "cv_type": 16,
                   "pix_fmt": 0, "colorspace": 0, "limited_range": False}]
        out_cfg = [{"width": 320, "height": 240, "cv_type": 16,
                    "pix_fmt": 0, "colorspace": 0, "limited_range": False}]

        self.assertTrue(plugin.configure(in_cfg, out_cfg))
        self.assertEqual(out_cfg[0]["width"], 320)
        self.assertEqual(out_cfg[0]["height"], 240)

    def test_configure_with_scale(self):
        plugin = self.BlurPlugin()
        plugin.init("scale=0.5", 1, 1)

        in_cfg = [{"width": 320, "height": 240, "cv_type": 16,
                   "pix_fmt": 0, "colorspace": 0, "limited_range": False}]
        out_cfg = [{"width": 320, "height": 240, "cv_type": 16,
                    "pix_fmt": 0, "colorspace": 0, "limited_range": False}]

        self.assertTrue(plugin.configure(in_cfg, out_cfg))
        self.assertEqual(out_cfg[0]["width"], 160)
        self.assertEqual(out_cfg[0]["height"], 120)


class TestColorDetectPlugin(unittest.TestCase):
    """Test the example ColorDetectPlugin."""

    @classmethod
    def setUpClass(cls):
        try:
            from detect_color_plugin import ColorDetectPlugin
            cls.ColorDetectPlugin = ColorDetectPlugin
            cls.has_plugin = True
        except ImportError:
            cls.has_plugin = False

    def setUp(self):
        if not self.has_plugin:
            self.skipTest("ColorDetectPlugin not importable")

    def test_init(self):
        plugin = self.ColorDetectPlugin()
        self.assertTrue(plugin.init("", 1, 1))

    def test_detect_empty_frame(self):
        """Black frame should produce no detections."""
        plugin = self.ColorDetectPlugin()
        plugin.init("", 1, 1)

        inp = np.zeros((64, 64, 3), dtype=np.uint8)
        out = np.zeros_like(inp)

        result_code, detections = plugin.detect(inp, out)
        self.assertEqual(result_code, ProcessResult.Ok)
        self.assertEqual(len(detections["boxes"]), 0)
        # Output should equal input (pass-through)
        np.testing.assert_array_equal(out, inp)

    def test_detect_passthrough(self):
        """Output should match input (pass-through detection)."""
        plugin = self.ColorDetectPlugin()
        plugin.init("", 1, 1)

        inp = np.random.randint(0, 256, (64, 64, 3), dtype=np.uint8)
        out = np.zeros_like(inp)

        plugin.detect(inp, out)
        np.testing.assert_array_equal(out, inp)


class TestZeroCopySimulation(unittest.TestCase):
    """
    Simulate the zero-copy behavior between C++ and Python.

    The C++ bridge creates numpy arrays that share memory with cv::Mat.
    These tests verify that writing into the pre-allocated output buffer
    works correctly.
    """

    def test_copyto_writes_into_preallocated(self):
        """np.copyto should write into the pre-allocated buffer."""
        # Simulate pre-allocated output from FFmpeg
        output_buffer = np.zeros((64, 64, 3), dtype=np.uint8)
        original_data_ptr = output_buffer.ctypes.data

        # Simulate plugin writing into output
        result = np.full((64, 64, 3), 128, dtype=np.uint8)
        np.copyto(output_buffer, result)

        # Data pointer should not change (same memory)
        self.assertEqual(output_buffer.ctypes.data, original_data_ptr)
        # Values should be written
        np.testing.assert_array_equal(output_buffer, result)

    def test_readonly_input(self):
        """Read-only input arrays should not be writable."""
        inp = np.zeros((64, 64, 3), dtype=np.uint8)
        inp.flags.writeable = False

        with self.assertRaises(ValueError):
            inp[0, 0, 0] = 255


class TestParameterParsing(unittest.TestCase):
    """Test parameter parsing logic used by example plugins."""

    def test_blur_params(self):
        from blur_plugin import BlurPlugin
        plugin = BlurPlugin()
        plugin.init("ksize=7&scale=2.0", 1, 1)
        self.assertEqual(plugin.ksize, 7)
        self.assertAlmostEqual(plugin.scale, 2.0)

    def test_empty_params(self):
        from blur_plugin import BlurPlugin
        plugin = BlurPlugin()
        plugin.init("", 1, 1)
        self.assertEqual(plugin.ksize, 15)  # default


class TestMultiPluginInstances(unittest.TestCase):
    """
    Test that multiple plugin instances maintain independent state.

    This simulates what happens when the C++ bridge loads multiple
    Python plugins in the same FFmpeg process (e.g., chained filters).
    """

    def test_same_plugin_different_params(self):
        """Two BlurPlugin instances with different ksize should be independent."""
        from blur_plugin import BlurPlugin

        plugin1 = BlurPlugin()
        plugin1.init("ksize=3", 1, 1)

        plugin2 = BlurPlugin()
        plugin2.init("ksize=31", 1, 1)

        # Each should have its own ksize
        self.assertEqual(plugin1.ksize, 3)
        self.assertEqual(plugin2.ksize, 31)

        # Process with both — outputs should differ
        inp = np.zeros((64, 64, 3), dtype=np.uint8)
        inp[20:40, 20:40] = 255

        out1 = np.zeros_like(inp)
        out2 = np.zeros_like(inp)

        plugin1.process([inp], [out1])
        plugin2.process([inp], [out2])

        # Both should be non-zero (processed)
        self.assertGreater(out1.sum(), 0)
        self.assertGreater(out2.sum(), 0)
        # ksize=31 is more blurred, so outputs should differ
        self.assertFalse(np.array_equal(out1, out2))

    def test_different_plugins_coexist(self):
        """BlurPlugin and InvertPlugin should coexist without interference."""
        from blur_plugin import BlurPlugin
        from invert_plugin import InvertPlugin

        blur = BlurPlugin()
        blur.init("ksize=5", 1, 1)

        invert = InvertPlugin()
        invert.init("", 1, 1)

        inp = np.full((64, 64, 3), 100, dtype=np.uint8)

        # Process with blur
        blur_out = np.zeros_like(inp)
        blur.process([inp], [blur_out])

        # Process with invert
        inv_out = np.zeros_like(inp)
        invert.process([inp], [inv_out])

        # Invert should produce 255 - 100 = 155
        np.testing.assert_array_equal(inv_out, 155)
        # Blur output should be close to 100 (uniform input, blur does nothing visible)
        np.testing.assert_array_almost_equal(blur_out, inp, decimal=0)

    def test_chained_processing(self):
        """Simulate blur → invert chain: output of first is input to second."""
        from blur_plugin import BlurPlugin
        from invert_plugin import InvertPlugin

        blur = BlurPlugin()
        blur.init("ksize=5", 1, 1)

        invert = InvertPlugin()
        invert.init("", 1, 1)

        # Create input with sharp edges
        inp = np.zeros((64, 64, 3), dtype=np.uint8)
        inp[20:40, 20:40] = 200

        # Step 1: Blur
        blurred = np.zeros_like(inp)
        blur.process([inp], [blurred])

        # Step 2: Invert the blurred result
        final = np.zeros_like(inp)
        invert.process([blurred], [final])

        # Final should be 255 - blurred
        expected = (255 - blurred).astype(np.uint8)
        np.testing.assert_array_equal(final, expected)


if __name__ == "__main__":
    unittest.main()
