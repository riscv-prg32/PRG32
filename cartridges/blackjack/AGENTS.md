# AGENTS.md

When making changes to this cartridge, run the following local checks and package scripts from the repository root:

```bash
cc -std=c11 -Wall -Wextra -Werror \
  cartridges/blackjack/tests/test_rules.c \
  cartridges/blackjack/blackjack_rules.c \
  -o /tmp/prg32-blackjack-rules
/tmp/prg32-blackjack-rules
cartridges/blackjack/build.sh
```
