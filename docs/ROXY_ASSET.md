# Roxy asset provenance

MicroStick's Roxy animation data is derived from the repository owner's custom
Codex pet package:

- pet ID: `roxy-pixel`
- display name: `Roxy Pixel`
- source atlas SHA-256: `f88a7e1140a2d540d6703716981e4c715b6e5ffb1c26fac6413a8b6f07f15f7e`
- source location at generation time: `~/.codex/pets/roxy-pixel/spritesheet.webp`

The original WebP atlas is not committed or included in release archives.
`firmware/sticks3/tools/generate_roxy_assets.py` validates the exact source
hash, crops the declared animation rows, scales them to 96×104, quantizes the
palette, and emits bounded compressed frame data in
`firmware/sticks3/generated/microstick_roxy_assets.c`.

The generated frame data and previews are distributed by the project owner
under MicroStick's MIT license. Roxy is a MicroStick product character in this
repository; it is not presented as an OpenAI or Work Louder built-in asset or
official mark.
