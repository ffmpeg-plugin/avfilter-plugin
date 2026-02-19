"""
Example: Simple Color Detect Plugin using Python + OpenCV

Demonstrates a DetectPlugin that finds colored regions in video frames.

Parameters:
    (none)

Usage:
    ffmpeg -i input.mp4 \
        -vf "oc_plugin=plugin=libpython_detect_bridge_plugin.so:\
             params=script=detect_color_plugin.py&class=ColorDetectPlugin" \
        output.mp4
"""

import numpy as np

try:
    import cv2
except ImportError:
    cv2 = None

import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from quink_plugin import DetectPluginBase, ProcessResult


class ColorDetectPlugin(DetectPluginBase):
    """Detect colored regions in video frames."""

    def __init__(self):
        self.min_area = 500

    def init(self, params: str, nb_inputs: int, nb_outputs: int) -> bool:
        if params:
            for token in params.split("&"):
                if "=" in token:
                    key, value = token.split("=", 1)
                    if key == "min_area":
                        self.min_area = int(value)
        return True

    def detect(self, input_frame, output_frame):
        # Pass-through: copy input to output
        np.copyto(output_frame, input_frame)

        detections = {
            "boxes": [],
            "class_ids": [],
            "confidences": [],
            "labels": []
        }

        if cv2 is None:
            return (ProcessResult.Ok, detections)

        # Convert to HSV for color detection
        hsv = cv2.cvtColor(input_frame, cv2.COLOR_BGR2HSV)

        colors = [
            ("red",    0, np.array([0, 100, 100]),   np.array([10, 255, 255])),
            ("green",  1, np.array([35, 100, 100]),  np.array([85, 255, 255])),
            ("blue",   2, np.array([100, 100, 100]), np.array([130, 255, 255])),
        ]

        for name, class_id, lower, upper in colors:
            mask = cv2.inRange(hsv, lower, upper)
            contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL,
                                           cv2.CHAIN_APPROX_SIMPLE)

            for contour in contours:
                area = cv2.contourArea(contour)
                if area < self.min_area:
                    continue

                x, y, w, h = cv2.boundingRect(contour)
                confidence = min(1.0, area / 10000.0)

                detections["boxes"].append((x, y, w, h))
                detections["class_ids"].append(class_id)
                detections["confidences"].append(confidence)
                detections["labels"].append(name)

        return (ProcessResult.Ok, detections)

    def flush_detect(self, output_frame):
        return False
