#!/usr/bin/env python3
"""Generate the small Chinese UI fonts used by the KernSigner migration shell.

LVGL's built-in CJK fonts are intentionally small subsets. This script bakes
only the glyphs used by the current firmware UI, plus printable ASCII, so
Chinese text stays readable without pulling a full CJK font into firmware.
"""

import subprocess
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = REPO_ROOT / "main/ui/assets"
SOURCE_GLOBS = ("main/**/*.c", "main/**/*.h")
GENERATED_FONT_PREFIX = "signer_cn_"
LATIN_FONT = Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")
CJK_FONT = Path("/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf")
FONT_SIZES = (20, 28)
EXTRA_CJK_SYMBOLS = (
    "：，。、；！？（）【】《》“”‘’·—"
    "固件会自动打开相机几秒钟串口日志记录是否收到画面导入钱包保存二维码内容"
)


def collect_cjk_symbols() -> str:
    chars = set(EXTRA_CJK_SYMBOLS)
    for pattern in SOURCE_GLOBS:
        for path in REPO_ROOT.glob(pattern):
            if path.parent == OUT_DIR and path.name.startswith(GENERATED_FONT_PREFIX):
                continue
            text = path.read_text(encoding="utf-8")
            chars.update(ch for ch in text if ord(ch) >= 0x80)
    return "".join(sorted(chars))


def require_file(path: Path) -> None:
    if not path.exists():
        raise SystemExit(f"Missing font file: {path}")


def main() -> None:
    require_file(LATIN_FONT)
    require_file(CJK_FONT)

    ascii_symbols = "".join(chr(cp) for cp in range(0x20, 0x7F))
    cjk_symbols = collect_cjk_symbols()

    for size in FONT_SIZES:
        output = OUT_DIR / f"signer_cn_{size}.c"
        subprocess.run(
            [
                "npx",
                "--yes",
                "lv_font_conv",
                "--size",
                str(size),
                "--bpp",
                "4",
                "--font",
                str(LATIN_FONT),
                "--symbols",
                ascii_symbols,
                "--font",
                str(CJK_FONT),
                "--symbols",
                cjk_symbols,
                "--format",
                "lvgl",
                "--lv-include",
                "lvgl.h",
                "--lv-font-name",
                f"signer_cn_{size}",
                "-o",
                str(output),
                "--force-fast-kern-format",
                "--no-compress",
                "--no-prefilter",
            ],
            check=True,
        )
        output.write_text(output.read_text(encoding="utf-8").rstrip() + "\n",
                          encoding="utf-8")
        print(output)

    print(f"ASCII glyphs: {len(ascii_symbols)}")
    print(f"CJK/UI glyphs: {len(cjk_symbols)}")


if __name__ == "__main__":
    main()
