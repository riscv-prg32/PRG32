# AGENTS.md

Guidance for coding agents working in this repository.

PRG32 is an educational ESP32-C6 / RISC-V assembly gaming platform. The project is deliberately simple, readable, and classroom-oriented. Preserve that spirit: prefer explicit code, clear names, and teachable examples over clever compact abstractions.

AGENT.md applies to the whole repository. Everytime you need information, look in the docs. Grab more specific information from the README.md files that exist deeper in the tree. When you encounter a redirect relevant to your task, you should always follow it and read further.

Everytime you modify code, you should also update the relevant documentation files, or you should create new documentation files for new features.
When you update or write documentation files, you should always avoid creating duplicate information. You should instead redirect to the more relevant file.

Documentation: `/docs`. **Always explore the complete hierarchy in the folder, read the files relevant to your task, and UPDATE THEM AFTER EDITS!**

You should also read [README.md](/README.md) and [CONTRIBUTING.md](/CONTRIBUTING.md).

## Detailed Guidelines

> [!IMPORTANT]
> Agents should ALWAYS read the files relevant to the task in `/docs/agents/` before modifying code or answering questions related to these topics. Explore the folder and read all possibly relevant files.

## Academic Project Context

Treat this repository as an academic-style software artifact:

- prioritize reproducibility over convenience
- keep implementation choices explainable in lab/classroom settings
- keep documentation aligned with coursework and assessment usage
- prefer transparent behavior over hidden automation

## Contributors

Please refer to [`CONTRIBUTORS.md`](CONTRIBUTORS.md) for the contributor metadata used across project docs.

## Repository Layout

Please refer to [`docs/repository_structure.md`](docs/repository_structure.md) for the complete repository layout.

## Git and Workspace Rules

- The user may have local changes. Do not revert files you did not change unless explicitly asked.
- Treat untracked files as user-owned unless you created them during the current task.
- Do not commit or push unless the user asks.
- When committing, use a short, specific message.
- Before committing or finalizing, run at least `git diff --check`.
- If a push is rejected because remote moved, pull/rebase and resolve conflicts without force-pushing unless the user explicitly requests a force push.

## Validation Checklist

Before reporting completion, try the relevant subset. YOU SHOULD ALWAYS SOURCE esp-idf/export.sh before doing this; usually it is found in $HOME/esp-idf/export.sh if you do not find it there, ask the user for the location.

```bash
git diff --check
python3 -m prg32 doctor
python3 -m py_compile python3 -m prg32
python3 -m py_compile 
idf.py -B build -D SDKCONFIG_DEFAULTS=sdkconfig.defaults build
```

If ESP-IDF is not available, use the first two checks and clearly state that the firmware or QEMU build could not be run locally.

Remember that python tooling `python -m prg32` is always available in /prg32/ and has lots of useful commands.