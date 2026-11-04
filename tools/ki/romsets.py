"""MAME ROM declarations for the supported KI parent sets.

The filenames and checksums are copied from src/mame/rare/kinst.cpp. Keep the
set layout here while the KI board/ROM drivers are being prototyped.
"""

from dataclasses import dataclass


@dataclass(frozen=True)
class Rom:
    name: str
    crc32: int
    size: int = 0x80000
    base: int = 0


@dataclass(frozen=True)
class Game:
    setname: str
    bios: str
    boot: Rom
    dcs: tuple[Rom, ...]
    chd: str
    chd_sha1: str
    adsp_images: tuple[tuple[int, int, int, bool], ...]


# UltraCombo's sound_code_images() mapping: boot page, bank-1 DCS OS, then
# the two program images that the OS copies into external PM RAM.
KI_ADSP_IMAGES = (
    (0x0000, 0x0000, 0, True),
    (0x1000, 0x0000, 0, True),
    (0x0800, 0x0800, 0x300, False),
    (0x2000, 0x0800, 0x400, False),
)


GAMES = {
    "ki1": Game(
        setname="kinst", bios="v1.5d",
        boot=Rom("ki-l15d.u98", 0x7b65ca3d),
        dcs=(
            Rom("u10-l1", 0xb6cc155f, base=0x000000),
            Rom("u11-l1", 0x0b5e05df, base=0x200000),
            Rom("u12-l1", 0xd05ce6ad, base=0x400000),
            Rom("u13-l1", 0x7d0954ea, base=0x600000),
            Rom("u33-l1", 0x8bbe4f0c, base=0x800000),
            Rom("u34-l1", 0xb2e73603, base=0xa00000),
            Rom("u35-l1", 0x0aaef4fc, base=0xc00000),
            Rom("u36-l1", 0x0577bb60, base=0xe00000),
        ),
        chd="kinst.chd", chd_sha1="81d833236e994528d1482979261401b198d1ca53",
        adsp_images=KI_ADSP_IMAGES,
    ),
    "ki2": Game(
        setname="kinst2", bios="v1.4",
        boot=Rom("ki2-l14.u98", 0x27d0285e),
        dcs=(
            Rom("ki2_l1.u10", 0xfdf6ed51, base=0x000000),
            Rom("ki2_l1.u11", 0xf9e70024, base=0x200000),
            Rom("ki2_l1.u12", 0x2994c199, base=0x400000),
            Rom("ki2_l1.u13", 0x3fe6327b, base=0x600000),
            Rom("ki2_l1.u33", 0x6f4dcdcf, base=0x800000),
            Rom("ki2_l1.u34", 0x5db48206, base=0xa00000),
            Rom("ki2_l1.u35", 0x7245ce69, base=0xc00000),
            Rom("ki2_l1.u36", 0x8920acbb, base=0xe00000),
        ),
        chd="kinst2.chd", chd_sha1="e7c9291b4648eae0012ea0cc230731ed4987d1d5",
        adsp_images=KI_ADSP_IMAGES,
    ),
}
