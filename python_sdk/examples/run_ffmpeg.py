"""
Example: Run FFmpeg with Python bridge plugin from Python.

This script demonstrates how to launch FFmpeg as a subprocess
with the Python bridge plugin.

Usage:
    python run_ffmpeg.py --input input.mp4 --output output.mp4 --ksize 21
    python run_ffmpeg.py  # uses lavfi test source
"""

import argparse
import os
import subprocess
import sys


def find_plugin_lib(name="python_bridge_plugin"):
    """Search for the compiled bridge plugin shared library."""
    # Common build directories
    search_dirs = [
        os.path.join(os.path.dirname(__file__), "..", "..", "build", "src"),
        os.path.join(os.path.dirname(__file__), "..", "..", "build"),
        "/usr/local/lib",
        "/usr/lib",
    ]

    extensions = [".so", ".dylib", ".dll"]

    for d in search_dirs:
        for ext in extensions:
            path = os.path.join(d, f"lib{name}{ext}")
            if os.path.isfile(path):
                return os.path.abspath(path)

    return None


def main():
    parser = argparse.ArgumentParser(
        description="Run FFmpeg with Python bridge plugin")
    parser.add_argument("--input", "-i", default=None,
                        help="Input video file (default: lavfi testsrc)")
    parser.add_argument("--output", "-o", default="output.mp4",
                        help="Output video file")
    parser.add_argument("--ffmpeg", default="ffmpeg",
                        help="Path to ffmpeg binary")
    parser.add_argument("--plugin-lib", default=None,
                        help="Path to libpython_bridge_plugin.so")
    parser.add_argument("--script", default=None,
                        help="Path to Python plugin script")
    parser.add_argument("--plugin-class", default="BlurPlugin",
                        help="Python class name")
    parser.add_argument("--ksize", type=int, default=15,
                        help="Blur kernel size")
    parser.add_argument("--duration", type=float, default=3.0,
                        help="Test source duration in seconds")
    args = parser.parse_args()

    # Find plugin library
    plugin_lib = args.plugin_lib or find_plugin_lib()
    if plugin_lib is None:
        print("ERROR: Could not find libpython_bridge_plugin.so")
        print("Build the project first: cmake --build build")
        sys.exit(1)

    # Default script path
    if args.script is None:
        args.script = os.path.join(
            os.path.dirname(__file__), "blur_plugin.py")

    # Build FFmpeg command
    params = f"script={args.script}&class={args.plugin_class}&ksize={args.ksize}"

    if args.input:
        input_args = ["-i", args.input]
    else:
        input_args = [
            "-f", "lavfi", "-i",
            f"testsrc=duration={args.duration}:size=640x480:rate=30"
        ]

    cmd = [
        args.ffmpeg, "-hide_banner", "-y",
        *input_args,
        "-vf", f"oc_plugin=plugin={plugin_lib}:params={params}",
        args.output,
    ]

    print("Running:", " ".join(cmd))
    result = subprocess.run(cmd)
    sys.exit(result.returncode)


if __name__ == "__main__":
    main()
