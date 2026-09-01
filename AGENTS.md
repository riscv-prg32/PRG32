Markdown

# PRG32 Agent Instructions

**CONTEXT:** PRG32 is an educational ESP32-C6 / RISC-V assembly gaming platform. The project is deliberately simple, readable, and classroom-oriented. 
**PRIME DIRECTIVE:** Prioritize reproducibility, explicit code, clear names, and teachable examples over clever, compact abstractions. Treat this as an academic-style software artifact. 
**HOW YOU OPERATE**: Before doing any task, you must gain a complete understanding of the architecture of the project and how what you are going to modify is implemented. This is because working in embedded is a thorough task and must be done perfectly. 

**MISSION CRITICAL: YOU MUST RUN THESE PHASES COMPLETELY, OTHERWISE THE OUTPUT WILL BE REJECTED**

## [PHASE 1: MANDATORY RESEARCH]
Before writing or modifying any code, you MUST complete these steps:

1. **Read the Structure:** Read `docs/repository_structure.md` to understand where files belong.
2. **Read Agent Docs:** You MUST search `/docs/agents/` for files related to your current task and read them. Read all files with `cat docs/agents/*.md`
3. **Map the Docs:** Run `ls -R docs/` to view the available documentation hierarchy.
4. **Read the Docs**: Based on your prompt, read AT LEAST 5 OTHER files with `cat` from `docs`.
5. **Follow Redirects:** If a file tells you to look elsewhere, you MUST follow the redirect and read the target file.
*Do not guess architectural patterns. Read the docs first.*

## [PHASE 2: EXECUTION & DOCUMENTATION]
When modifying code, you are bound by these rules:

- **No Ghost Edits:** Do not revert or modify files unrelated to the user's explicit prompt. Treat untracked files as user-owned.
- **Documentation Parity:** Every code change MUST be accompanied by an update to the relevant `/docs` file or a new markdown file. 
- **DRY Docs:** Do not duplicate information. If a concept exists, link to the existing file rather than rewriting it.
- **Contributors:** Read `CONTRIBUTORS.md` for metadata formatting when updating docs.

## [PHASE 3: VALIDATION]
Before reporting task completion, you MUST run the following checks. 

**Pre-requisite:** Source the ESP-IDF environment. Run `source $HOME/esp-idf/export.sh`. If it is not found, STOP and ask the user for the ESP-IDF path.

Run these exact commands to validate your work:
```bash
# 1. Check for syntax/whitespace errors
git diff --check

# 2. Run Python linting/tooling
python3 -m prg32 doctor
python3 -m py_compile python3 -m prg32

# 3. If you are working on ESP32c6, Build the physical firmware:
python3 -m prg32 esp32c6 build

# 3. Otherwise if you are working on QEMU, Build the QEMU firmware:
python3 -m prg32 qemu build
```