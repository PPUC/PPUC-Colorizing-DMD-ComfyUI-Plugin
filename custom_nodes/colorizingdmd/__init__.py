# license:GPLv3+
"""ColorizingDMD nodes for ComfyUI.

"ColorizingDMD Load Clip" (CDMDLoadClip) loads the pictures the ColorizingDMD editor's ComfyUI
plugin uploaded for one run as one batch, so that a video model such as SeedVR2 sees consecutive
frames together. Its "images" input holds the uploaded files' names, one a line, as the plugin
fills it in; it gives the batch (IMAGE) and the masks (MASK, 1 where a picture is transparent,
as Load Image gives them).

Copy or link this folder into ComfyUI's custom_nodes folder.
"""

import hashlib
import os


def clip_names(text):
    """The file names of the input, one a line, without empty lines."""
    return [line.strip() for line in text.splitlines() if line.strip()]


def load_arrays(paths):
    """The pictures as numpy arrays: RGB (height, width, 3) and masks (height, width), floats
    from 0 to 1. Raises ValueError for pictures of different sizes."""
    import numpy as np  # pylint: disable=import-outside-toplevel
    from PIL import Image, ImageOps  # pylint: disable=import-outside-toplevel

    images = []
    masks = []
    for path in paths:
        with Image.open(path) as opened:
            picture = ImageOps.exif_transpose(opened)
            rgb = np.array(picture.convert("RGB"), dtype=np.float32) / 255.0
            if "A" in picture.getbands():
                alpha = np.array(picture.getchannel("A"), dtype=np.float32) / 255.0
                mask = 1.0 - alpha
            else:
                mask = np.zeros(rgb.shape[:2], dtype=np.float32)
        if images and rgb.shape != images[0].shape:
            raise ValueError(f"{os.path.basename(path)} has another size than the clip's first "
                             "picture")
        images.append(rgb)
        masks.append(mask)
    return images, masks


class LoadClip:
    """The uploaded pictures of a run as one batch."""

    CATEGORY = "ColorizingDMD"
    RETURN_TYPES = ("IMAGE", "MASK")
    RETURN_NAMES = ("images", "masks")
    FUNCTION = "load"

    @classmethod
    def INPUT_TYPES(cls):  # pylint: disable=invalid-name
        return {"required": {"images": ("STRING", {"multiline": True, "default": ""})}}

    @staticmethod
    def _paths(images):
        import folder_paths  # pylint: disable=import-outside-toplevel,import-error

        return [folder_paths.get_annotated_filepath(name) for name in clip_names(images)]

    def load(self, images):
        import numpy as np  # pylint: disable=import-outside-toplevel
        import torch  # pylint: disable=import-outside-toplevel,import-error

        paths = self._paths(images)
        if not paths:
            raise ValueError("No pictures given")
        rgb, masks = load_arrays(paths)
        return (torch.from_numpy(np.stack(rgb)), torch.from_numpy(np.stack(masks)))

    @classmethod
    def IS_CHANGED(cls, images):  # pylint: disable=invalid-name
        digest = hashlib.sha256()
        for path in cls._paths(images):
            with open(path, "rb") as file:
                digest.update(file.read())
        return digest.hexdigest()

    @classmethod
    def VALIDATE_INPUTS(cls, images):  # pylint: disable=invalid-name
        names = clip_names(images)
        if not names:
            return "No pictures given"
        for name, path in zip(names, cls._paths(images)):
            if not os.path.isfile(path):
                return f"Invalid image file: {name}"
        return True


NODE_CLASS_MAPPINGS = {"CDMDLoadClip": LoadClip}
NODE_DISPLAY_NAME_MAPPINGS = {"CDMDLoadClip": "ColorizingDMD Load Clip"}
