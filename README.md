# ColorizingDMD ComfyUI Workflows

A plugin for the [PPUC Colorizing DMD](https://github.com/PPUC/PPUC-Colorizing-DMD) editor. It runs
colorized SD pictures (128x32) through a [ComfyUI](https://github.com/comfyanonymous/ComfyUI)
workflow and proposes the results for the HD plane (256x64). It works on frames, backgrounds
and sprites.

The workflow can be anything that turns pictures into pictures, for example an upscaler such as
SeedVR2 or a model that redraws in a style. ComfyUI runs on the same computer or on another one,
and the plugin talks to its HTTP API.

The plugin brings each result back to the HD plane's size and to RGB565 exactly, then proposes
it in the editor's review window. One Undo takes back what was applied.

Needs PPUC Colorizing DMD 3.99.30 or later (editor API version 3).

## Install

The plugin goes into the editor's folder for the plugins you install; ComfyUI runs on this or
another computer, see "The workflow" below.

1. Download the plugin for your editor from the
   [releases](https://github.com/PPUC/PPUC-Colorizing-DMD-ComfyUI-Plugin/releases), under
   `Assets`:

   | Editor package           | Plugin package                                        |
   | ------------------------ | ----------------------------------------------------- |
   | Windows                  | `PPUC-Colorizing-DMD-ComfyUI-Plugin-windows-x86_64-…` |
   | Linux x86_64 (AppImage)  | `PPUC-Colorizing-DMD-ComfyUI-Plugin-linux-x86_64-…`   |
   | Linux aarch64 (AppImage) | `PPUC-Colorizing-DMD-ComfyUI-Plugin-linux-aarch64-…`  |
   | macOS (Apple silicon)    | `PPUC-Colorizing-DMD-ComfyUI-Plugin-macos-arm64-…`    |

   The zip holds one folder, `ComfyUI`, with the example workflows in `examples`. Builds of
   every commit are under the repository's `Actions` tab, for testing; they need a GitHub login
   to download.
2. Put the folder `ComfyUI` into the editor's folder for the plugins you install.
   `Settings -> Plugins` names it, and `Open Folder` opens it:
   - Windows: `%APPDATA%\ppuc.org\PPUC Colorizing DMD\plugins`
   - macOS: `~/Library/Application Support/ppuc.org/PPUC Colorizing DMD/plugins`
   - Linux: `~/.local/share/ppuc.org/PPUC Colorizing DMD/plugins`

   The folder `ComfyUI`, with `plugin.cfg` in it, must lie directly in `plugins`. On macOS,
   remove the mark macOS puts on downloaded files, or the library does not load; in the
   Terminal:

       xattr -dr com.apple.quarantine ~/Library/Application\ Support/ppuc.org/PPUC\ Colorizing\ DMD/plugins/ComfyUI

3. Restart the editor and check `ComfyUI Workflows` under `Settings -> Plugins`. The `Plugins`
   menu then has the ComfyUI entries; see "Use" below. A plugin that does not load is named
   there with the reason, and `Help -> Plugin Log` has the details.

To update the plugin, replace the `ComfyUI` folder with the new one; the settings are kept.

For the `ColorizingDMD Load Clip` node, download `PPUC-Colorizing-DMD-ComfyUI-Node-…` from the
same release on the computer that runs ComfyUI, and put its folder `colorizingdmd` into
ComfyUI's `custom_nodes` (see "The ColorizingDMD Load Clip node").

## Parts

- `src/`: the plugin (C++, the editor API `plugin-api/ColorizingDMDPlugin.h` on the VPX
  plugin SDK).
  - `Pictures`: what is sent, and how a result becomes a picture: RGB565, the color modes and
    the write mask.
  - `Gather`: the pictures read from the editor, and the clips.
  - `Images`: PNG and resizing.
  - `Workflow`: the workflow file, its two titled nodes and the placeholders.
  - `ComfyClient`: ComfyUI's HTTP API.
  - `Run`: a run from upload to proposals.
  - `Plugin.cpp`: the entry points, the menu and the worker thread.
- `custom_nodes/colorizingdmd/`: a ComfyUI node, `ColorizingDMD Load Clip`, that loads several
  consecutive frames as one batch.
- `examples/`: two workflows that only enlarge the pictures (nearest, 4x), for trying the plugin
  without a GPU.
- `tests/`: the plugin's tests. They need neither the editor nor ComfyUI and run against a stub
  of ComfyUI's API. `custom_nodes/test_colorizingdmd.py` holds the node's tests.
- `third-party/`: cpp-httplib 0.18.3, nlohmann/json 3.11.3, stb_image 2.30 and stb_image_write
  1.16.

## Build

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCOLORIZINGDMD_DIR=/path/to/ColorizingDMD \
        -DVPINBALL_DIR=/path/to/vpinball \
        -DCOLORIZINGDMD_PLUGIN_DIR=/path/to/editor/build/vpx-plugins
    cmake --build build
    ctest --test-dir build --output-on-failure

The build puts the plugin's folder in `build/ComfyUI`: `plugin.cfg` and the library. With
`COLORIZINGDMD_PLUGIN_DIR` set, it is also copied into that folder, for example a development
editor's plugin folder or the folder for the plugins you install (see "Install").

CI (`.github/workflows/build.yml`) builds and tests the plugin for each editor package's
platform with warnings as errors, against the editor API and the VPX plugin SDK at the commits
it pins (`COLORIZINGDMD_SHA`, `VPINBALL_SHA`). Every run uploads the plugin packages and the
ComfyUI node as the run's artifacts.

## Release

1. Raise the version in `CMakeLists.txt` (`project(... VERSION x.y.z)`) and in `plugin.cfg`
   (`version`), in one commit.
2. Tag it `vx.y.z` and push the tag. CI checks the tag against both versions and collects the
   packages into a draft release.
3. Write the release notes on GitHub and publish the draft.

## The workflow

1. Build the workflow in ComfyUI and give two nodes these titles (right click the node, then
   Title):
   - `CDMD Input`: the node the pictures go into. Either:
     - a `Load Image` node, which takes one picture per run;
     - or the `ColorizingDMD Load Clip` node, which takes up to `Frames per clip` consecutive
       frames per run as one batch. Video models such as SeedVR2 keep the result steady from
       one frame to the next this way.
   - `CDMD Output`: the `Save Image` or `Preview Image` node whose pictures are the results. It
     must give one picture for each picture sent, in the same order.
2. Text inputs may hold placeholders, which the plugin fills in for each run. An input that is
   only a placeholder becomes a number, so a seed or a size can come from the plugin. The
   placeholders are:
   - `{seed}`: the seed from the plugin's settings.
   - `{width}` and `{height}`: the size the results go to, twice the pictures sent.
   - `{sdwidth}` and `{sdheight}`: the size of the pictures sent.
   - `{count}`: the number of pictures in the run.
3. Export it with `Export (API)` (on older ComfyUI versions, enable the developer mode
   setting first). The plugin reads this file. The plain workflow file is in another format
   and is refused.

The results may have any size. The plugin scales each down or up to exactly twice the picture
sent, each pixel the average of the area it covers. Then it brings the pixels to RGB565 with the
color mode chosen in the settings.

For SeedVR2, install a SeedVR2 node pack for ComfyUI, for example
[ComfyUI-SeedVR2_VideoUpscaler](https://github.com/numz/ComfyUI-SeedVR2_VideoUpscaler). Then
build the workflow from these nodes:

1. `ColorizingDMD Load Clip`, titled `CDMD Input`.
2. Optionally `Upscale Image By` (nearest-exact, 4), as SeedVR2 works on larger pictures than
   128x32.
3. The SeedVR2 upscaler node.
4. `Save Image`, titled `CDMD Output`.

Put `{seed}` into the upscaler's seed if it should come from the plugin.

### The ColorizingDMD Load Clip node

Copy or link `custom_nodes/colorizingdmd` into ComfyUI's `custom_nodes` folder and restart
ComfyUI. The node needs nothing beyond what ComfyUI has.

Its `images` input lists the uploaded files' names, one per line, as the plugin fills it in. It
gives two outputs:
- `images`: the batch.
- `masks`: 1 where a picture is transparent, as Load Image gives them.

## Use

1. Start ComfyUI. For another computer, start it with `--listen` or use an SSH tunnel
   (`ssh -L 8188:127.0.0.1:8188 gpu-box`).
2. In the editor, check `ComfyUI Workflows` under `Settings -> Plugins`.
3. `Plugins -> ComfyUI: Settings...` sets the following:
   - ComfyUI's address (default `http://127.0.0.1:8188`).
   - A token: it is sent as a bearer token, for ComfyUI behind a proxy that asks for one.
   - The workflow file.
   - The seed.
   - How the results are made:
     - Colors:
       - `Colors of the pixels around` (the default): each HD pixel takes the nearest of the
         colors of the 3x3 SD pixels around the one under it. This keeps edges crisp and
         invents no color.
       - `Colors of the picture`: the nearest of all the colors the picture uses.
       - `Any color`: the result rounded to RGB565.
     - `Send frames with their background`: where a frame's background mask shows the
       background, the workflow sees the background. The result is written only where the
       frame shows its own pixels; the background mask decides. Without this option, those
       pixels take the nearest frame color before they are sent.
     - `Leave dynamic zones as they are`.
     - `Frames per clip`: used with the ColorizingDMD Load Clip node.
4. Select frames and choose `Plugins -> ComfyUI: Run Workflow on Selected Frames...`.
   - The other entries run the workflow on the backgrounds or the sprites of the selected
     frames, or on the background or sprite chosen in its panel.
   - The status bar shows the progress.
   - `ComfyUI: Cancel Run` takes the prompt out of ComfyUI's queue, or interrupts it.
5. The review window shows each picture now and as proposed. Uncheck what should stay as it is,
   then click `Apply`. A frame, background or sprite without HD gets it first, as `Create HD`
   makes it.

The project must have an HD plane of twice the SD size.

A sprite is sent as the part its SD shape covers, as RGBA: it is transparent where the sprite
does not cover. The result is written where its HD shape covers; when the sprite has no HD yet,
that is the SD shape doubled.

The pictures are uploaded into ComfyUI's input folder as `cdmd_<run>_<picture>.png`, for
example `cdmd_1730800000000_1_f12.png`. The plugin does not remove them.

### What the plugin calls

- `GET /system_stats`: is ComfyUI there.
- `POST /upload/image`: the pictures, with `overwrite`.
- `POST /prompt`: the workflow, with the names and placeholders filled in.
- `GET /history/<id>`: polled until the prompt ends.
- `GET /view`: the output node's pictures.
- `POST /queue` (delete) and `POST /interrupt` with the prompt's ID: cancel. ComfyUI versions
  that do not know the ID interrupt whatever runs.

## Licence

GPLv3 or later (`LICENSE`), the same as the editor. ComfyUI (GPLv3) runs as a program of its
own. The custom node runs inside ComfyUI and is GPLv3 or later as well.
