# Hardware documentation maintenance

Follow the repository-root agent instructions and
[hardware guidelines](../agents/hardware_guidelines.md).

Agents must **automatically keep [where_to_buy.md](where_to_buy.md) updated and
coherent as part of every relevant change**, without waiting for a separate user
request. This applies when hardware components, required quantities, board
variants, display requirements, controls, audio modes, wiring, PCB footprints,
or supplier information change. Review the purchasing list whenever editing
hardware documentation; update affected entries in the same change.

- Cross-check the bill of materials with [hardware.md](hardware.md),
  [the PCB reference](../pcb/README.md), [audio documentation](../tools/audio.md),
  and the canonical firmware configuration in
  [main/prg32_config.h](../../main/prg32_config.h) and
  [audio Kconfig](../../components/prg32_audio/Kconfig).
- Keep baseline mono quantities, total stereo quantities and optional items
  explicit. Check electrical compatibility and mechanical fit separately.
- Maintain Amazon country searches and non-Amazon alternatives for each BOM
  item, plus sourcing guidance for countries without a dedicated Amazon store.
  Keep links free of affiliate tags.
- When reviewing suppliers, check current manufacturer specifications and
  affected links. Record the actual review date and mark unavailable or
  unverifiable listings; never invent stock, prices or delivery coverage.
- Keep the purchasing list in one place. Other documents should link to it,
  and it should link back to canonical wiring and configuration documentation.
- This automatic maintenance is part of the agent's repository-editing workflow.
  It does not create a background scheduler, scrape stores, or claim that stock
  and prices are continuously monitored.
