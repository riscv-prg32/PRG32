# Integration into PRG32

Target branch: `development-c6`.

Copy this directory to the repository as `cartridges/blackjack/`. No firmware source changes are required by the cartridge itself.

The build script intentionally uses only public branch facilities: portable cartridge compilation, AUD0 audio packing, multiplayer cartridge flag, metadata/icon/screenshot/colophon attachment through `prg32 store`, architecture tags, cartridge inspection, and multi-architecture store bundle packing.

If a CLI spelling changes while `development-c6` is still under development, consult `docs/software/cartridges.md`, `docs/software/audio.md`, and the Cartridge Store documentation and adjust `build.sh`; the game source/API targets are isolated from those host-side command spellings.
