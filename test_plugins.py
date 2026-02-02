#!/usr/bin/env python3
"""
FFmpeg OpenCV Plugin Test Script
Uses lavfi input sources for testing without requiring external video files

Usage Scenarios:
  1. Test in FFmpeg build directory (test ffmpeg's oc_plugin filter loading):
     FFMPEG_BIN=/path/to/ffmpeg/build/ffmpeg python test_plugins.py

  2. Test in project directory (test compiled plugins):
     python test_plugins.py

Environment Variables:
  FFMPEG_BIN   - Path to ffmpeg binary (default: ffmpeg in PATH)
  PLUGIN_DIR   - Directory containing plugins (default: ./build/src)
  OUTPUT_DIR   - Directory for output files (default: ./build/test_output)
"""

import os
import sys
import platform
import subprocess
import argparse
import re
import shutil
from pathlib import Path


def detect_plugin_ext():
    """Auto-detect OS and return plugin extension."""
    system = platform.system()
    if system == "Linux":
        return ".so"
    elif system == "Darwin":
        return ".dylib"
    elif system == "Windows":
        return ".dll"
    else:
        # Check for MSYS/Cygwin/MinGW via uname
        try:
            uname = subprocess.check_output(["uname", "-s"], stderr=subprocess.DEVNULL).decode().strip()
            if any(x in uname for x in ["MINGW", "MSYS", "CYGWIN"]):
                return ".dll"
        except:
            pass
        return ".so"  # Default to Linux

def get_uname():
    """Get uname -s output for path conversion detection."""
    try:
        return subprocess.check_output(["uname", "-s"], stderr=subprocess.DEVNULL).decode().strip()
    except:
        return platform.system()

def convert_path(path: str) -> str:
    r"""
    Convert paths for ffmpeg compatibility.
    - MSYS/Cygwin: /c/Users/... -> C:/Users/...
    - Windows: C:\\Users\\... -> C:/Users/... (use forward slashes)
    This is needed because ffmpeg filter parser treats backslash as escape character.
    """
    uname = get_uname()

    if "MINGW" in uname or "MSYS" in uname:
        # Convert /c/Users/... to C:/Users/...
        match = re.match(r'^/([a-zA-Z])/(.*)', path)
        if match:
            drive = match.group(1).upper()
            rest = match.group(2)
            path = f"{drive}:/{rest}"
    elif "CYGWIN" in uname:
        # Try cygpath first
        try:
            result = subprocess.check_output(["cygpath", "-m", path], stderr=subprocess.DEVNULL)
            path = result.decode().strip()
        except:
            # Fallback: Convert /cygdrive/c/... to C:/...
            match = re.match(r'^/cygdrive/([a-zA-Z])/(.*)', path)
            if match:
                drive = match.group(1).upper()
                rest = match.group(2)
                path = f"{drive}:/{rest}"
    else:
        # Handle regular Windows paths
        # Convert backslashes to forward slashes
        path = path.replace('\\', '/')

        # If path starts with drive letter like C:/, ensure it's properly formatted
        if len(path) >= 2 and path[1] == ':' and path[0].isalpha():
            # Already in correct format C:/...
            pass
        elif path.startswith('/') and not path.startswith('//'):
            # Handle Unix-style paths that might be misinterpreted
            # If it's a full path starting with /, check if it should be converted
            pass

    # Always convert backslashes to forward slashes for ffmpeg filter compatibility
    # FFmpeg filter parser treats backslash as escape character
    return path.replace('\\', '/')


def plugin_path(plugin_dir: str, plugin_name: str, plugin_ext: str) -> str:
    """Get full plugin path with Windows path conversion.
    Returns the path in a format compatible with ffmpeg filter parser.
    """
    path = os.path.join(plugin_dir, f"lib{plugin_name}{plugin_ext}")

    # Try using relative path instead of absolute path
    # FFmpeg might have issues with absolute paths containing special characters

    # Get current working directory
    cwd = os.getcwd()

    # Try to make path relative to current directory
    try:
        relative_path = os.path.relpath(path, cwd)
        # Use forward slashes for consistency
        relative_path = relative_path.replace('\\', '/')

        # Check if the relative path is reasonable (not going up too many levels)
        if not relative_path.startswith('..') or relative_path.count('../') <= 2:
            return relative_path
    except:
        pass

    # Fallback to absolute path with proper formatting
    converted = convert_path(path)

    # Clean up the path - remove any existing quotes
    converted = converted.strip("'\"")

    # Ensure forward slashes are used consistently
    converted = converted.replace('\\', '/')

    # For Windows paths starting with drive letter, ensure proper format
    if len(converted) >= 2 and converted[1] == ':' and converted[0].isalpha():
        # Windows absolute path like C:/Users/... - try without quotes first
        pass

    return converted

