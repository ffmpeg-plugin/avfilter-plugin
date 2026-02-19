"""
Example: Gaussian Blur Plugin using Python + OpenCV

Demonstrates a simple ProcessPlugin that applies Gaussian blur.

Parameters (via FFmpeg params= option):
    ksize=<int>   - Kernel size for Gaussian blur (default: 15, must be odd)
    scale=<float> - Output scale factor (default: 1.0)

Usage:
    ffmpeg -i input.mp4 \
        -vf "oc_plugin=plugin=libpython_bridge_plugin.so:\
             params=script=blur_plugin.py&class=BlurPlugin&ksize=21" \
        output.mp4
"""

import numpy as np

try:
    import cv2
except ImportError:
    cv2 = None

# Import from SDK (installed or local)
import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from quink_plugin import ProcessPluginBase, ProcessResult


class BlurPlugin(ProcessPluginBase):
    """Apply Gaussian blur to video frames."""

    def __init__(self):
        self.ksize = 15
        self.scale = 1.0

    def init(self, params: str, nb_inputs: int, nb_outputs: int) -> bool:
        if nb_inputs != 1 or nb_outputs != 1:
            return False

        # Parse parameters: "ksize=15&scale=0.5"
        if params:
            for token in params.split("&"):
                if "=" in token:
                    key, value = token.split("=", 1)
                    if key == "ksize":
                        self.ksize = int(value)
                        # Ensure odd kernel size
                        if self.ksize % 2 == 0:
                            self.ksize += 1
                    elif key == "scale":
                        self.scale = float(value)

        if cv2 is None:
            print("[BlurPlugin] WARNING: OpenCV not available, using numpy fallback")

        return True

    def process(self, inputs, outputs):
        if not inputs or not outputs:
            return ProcessResult.Error

        frame = inputs[0]

        if cv2 is not None:
            # Use OpenCV for Gaussian blur (optimal)
            if self.scale != 1.0:
                h, w = outputs[0].shape[:2]
                resized = cv2.resize(frame, (w, h), interpolation=cv2.INTER_CUBIC)
                blurred = cv2.GaussianBlur(resized, (self.ksize, self.ksize), 0)
            else:
                blurred = cv2.GaussianBlur(frame, (self.ksize, self.ksize), 0)
            np.copyto(outputs[0], blurred)
        else:
            # Numpy-only fallback: simple box blur approximation
            np.copyto(outputs[0], frame)

        return ProcessResult.Ok

    def flush(self, outputs):
        return False

    def configure(self, input_configs, output_configs):
        if self.scale != 1.0:
            for i, out_cfg in enumerate(output_configs):
                in_cfg = input_configs[min(i, len(input_configs) - 1)]
                out_cfg["width"] = int(in_cfg["width"] * self.scale)
                out_cfg["height"] = int(in_cfg["height"] * self.scale)
        return True
