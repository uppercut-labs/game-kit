# uppercut-godot-render

Renders a Godot scene offline with Movie Maker and checks what came out before anyone watches it: it trims the frames to the run, checks the frame count, the size and that the first and last frames are not blank, refuses a log with script errors or a renderer that is not Forward+, writes a SHA-256 for every frame, encodes bt709 H.264 and probes the result, makes a contact sheet and writes a JSON manifest. Standard library only; Godot, FFmpeg and FFprobe are run as programs.

The package knows no game. What the game says to it is a line or two in Godot's log (below), and everything else (the project, the flags, the frame count, the size) is the plan you pass in.

```sh
pip install uppercut-godot-render
```

You need Godot 4 (Movie Maker, `--write-movie`), `ffmpeg` and `ffprobe` on `PATH`. On Linux with no display the render runs under `xvfb-run`.

## Python

```python
from pathlib import Path
from uppercut_godot_render import RenderError, RenderPlan, find_godot, render, run_start_frame

plan = RenderPlan(
    godot=find_godot() or "godot",
    project=Path("game"),                # --path
    output=Path("out/clip"),             # must be empty or absent
    fps=60,
    size=(1920, 1080),
    frame_count=210,                     # footage frames the run has
    start_frame=run_start_frame,         # reads the log: which Movie Maker frame is footage frame 0
    game_arguments=("--look=hd", "--out=out/clip"),   # the game's own flags, after "--"
    audio=True,                          # trim frame.wav and encode it
)
try:
    result = render(plan)
except RenderError as error:
    raise SystemExit(f"FAIL: {error} (partial output in {plan.output})")
print(result.output / "footage.mp4", result.manifest["outputs"])
```

The output folder then holds `godot.log`, `frames/frame00000000.png` and on, `frames.sha256`, `audio.wav` (with `audio=True`), `footage.mp4`, `contact_sheet.png` and `manifest.json`. Nothing is deleted when a check fails, so the frames, the log and Godot's raw output are there to look at.

## The log lines the game writes

Movie Maker numbers its frames from 0 from the moment the engine starts, so the first frames of a run are preroll. The game says which frame its run starts on, and the footage is the frames from there.

| Line | Read by | Meaning |
| --- | --- | --- |
| `RUN_START_FRAME <n>` (exactly once) | `run_start_frame(log)` | the process frame of the game's tick 0; Movie Maker's frame index is the same number, and footage frame `i` is Movie Maker frame `n + i` |
| `SHOT_FRAME <f> COUNTER <k>` (first one counts) | `shot_start_frame(log, expected_first_shot=None)` | footage shot frame `f` is Movie Maker frame `k`; the first line's `f` must equal `expected_first_shot` when it is given |

`RenderPlan.start_frame` is any function from the log text to the index, so a game with its own line can use `find_marker(log, pattern)` or its own parser. The first project to use this measured, on Godot 4.7.2 with a game that starts its run deferred from `_ready`, that tick 0 lands in Movie Maker frame 1 at 60 fps but frame 0 at 30 or 24 fps, and that the tick that ends the run can share a frame with the one before it, which is why the game says where it starts rather than the renderer guessing.

## What a render checks

- `ffmpeg` and `ffprobe` are on `PATH`; the output folder is empty; no `override.cfg` is already in the project (a killed render leaves one behind and every later render would then run at its size without saying so).
- The log has no `SCRIPT ERROR`, `SHADER ERROR` or `Parse Error` (Godot can exit 0 after a script fails to load; the markers are a parameter) and has a renderer line `Vulkan`, `Metal` or `D3D12 ... - Forward+ - Using Device` (Godot falls back to OpenGL silently when no Vulkan driver exists).
- Movie Maker wrote enough frames; the first and last footage frame are the plan's size and not blank (the standard deviation of a 64x36 greyscale thumbnail is at least `min_deviation`, 2.0 by default; a deliberately dark scene can lower it).
- The encode is constant-rate, the plan's size, has exactly `frame_count` frames and is tagged bt709 in matrix, primaries and transfer (`ffprobe -count_frames`).
- With `audio=True`, `frame.wav` is cut to exactly the run's samples (the sample rate must be a multiple of the frame rate) and the footage has one audio stream; without it, none.

`RenderPlan(check_run=...)` is called with Godot's exit code and log before anything is trimmed, for a check of your own, such as reading the game's own result file.

## Sizing the frames

Movie Maker records at the project's viewport size, not the window's: `--resolution` is ignored. A clip of another size needs an `override.cfg` in the project for the length of the render. `RenderPlan(write_override=True)` writes one with the plan's size (and stretch mode `viewport`, so a screen shorter than the frame cannot letterbox it) and deletes it afterwards, even on an error or an interrupt. `viewport_override(path, size)` is the same context manager on its own.

## Other pieces

All the steps are functions you can call yourself: `movie_arguments`, `build_command`, `run_godot`, `trim_frames`, `verify_frames`, `write_frame_hashes`, `compare_frame_hashes` (a determinism check: render twice, compare the two `frames.sha256`), `Media.encode`, `Media.verify_footage`, `Media.contact_sheet`, `trim_audio`, `x264_arguments` and the `renderer_line` and `require_*` log checks. `Media(run=..., which=...)` and the `runner` argument of `render` stand in for FFmpeg and Godot in tests.

A render that has to run Godot another way (for example on a Windows desktop nobody sees) passes `runner=`: a function from the command line to a `GodotRun(exit_code, output)`.

## Notes

- `RenderPlan` defaults to constant quality 16 at preset `medium`; `crf` and `preset` are parameters.
- The tests need neither Godot nor a GPU: they stand in for Godot and FFmpeg, and a second group runs the pipeline with the real `ffmpeg` and `ffprobe` when they are installed (and is skipped when they are not).
- Nothing in the manifest depends on the clock (no time stamps or render times); add them through `manifest_extra` if you want them.

## License

MIT
