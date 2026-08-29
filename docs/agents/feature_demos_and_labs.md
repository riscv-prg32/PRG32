# Feature Demos and Labs

## Feature Demo Guidelines

Focused demos live in `examples/features/<name>/`.

Use them for framework mechanics that are bigger than one API call but smaller
than a full game:

- scrolling and parallax playfields
- animated sprites
- dual playfields

Each feature demo should contain a `README.md`, one assembly source such as
`demo.S`, and a C source under `c/demo.c` when it belongs to the standard
student demo set. Export game-like symbols with a feature-specific prefix:

```text
<feature>_init
<feature>_update
<feature>_draw
```

Use the `_c` suffix for C demo prefixes, for example `dual_playfield_c_init`.

Keep feature demos shorter than games. They should isolate the API behavior
students are meant to inspect.

## Editing Labs

When editing labs:

- Use concrete student actions.
- Include a visible checkpoint.
- Include a short reflection/debugging question.
- Keep commands copy-pastable.