def check_plugin(plugin_dir: str, plugin_name: str, plugin_ext: str) -> bool:
    """Check if plugin exists."""
    raw_path = os.path.join(plugin_dir, f"lib{plugin_name}{plugin_ext}")
    converted = convert_path(raw_path)
    if os.path.isfile(raw_path):
        print(f"[OK] Found: {converted}")
        return True
    else:
        print(f"[SKIP] Not found: {converted}")
        return False

def run_ffmpeg(ffmpeg_bin: str, args: list) -> bool:
    """Run ffmpeg with given arguments, return True if successful."""
    # Add -hide_banner to reduce output, -loglevel to control verbosity
    # Use -nocolor (via env AV_LOG_FORCE_NOCOLOR) to disable color output
    cmd = [ffmpeg_bin, "-hide_banner"] + args

    # Print the command for debugging
    print(f"Command: {' '.join(cmd)}")
    print()

    try:
        env = os.environ.copy()
        env["AV_LOG_FORCE_NOCOLOR"] = "1"  # Disable ffmpeg color output
        result = subprocess.run(cmd, check=False, env=env)
        return result.returncode == 0
    except Exception as e:
        print(f"Error running ffmpeg: {e}")
        return False

def get_ffmpeg_version(ffmpeg_bin: str) -> str:
    """Get ffmpeg version string."""
    try:
        result = subprocess.run(
            [ffmpeg_bin, "-version"],
            capture_output=True,
            text=True,
            check=False
        )
        return result.stdout.split('\n')[0] if result.stdout else "Unknown"
    except:
        return "Unknown"

