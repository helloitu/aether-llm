from pathlib import Path


SCRIPT = Path("/lib/OpenOrbisSDK/scripts/make_fself.py")


def replace_once(text, old, new):
    if new in text:
        return text
    if old not in text:
        raise RuntimeError(f"patch target not found: {old!r}")
    return text.replace(old, new, 1)


text = SCRIPT.read_text()

text = replace_once(
    text,
    "import argparse, re, string\n",
    "import argparse, re, string\n\n"
    "try:\n"
    "\txrange\n"
    "except NameError:\n"
    "\txrange = range\n",
)

replacements = [
    ("MAGIC = '\\x7FELF'", "MAGIC = b'\\x7FELF'"),
    ("EMPTY_DIGEST = '\\0' * DIGEST_SIZE", "EMPTY_DIGEST = b'\\0' * DIGEST_SIZE"),
    ("EMPTY_SIGNATURE = '\\0' * SIGNATURE_SIZE", "EMPTY_SIGNATURE = b'\\0' * SIGNATURE_SIZE"),
    ("MAGIC = '\\x4F\\x15\\x3D\\x1D'", "MAGIC = b'\\x4F\\x15\\x3D\\x1D'"),
    (
        "self.npdrm_control_block.content_id = '\\0' * SELF_NPDRM_CONTROL_BLOCK_CONTENT_ID_SIZE",
        "self.npdrm_control_block.content_id = b'\\0' * SELF_NPDRM_CONTROL_BLOCK_CONTENT_ID_SIZE",
    ),
    (
        "self.npdrm_control_block.random_pad = '\\0' * SELF_NPDRM_CONTROL_BLOCK_RANDOM_PAD_SIZE",
        "self.npdrm_control_block.random_pad = b'\\0' * SELF_NPDRM_CONTROL_BLOCK_RANDOM_PAD_SIZE",
    ),
    (".ljust(SIGNATURE_SIZE, '\\0')", ".ljust(SIGNATURE_SIZE, b'\\0')"),
    ("val = val.decode('hex')", "val = bytes.fromhex(val)"),
]

for old, new in replacements:
    text = replace_once(text, old, new)

text = text.replace("data = ''", "data = b''")

SCRIPT.write_text(text)
