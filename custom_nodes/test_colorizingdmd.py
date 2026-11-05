# license:GPLv3+
"""Tests of the ColorizingDMD nodes without ComfyUI: folder_paths is stood in for; the parts that
read pictures need numpy and Pillow (as ComfyUI has them), the batch also torch, and are skipped
without them."""

import importlib.util
import os
import struct
import sys
import tempfile
import types
import unittest
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import colorizingdmd  # noqa: E402  pylint: disable=wrong-import-position


def available(module):
    return importlib.util.find_spec(module) is not None


def write_png(path, width, height, channels, pixels):
    """A PNG of 8 bit RGB (channels 3) or RGBA (channels 4) pixels."""
    rows = b"".join(b"\0" + bytes(pixels[y * width * channels:(y + 1) * width * channels])
                    for y in range(height))

    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff))

    header = struct.pack(">IIBBBBB", width, height, 8, 6 if channels == 4 else 2, 0, 0, 0)
    with open(path, "wb") as file:
        file.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
                   chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


class Folder:
    """An input folder with folder_paths standing in for ComfyUI's."""

    def __init__(self):
        self.temp = tempfile.TemporaryDirectory()
        module = types.ModuleType("folder_paths")
        module.get_annotated_filepath = lambda name: os.path.join(self.temp.name, name)
        sys.modules["folder_paths"] = module

    def path(self, name):
        return os.path.join(self.temp.name, name)

    def close(self):
        del sys.modules["folder_paths"]
        self.temp.cleanup()


class NodeTests(unittest.TestCase):
    def setUp(self):
        self.folder = Folder()

    def tearDown(self):
        self.folder.close()

    def test_names(self):
        self.assertEqual(colorizingdmd.clip_names("a.png\n\n  b.png \n"), ["a.png", "b.png"])
        self.assertIn("CDMDLoadClip", colorizingdmd.NODE_CLASS_MAPPINGS)

    def test_validation_and_change(self):
        node = colorizingdmd.LoadClip
        self.assertEqual(node.VALIDATE_INPUTS(""), "No pictures given")
        self.assertIn("a.png", node.VALIDATE_INPUTS("a.png"))
        write_png(self.folder.path("a.png"), 1, 1, 3, [1, 2, 3])
        self.assertTrue(node.VALIDATE_INPUTS("a.png"))
        before = node.IS_CHANGED("a.png")
        write_png(self.folder.path("a.png"), 1, 1, 3, [1, 2, 4])
        self.assertNotEqual(node.IS_CHANGED("a.png"), before)

    @unittest.skipUnless(available("numpy") and available("PIL"), "needs numpy and Pillow")
    def test_arrays(self):
        write_png(self.folder.path("a.png"), 2, 1, 3, [255, 0, 0, 0, 255, 0])
        write_png(self.folder.path("b.png"), 2, 1, 4, [0, 0, 255, 255, 9, 9, 9, 0])
        images, masks = colorizingdmd.load_arrays([self.folder.path("a.png"),
                                                   self.folder.path("b.png")])
        self.assertEqual([image.shape for image in images], [(1, 2, 3), (1, 2, 3)])
        self.assertEqual(images[0][0][1].tolist(), [0.0, 1.0, 0.0])
        self.assertEqual(masks[0].tolist(), [[0.0, 0.0]])
        self.assertEqual(masks[1].tolist(), [[0.0, 1.0]])
        write_png(self.folder.path("c.png"), 1, 1, 3, [0, 0, 0])
        with self.assertRaises(ValueError):
            colorizingdmd.load_arrays([self.folder.path("a.png"), self.folder.path("c.png")])

    @unittest.skipUnless(available("numpy") and available("PIL") and available("torch"),
                         "needs numpy, Pillow and torch")
    def test_batch(self):
        write_png(self.folder.path("a.png"), 2, 1, 3, [255, 0, 0, 0, 255, 0])
        write_png(self.folder.path("b.png"), 2, 1, 3, [0, 0, 255, 9, 9, 9])
        images, masks = colorizingdmd.LoadClip().load("a.png\nb.png")
        self.assertEqual(tuple(images.shape), (2, 1, 2, 3))
        self.assertEqual(tuple(masks.shape), (2, 1, 2))


if __name__ == "__main__":
    unittest.main()