def main():
    parser = argparse.ArgumentParser(
        description="FFmpeg OpenCV Plugin Test Script",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Test in project directory (uses system ffmpeg)
  python test_plugins.py

  # Test with custom ffmpeg build
  FFMPEG_BIN=/path/to/ffmpeg/build/ffmpeg python test_plugins.py

  # Test in FFmpeg build directory
  cd /path/to/ffmpeg/build
  FFMPEG_BIN=./ffmpeg PLUGIN_DIR=/path/to/plugins python test_plugins.py

  # Use command line arguments
  python test_plugins.py -f /path/to/ffmpeg -p /path/to/plugins
"""
    )
    parser.add_argument("-f", "--ffmpeg", help="Path to ffmpeg binary")
    parser.add_argument("-p", "--plugin-dir", help="Plugin directory")
    parser.add_argument("-o", "--output-dir", help="Output directory")
    args = parser.parse_args()

    # Configuration with priority: CLI args > env vars > defaults
    plugin_ext = detect_plugin_ext()
    ffmpeg_bin = args.ffmpeg or os.environ.get("FFMPEG_BIN", "ffmpeg")
    plugin_dir = args.plugin_dir or os.environ.get("PLUGIN_DIR", "./build/src")
    output_dir = args.output_dir or os.environ.get("OUTPUT_DIR", "./build/test_output")

    # Get script directory for resolving relative paths
    script_dir = Path(__file__).parent.resolve()

    # If plugin_dir is relative and doesn't exist, try relative to script directory
    if not os.path.isabs(plugin_dir) and not os.path.isdir(plugin_dir):
        alt_path = script_dir / plugin_dir
        if alt_path.is_dir():
            plugin_dir = str(alt_path)

    # Convert to absolute path
    try:
        plugin_dir = str(Path(plugin_dir).resolve())
    except Exception:
        print(f"Error: Plugin directory not found: {plugin_dir}")
        print("Please build the plugins first or specify PLUGIN_DIR")
        sys.exit(1)

    if not os.path.isdir(plugin_dir):
        print(f"Error: Plugin directory not found: {plugin_dir}")
        print("Please build the plugins first or specify PLUGIN_DIR")
        sys.exit(1)

    # Print configuration
    print("=" * 40)
    print("FFmpeg OpenCV Plugin Test Script")
    print("=" * 40)
    print(f"Detected OS: {get_uname()}")
    print(f"Plugin extension: {plugin_ext}")
    print(f"FFmpeg binary: {ffmpeg_bin}")
    print(f"Plugin directory: {plugin_dir}")
    print(f"Output directory: {output_dir}")
    print("=" * 40)

    # Check ffmpeg binary
    try:
        subprocess.run([ffmpeg_bin, "-version"], capture_output=True, check=True)
    except (subprocess.CalledProcessError, FileNotFoundError):
        print(f"Error: ffmpeg not found: {ffmpeg_bin}")
        sys.exit(1)

    # Show ffmpeg version
    print()
    print("FFmpeg version:")
    print(get_ffmpeg_version(ffmpeg_bin))
    print()

    # Create output directory
    os.makedirs(output_dir, exist_ok=True)

    # Copy plugins and dependencies to ffmpeg directory for testing
    ffmpeg_dir = os.path.dirname(ffmpeg_bin)
    if os.path.isdir(ffmpeg_dir):
        print(f"Copying plugins and dependencies to FFmpeg directory: {ffmpeg_dir}")

        # Copy OpenCV and zlib dependencies
        for dep_file in ["opencv_core4.dll", "opencv_imgproc4.dll", "zlib1.dll"]:
            src_path = os.path.join(plugin_dir, dep_file)
            dst_path = os.path.join(ffmpeg_dir, dep_file)
            if os.path.isfile(src_path):
                try:
                    shutil.copy2(src_path, dst_path)
                    print(f"  Copied: {dep_file}")
                except Exception as e:
                    print(f"  Warning: Failed to copy {dep_file}: {e}")

        # Copy plugin files
        for plugin_name in ["blur_plugin", "avgframes_plugin", "split_plugin", "blend_plugin", "detect_plugin"]:
            plugin_file = f"lib{plugin_name}{plugin_ext}"
            src_path = os.path.join(plugin_dir, plugin_file)
            dst_path = os.path.join(ffmpeg_dir, plugin_file)
            if os.path.isfile(src_path):
                try:
                    shutil.copy2(src_path, dst_path)
                    print(f"  Copied: {plugin_file}")
                except Exception as e:
                    print(f"  Warning: Failed to copy {plugin_file}: {e}")
        print()

    # Test parameters
    DURATION = 3
    WIDTH = 640
    HEIGHT = 480
    FPS = 30

    print(f"Testing plugins with lavfi input ({WIDTH}x{HEIGHT}, {DURATION}s, {FPS}fps)...")
    print()

    # Track test results
    passed = 0
    failed = 0
    skipped = 0

    # Helper to get plugin path (now relative to ffmpeg directory)
    def get_plugin(name):
        return f"{plugin_dir}/lib{name}{plugin_ext}"

    # Test 1: Blur Plugin
    print("-" * 40)
    print("Test 1: Blur Plugin")
    print("-" * 40)
    if check_plugin(plugin_dir, "blur_plugin", plugin_ext):
        success = run_ffmpeg(ffmpeg_bin, [
            "-y", "-f", "lavfi",
            "-i", f"testsrc=duration={DURATION}:size={WIDTH}x{HEIGHT}:rate={FPS}",
            "-vf", f"oc_plugin=plugin={get_plugin('blur_plugin')}:params=ksize=15",
            f"{output_dir}/test_blur.mp4"
        ])
        if success:
            print(f"[PASS] Blur plugin test completed: {output_dir}/test_blur.mp4")
            passed += 1
        else:
            print("[FAIL] Blur plugin test failed")
            failed += 1
    else:
        skipped += 1

    # Test 2: Average Frames Plugin
    print()
    print("-" * 40)
    print("Test 2: Average Frames Plugin")
    print("-" * 40)
    if check_plugin(plugin_dir, "avgframes_plugin", plugin_ext):
        success = run_ffmpeg(ffmpeg_bin, [
            "-y", "-f", "lavfi",
            "-i", f"testsrc=duration={DURATION}:size={WIDTH}x{HEIGHT}:rate={FPS}",
            "-vf", f"oc_plugin=plugin={get_plugin('avgframes_plugin')}:params=frames=5",
            f"{output_dir}/test_avgframes.mp4"
        ])
        if success:
            print(f"[PASS] Average frames plugin test completed: {output_dir}/test_avgframes.mp4")
            passed += 1
        else:
            print("[FAIL] Average frames plugin test failed")
            failed += 1
    else:
        skipped += 1

    # Test 3: Split Plugin (1 input -> 3 outputs)
    print()
    print("-" * 40)
    print("Test 3: Split Plugin (3 outputs)")
    print("-" * 40)
    if check_plugin(plugin_dir, "split_plugin", plugin_ext):
        success = run_ffmpeg(ffmpeg_bin, [
            "-y", "-f", "lavfi",
            "-i", f"testsrc=duration={DURATION}:size={WIDTH}x{HEIGHT}:rate={FPS}",
            "-filter_complex", f"oc_plugin=plugin={get_plugin('split_plugin')}:outputs=3:params=outputs=3[out0][out1][out2]",
            "-map", "[out0]", f"{output_dir}/test_split_passthrough.mp4",
            "-map", "[out1]", f"{output_dir}/test_split_gray.mp4",
            "-map", "[out2]", f"{output_dir}/test_split_edges.mp4"
        ])
        if success:
            print("[PASS] Split plugin test completed:")
            print(f"       - {output_dir}/test_split_passthrough.mp4 (passthrough)")
            print(f"       - {output_dir}/test_split_gray.mp4 (grayscale)")
            print(f"       - {output_dir}/test_split_edges.mp4 (edges)")
            passed += 1
        else:
            print("[FAIL] Split plugin test failed")
            failed += 1
    else:
        skipped += 1

    # Test 4: Blend Plugin (2 inputs -> 1 output)
    print()
    print("-" * 40)
    print("Test 4: Blend Plugin (2 inputs)")
    print("-" * 40)
    if check_plugin(plugin_dir, "blend_plugin", plugin_ext):
        success = run_ffmpeg(ffmpeg_bin, [
            "-y",
            "-f", "lavfi", "-i", f"testsrc=duration={DURATION}:size={WIDTH}x{HEIGHT}:rate={FPS}",
            "-f", "lavfi", "-i", f"color=c=blue:duration={DURATION}:size={WIDTH}x{HEIGHT}:rate={FPS}",
            "-filter_complex", f"[0:v][1:v]oc_plugin=plugin={get_plugin('blend_plugin')}:inputs=2:params=alpha=0.5",
            f"{output_dir}/test_blend.mp4"
        ])
        if success:
            print(f"[PASS] Blend plugin test completed: {output_dir}/test_blend.mp4")
            passed += 1
        else:
            print("[FAIL] Blend plugin test failed")
            failed += 1
    else:
        skipped += 1

    # Test 5: Detect Plugin (immediate mode)
    print()
    print("-" * 40)
    print("Test 5: Detect Plugin (immediate mode)")
    print("-" * 40)
    if check_plugin(plugin_dir, "detect_plugin", plugin_ext):
        # Use color test source with colored regions for detection
        success = run_ffmpeg(ffmpeg_bin, [
            "-y", "-f", "lavfi",
            "-i", f"smptebars=duration={DURATION}:size={WIDTH}x{HEIGHT}:rate={FPS}",
            "-vf", f"oc_plugin=plugin={get_plugin('detect_plugin')}",
            f"{output_dir}/test_detect_immediate.mp4"
        ])
        if success:
            print(f"[PASS] Detect plugin (immediate) test completed: {output_dir}/test_detect_immediate.mp4")
            passed += 1
        else:
            print("[FAIL] Detect plugin (immediate) test failed")
            failed += 1
    else:
        skipped += 1

    # Test 6: Detect Plugin (delayed mode with buffering)
    print()
    print("-" * 40)
    print("Test 6: Detect Plugin (delayed mode, delay=2)")
    print("-" * 40)
    if check_plugin(plugin_dir, "detect_plugin", plugin_ext):
        success = run_ffmpeg(ffmpeg_bin, [
            "-y", "-f", "lavfi",
            "-i", f"smptebars=duration={DURATION}:size={WIDTH}x{HEIGHT}:rate={FPS}",
            "-vf", f"oc_plugin=plugin={get_plugin('detect_plugin')}:params=delay=2",
            f"{output_dir}/test_detect_delayed.mp4"
        ])
        if success:
            print(f"[PASS] Detect plugin (delayed) test completed: {output_dir}/test_detect_delayed.mp4")
            passed += 1
        else:
            print("[FAIL] Detect plugin (delayed) test failed")
            failed += 1
    else:
        skipped += 1

    # Test 7: Detect Plugin - Show detection results (no encoding)
    print()
    print("-" * 40)
    print("Test 7: Detect Plugin - Show Detection Results")
    print("-" * 40)
    if check_plugin(plugin_dir, "detect_plugin", plugin_ext):
        print("Running detection on SMPTE color bars (showinfo filter)...")
        print()
        # Use showinfo filter to display detection bbox side data
        cmd = [
            ffmpeg_bin, "-hide_banner",
            "-f", "lavfi",
            "-i", f"smptebars=duration=0.2:size={WIDTH}x{HEIGHT}:rate=5",
            "-vf", f"oc_plugin=plugin={get_plugin('detect_plugin')},showinfo",
            "-f", "null", "-"
        ]
        print(f"Command: {' '.join(cmd)}")
        print()
        try:
            env = os.environ.copy()
            env["AV_LOG_FORCE_NOCOLOR"] = "1"
            result = subprocess.run(cmd, capture_output=True, text=True, env=env)
            output = result.stderr + result.stdout
            
            # Parse detection results from showinfo output
            bbox_found = False
            detection_lines = []
            frame_info = None
            
            for line in output.split('\n'):
                # Capture frame info line
                if 'n:' in line and 'pts:' in line and 'fmt:' in line:
                    frame_info = line.strip()
                # Capture side data header
                elif 'side data - Bounding boxes' in line:
                    bbox_found = True
                    detection_lines.append(line.strip())
                # Capture individual detection boxes
                elif 'index:' in line and 'region:' in line and 'label:' in line:
                    detection_lines.append(line.strip())
            
            if bbox_found and detection_lines:
                print("=" * 50)
                print("DETECTION RESULTS")
                print("=" * 50)
                if frame_info:
                    # Extract key info from frame_info
                    print(f"Frame: {frame_info.split(']')[-1].strip()[:80]}...")
                print()
                print("Detected objects:")
                print("-" * 50)
                for line in detection_lines:
                    if 'index:' in line:
                        # Parse: index: 0, region: (460, 0) -> (552, 320), label: red, confidence: 1000000/1000000.
                        # Extract and format nicely
                        parts = line.split(',')
                        idx = parts[0].split(':')[-1].strip() if len(parts) > 0 else '?'
                        
                        # Extract region coordinates
                        region_match = re.search(r'region:\s*\((\d+),\s*(\d+)\)\s*->\s*\((\d+),\s*(\d+)\)', line)
                        if region_match:
                            x1, y1, x2, y2 = region_match.groups()
                            w = int(x2) - int(x1)
                            h = int(y2) - int(y1)
                            region_str = f"({x1},{y1}) {w}x{h}"
                        else:
                            region_str = "?"
                        
                        # Extract label
                        label_match = re.search(r'label:\s*(\w+)', line)
                        label = label_match.group(1) if label_match else '?'
                        
                        # Extract confidence
                        conf_match = re.search(r'confidence:\s*(\d+)/(\d+)', line)
                        if conf_match:
                            conf = int(conf_match.group(1)) / int(conf_match.group(2))
                            conf_str = f"{conf*100:.1f}%"
                        else:
                            conf_str = "?"
                        
                        print(f"  [{idx}] {label:8s} @ {region_str:20s} conf: {conf_str}")
                    else:
                        print(f"  {line}")
                print("-" * 50)
                print()
                print("[PASS] Detection plugin produces detailed bbox results")
                passed += 1
            else:
                # Check if plugin at least ran successfully
                if result.returncode == 0:
                    print("Plugin ran but no detection boxes found in this frame.")
                    print("[PASS] Detection plugin executed successfully")
                    passed += 1
                else:
                    print("[FAIL] Detection plugin test failed")
                    print(f"Return code: {result.returncode}")
                    failed += 1
        except Exception as e:
            print(f"Error: {e}")
            print("[FAIL] Detection plugin test failed")
            failed += 1
    else:
        skipped += 1

    # Print summary
    print()
    print("=" * 40)
    print("Test Summary")
    print("=" * 40)
    print(f"Passed:  {passed}")
    print(f"Failed:  {failed}")
    print(f"Skipped: {skipped}")
    print("=" * 40)
    print(f"Output files are in: {output_dir}")
    print("=" * 40)

    # List output files
    print()
    print("Generated files:")
    try:
        mp4_files = list(Path(output_dir).glob("*.mp4"))
        if mp4_files:
            for f in mp4_files:
                size = f.stat().st_size
                size_str = f"{size / 1024:.1f}K" if size < 1024 * 1024 else f"{size / 1024 / 1024:.1f}M"
                print(f"  {f.name} ({size_str})")
        else:
            print("No output files generated")
    except Exception:
        print("No output files generated")

    # Exit with error if any tests failed
    if failed > 0:
        sys.exit(1)


if __name__ == "__main__":
    main()
