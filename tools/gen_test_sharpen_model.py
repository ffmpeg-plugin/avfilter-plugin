#!/usr/bin/env python3
"""
Generate a tiny ONNX sharpening model for testing.

The model performs Laplacian-based sharpening using a 3x3 depthwise convolution:
  output = clip(conv(input, sharpen_kernel), 0, 1)

Input:  [1, 64, 64, 3] float32 (NHWC, values in [0, 1])
Output: [1, 64, 64, 3] float32 (NHWC, values in [0, 1])

This model has no trainable weights, only a fixed sharpening kernel.
It is intended for unit testing the ONNX inference plugin pipeline.
"""

import numpy as np
import onnx
from onnx import helper, TensorProto, numpy_helper
import sys

H, W = 64, 64
strength = 3.0

kernel = np.array([
    [0, -strength, 0],
    [-strength, 1 + 4*strength, -strength],
    [0, -strength, 0]
], dtype=np.float32)

conv_kernel = np.zeros((3, 1, 3, 3), dtype=np.float32)
for c in range(3):
    conv_kernel[c, 0] = kernel

conv_kernel_init = numpy_helper.from_array(conv_kernel, name="conv_kernel")

input_node = helper.make_tensor_value_info("input", TensorProto.FLOAT, [1, H, W, 3])
output_node = helper.make_tensor_value_info("output", TensorProto.FLOAT, [1, H, W, 3])

transpose_in = helper.make_node("Transpose", ["input"], ["nchw_in"], perm=[0, 3, 1, 2])

pad_values = numpy_helper.from_array(
    np.array([0, 0, 1, 1, 0, 0, 1, 1], dtype=np.int64), name="pad_values")
pad_node = helper.make_node("Pad", ["nchw_in", "pad_values"], ["padded"], mode="reflect")

conv_node = helper.make_node("Conv", ["padded", "conv_kernel"], ["nchw_out"],
                              group=3, kernel_shape=[3, 3])

clip_min = numpy_helper.from_array(np.array(0.0, dtype=np.float32), name="clip_min")
clip_max = numpy_helper.from_array(np.array(1.0, dtype=np.float32), name="clip_max")
clip_node = helper.make_node("Clip", ["nchw_out", "clip_min", "clip_max"], ["nchw_clipped"])

transpose_out = helper.make_node("Transpose", ["nchw_clipped"], ["output"], perm=[0, 2, 3, 1])

graph = helper.make_graph(
    [transpose_in, pad_node, conv_node, clip_node, transpose_out],
    "simple_sharpen",
    [input_node],
    [output_node],
    initializer=[conv_kernel_init, pad_values, clip_min, clip_max]
)

model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 13)])
model.ir_version = 7
onnx.checker.check_model(model)

out_path = sys.argv[1] if len(sys.argv) > 1 else "test_sharpen_model.onnx"
onnx.save(model, out_path)
print(f"Saved: {out_path}")
