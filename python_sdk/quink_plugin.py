"""
Quink OpenCV Plugin SDK for Python

Provides base classes for writing Python plugins that integrate with the
FFmpeg oc_plugin filter via the Python bridge plugin.

Usage:
    from quink_plugin import ProcessPluginBase, DetectPluginBase

See examples/ directory for usage examples.
"""

from enum import IntEnum
from typing import List, Dict, Tuple, Optional

import numpy as np


class ProcessResult(IntEnum):
    """Process result codes matching quink::ProcessResult."""
    Ok       = 0    # Success, output frame(s) produced
    TryAgain = 1    # Success, but output not ready yet (buffering)
    Error    = -1   # Processing error


class ProcessPluginBase:
    """
    Base class for Python process plugins.

    Subclass this and implement process() at minimum.
    The plugin is loaded by the C++ python_bridge_plugin which
    converts between cv::Mat and numpy arrays (zero-copy).

    Lifecycle:
        __init__() -> init() -> configure() -> process()* -> flush()* -> uninit()
    """

    def init(self, params: str, nb_inputs: int, nb_outputs: int) -> bool:
        """
        Initialize the plugin.

        Called once after construction with the user-specified parameters.

        Args:
            params:     Parameter string (key=value pairs separated by '&',
                        excluding 'script' and 'class' which are consumed by the bridge)
            nb_inputs:  Number of input pads (configured via FFmpeg AVOption)
            nb_outputs: Number of output pads (configured via FFmpeg AVOption)

        Returns:
            True on success, False on failure
        """
        return True

    def process(self, inputs: List[np.ndarray],
                outputs: List[np.ndarray]) -> int:
        """
        Process one set of input frames.

        Called for each frame (or set of frames for multi-input).
        Input arrays are READ-ONLY (zero-copy from FFmpeg).
        Output arrays are PRE-ALLOCATED (zero-copy to FFmpeg) — write into them.

        IMPORTANT:
            - Do NOT reallocate outputs (e.g., outputs[0] = new_array is WRONG)
            - Use np.copyto(outputs[0], result) to write results
            - Or use in-place operations on outputs[0]
            - Input arrays are always uint8 with shape (H, W, C) where C is 3 (BGR) or 4 (BGRA)

        DELAYED OUTPUT IS NOT SUPPORTED:
            Saving input arrays for later use is NOT allowed — the underlying
            memory is owned by FFmpeg and becomes invalid after process() returns.
            TryAgain may ONLY be used for frame-dropping (e.g., decimation),
            where no input data needs to be preserved.

        Args:
            inputs:  List of input images as numpy arrays (H, W, C), uint8, read-only
            outputs: List of pre-allocated output buffers as numpy arrays

        Returns:
            ProcessResult.Ok (0):       output ready
            ProcessResult.TryAgain (1): frame dropped, no output
            ProcessResult.Error (-1):   error
        """
        raise NotImplementedError("Subclass must implement process()")

    def flush(self, outputs: List[np.ndarray]) -> bool:
        """
        Flush is NOT supported for Python plugins.

        Python plugins cannot buffer frames because input numpy arrays become
        invalid after process() returns (the underlying memory is owned by
        FFmpeg). This method is never called by the bridge.

        Returns:
            Always False
        """
        return False

    def configure(self, input_configs: List[Dict],
                  output_configs: List[Dict]) -> bool:
        """
        Configure plugin dimensions.

        Called during filter graph configuration. Modify output_configs in-place
        to set output dimensions. By default, output dimensions match input.

        Each config dict contains:
            - width (int): frame width
            - height (int): frame height
            - cv_type (int): OpenCV type
            - pix_fmt (int): pixel format enum value
            - colorspace (int): ISO/IEC 23091-2 colorspace
            - limited_range (bool): whether color range is limited

        Args:
            input_configs:  List of input configurations (read-only)
            output_configs: List of output configurations (modify in-place)

        Returns:
            True on success
        """
        return True

    def uninit(self):
        """Cleanup resources. Called before destruction."""
        pass


class DetectPluginBase:
    """
    Base class for Python detect plugins.

    Subclass this and implement detect() at minimum.
    Detect plugins are always 1:1 (single-input, single-output).

    Lifecycle:
        __init__() -> init() -> detect()* -> flush_detect()* -> uninit()
    """

    def init(self, params: str, nb_inputs: int, nb_outputs: int) -> bool:
        """
        Initialize the plugin.

        Args:
            params:     Parameter string (key=value pairs separated by '&')
            nb_inputs:  Always 1 for detect plugins
            nb_outputs: Always 1 for detect plugins

        Returns:
            True on success
        """
        return True

    def detect(self, input_frame: np.ndarray,
               output_frame: np.ndarray) -> Tuple[int, Dict]:
        """
        Perform detection on input frame.

        For pass-through, copy input to output: np.copyto(output_frame, input_frame)

        Args:
            input_frame:  Input image as numpy array (H, W, C), read-only
            output_frame: Pre-allocated output buffer, write into it

        Returns:
            Tuple of (result_code, detections_dict):
                result_code: ProcessResult value (0=Ok, 1=TryAgain, -1=Error)
                detections_dict: {
                    "boxes": [(x, y, w, h), ...],
                    "class_ids": [int, ...],
                    "confidences": [float, ...],
                    "labels": [str, ...]          # optional
                }
        """
        raise NotImplementedError("Subclass must implement detect()")

    def flush_detect(self, output_frame: np.ndarray) -> Optional[Tuple[bool, Dict]]:
        """
        Flush buffered frames at end of stream.

        Returns:
            None or False if no more frames,
            or (True, detections_dict) if a frame was output
        """
        return False

    def uninit(self):
        """Cleanup resources."""
        pass
