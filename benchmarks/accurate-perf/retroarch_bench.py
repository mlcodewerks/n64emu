"""Run one isolated Windows RetroArch Accurate/LLE benchmark with perf_proxy.dll."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--retroarch", type=Path, required=True)
    parser.add_argument("--core", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--proxy", type=Path, default=Path(__file__).with_name("perf_proxy.dll"))
    parser.add_argument("--renderer", choices=["glrdp", "gl2", "parallel-rdp", "softrdp"], default="glrdp")
    parser.add_argument("--frames", type=int, default=2400)
    parser.add_argument("--port", type=int, default=55440)
    parser.add_argument("--compare", type=Path, help="Compare checkpoints, image and savestate with a previous output directory")
    parser.add_argument("--sync-checkpoints", action="store_true", help="Materialize GPU RAM and hash full states at every checkpoint; use for equivalence, separately from performance runs")
    args = parser.parse_args()
    if args.frames <= 480:
        parser.error("--frames must exceed the 480-frame warmup")
    for name in ["retroarch", "core", "rom", "proxy"]:
        path = getattr(args, name).resolve()
        if not path.is_file():
            parser.error(f"{name} does not exist: {path}")
        setattr(args, name, path)
    folder = args.output.resolve()
    if (folder / "result.json").exists():
        parser.error("choose a new output directory to preserve the previous measurement")
    for name in ["config/n64emu", "saves", "states", "screenshots", "cache"]:
        (folder / name).mkdir(parents=True, exist_ok=True)
    options = {
        "wtfn64_renderer": args.renderer,
        "wtfn64_timing": "accurate",
        "wtfn64_hle": "Off",
        "wtfn64_glrdp_shader_specialization": "disabled",
        "wtfn64_angrylion_upscale": "1",
        "wtfn64_parallel_upscale": "1",
        "wtfn64_glrdp_upscale": "1",
        "wtfn64_legacy_upscale": "1",
    }
    option_text = "".join(f'{key} = "{value}"\n' for key, value in options.items())
    for path in [folder / "core.opt", folder / "config/n64emu/n64emu.opt"]:
        path.write_text(option_text)
    config = {
        "config_save_on_exit": "false",
        "video_driver": "glcore" if args.renderer == "glrdp" else "gl" if args.renderer == "gl2" else "vulkan",
        "audio_driver": "null", "audio_enable": "false",
        "video_fullscreen": "false", "video_windowed_fullscreen": "false",
        "video_window_auto": "false", "video_window_custom_size_enable": "true",
        "video_window_width": "640", "video_window_height": "480", "video_scale": "1",
        "video_window_save_positions": "false", "video_threaded": "false", "video_vsync": "false",
        "video_shader_enable": "false", "video_hard_sync": "false", "video_frame_delay": "0",
        "video_gpu_record": "false", "video_swap_interval": "1",
        "fastforward_ratio": "0.0", "fastforward_frameskip": "false", "pause_nonactive": "false",
        "menu_enable_widgets": "false", "menu_driver": "rgui", "menu_show_start_screen": "false",
        "notification_show_autoconfig": "false", "network_cmd_enable": "true", "network_cmd_port": str(args.port),
        "core_options_path": (folder / "core.opt").as_posix(),
        "core_options_directory": (folder / "config").as_posix(),
        "rgui_config_directory": (folder / "config").as_posix(),
        "savefile_directory": (folder / "saves").as_posix(),
        "savestate_directory": (folder / "states").as_posix(),
        "screenshot_directory": (folder / "screenshots").as_posix(),
        "cache_directory": (folder / "cache").as_posix(),
        "system_directory": (args.retroarch.parent / "system").as_posix(),
        "assets_directory": (args.retroarch.parent / "assets").as_posix(),
        "history_list_enable": "false", "content_runtime_log": "false", "content_runtime_log_aggregate": "false",
        "auto_overrides_enable": "false", "auto_remaps_enable": "false", "game_specific_options": "false",
        "savefiles_in_content_dir": "false", "savestates_in_content_dir": "false",
        "savestate_auto_save": "false", "savestate_auto_load": "false", "input_auto_game_focus": "false",
        "log_verbosity": "true",
    }
    config_path = folder / "retroarch.cfg"
    config_path.write_text("".join(f'{key} = "{value}"\n' for key, value in config.items()))
    env = dict(os.environ, N64EMU_TEST_CORE=str(args.core), N64EMU_PERF_STATE=str(folder / "final.state"),
               N64EMU_PERF_FRAMES=str(args.frames))
    # Benchmark the normal configuration, independent of an inherited diagnostic override.
    env.pop("N64EMU_CPU_HOST_FAULTS", None)
    env.pop("N64EMU_PERF_SYNC_CHECKPOINTS", None)
    if args.sync_checkpoints:
        env["N64EMU_PERF_SYNC_CHECKPOINTS"] = "1"
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    command = [str(args.retroarch), "-v", "-c", str(config_path), "-L", str(args.proxy),
               "-M", "noload-nosave", f"--max-frames={args.frames}", "--max-frames-ss",
               "--max-frames-ss-path=" + str(folder / "final.png"),
               "--log-file=" + str(folder / "retroarch.log"), str(args.rom)]
    started = time.perf_counter()
    with (folder / "stdout.log").open("w") as log:
        process = subprocess.Popen(command, cwd=folder, env=env, stdout=log, stderr=log, startupinfo=startup)
        print(f"RetroArch PID {process.pid}: {args.renderer}, {args.rom.name}", flush=True)
        time.sleep(3)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.sendto(b"FAST_FORWARD\n", ("127.0.0.1", args.port))
        try:
            status = process.wait(timeout=600)
        except subprocess.TimeoutExpired:
            process.kill()
            status = process.wait()
    output = (folder / "stdout.log").read_text(errors="replace")
    result = {
        "core": str(args.core), "core_sha256": hashlib.sha256(args.core.read_bytes()).hexdigest(),
        "renderer": args.renderer, "rom": args.rom.name, "requested_frames": args.frames,
        "synchronized_checkpoints": args.sync_checkpoints,
        "exit": status, "wall_s": time.perf_counter() - started,
        "perf": [line for line in output.splitlines() if line.startswith("PERF ")],
        "errors": [line for line in (folder / "retroarch.log").read_text(errors="replace").splitlines()
                   if "[libretro ERROR]" in line],
    }
    for name in ["final.png", "final.state"]:
        if (folder / name).exists():
            result[name + "_sha256"] = hashlib.sha256((folder / name).read_bytes()).hexdigest()
    if args.compare:
        previous = args.compare.resolve()
        traces = lambda path: [line for line in path.read_text(errors="replace").splitlines() if line.startswith("TRACE ")]
        current_trace = traces(folder / "stdout.log")
        reference_trace = traces(previous / "stdout.log")
        result["equivalence"] = {"ram_audio": bool(current_trace) and current_trace == reference_trace}
        if args.sync_checkpoints:
            state_traces = lambda path: [line for line in path.read_text(errors="replace").splitlines() if line.startswith("TRACE_STATE ")]
            current_states = state_traces(folder / "stdout.log")
            result["equivalence"]["state_checkpoints"] = len(current_states) == args.frames // 120 and current_states == state_traces(previous / "stdout.log")
        for name in ["final.png", "final.state"]:
            a, b = folder / name, previous / name
            result["equivalence"][name] = a.read_bytes() == b.read_bytes() if a.exists() and b.exists() else None
    (folder / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result), flush=True)
    complete = re.search(r"PERF frames=(\d+) ", output)
    if status or result["errors"] or not complete or int(complete[1]) != args.frames:
        raise SystemExit("RetroArch benchmark failed; inspect the private logs")
    if args.compare and not all(value is True for value in result["equivalence"].values()):
        raise SystemExit("One or more equivalence checks failed or could not be captured")


if __name__ == "__main__":
    main()
