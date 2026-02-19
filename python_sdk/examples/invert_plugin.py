"""
Example: Invert Colors Plugin using Python + OpenCV

Demonstrates a simple ProcessPlugin that inverts frame colors.
Useful for testing multi-plugin chains.

Parameters (via FFmpeg params= option):
    (none)

Usage:
    ffmpeg -i input.mp4 \
        -vf "oc_plugin=plugin=libpython_bridge_plugin.so:\
             params=script=invert_plugin.py&class=InvertPlugin" \
        output.mp4
"""

import numpy as np

import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from quink_plugin import ProcessPluginBase, ProcessResult


class InvertPlugin(ProcessPluginBase):
    """Invert colors of video frames (255 - pixel)."""

    def init(self, params: str, nb_inputs: int, nb_outputs: int) -> bool:
        if nb_inputs != 1 or nb_outputs != 1:
            return False
        return True

    def process(self, inputs, outputs):
        if not inputs or not outputs:
            return ProcessResult.Error

        np.copyto(outputs[0], 255 - inputs[0])
        return ProcessResult.Ok

    def flush(self, outputs):
        return False
