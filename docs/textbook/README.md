# Playing with RISC-V — LaTeX Source

This archive contains the complete LaTeX source for the textbook
**"Playing with RISC-V: Assembly, C, and Retro Games on the PRG32 Platform."**

## Building the PDF

You need a TeX distribution (TeX Live 2021+ or MiKTeX) with the standard
packages (listings, tcolorbox, tikz, booktabs, titlesec, natbib, microtype,
hyperref). Then run:

```bash
latexmk -pdf main.tex
```

or, manually:

```bash
pdflatex main.tex
bibtex   main
pdflatex main.tex
pdflatex main.tex
```

The result is `main.pdf` (≈108 pages).

## Project layout

```
main.tex                 Master document; \input order defines the book
prg32style.sty           Shared style: palette, code listings, callout boxes
frontmatter/
  titlepage.tex          Designed title page
  copyright.tex          Colophon / licensing / trademarks
  preface.tex            Preface
  howtoread.tex          Conventions and the two reading tracks
  references.bib         Bibliography (BibTeX)
chapters/
  ch01_introduction.tex          Why a game, why RISC-V (European IP), why now
  ch02_platform_and_design.tex   Game design principles + the PRG32 framework
  ch03..ch06                     Computer Architecture track (RISC-V assembly)
  ch07..ch09                     Computer Programming track (C)
  ch10_assembly_meets_c.tex      Reunion: one game, two languages
appendices/
  app_a_api.tex          The PRG32 runtime interface (full API reference)
  app_b_performance.tex  Measuring performance like a scientist
  app_c_worked_examples.tex  Full working example sources (verbatim)
  app_d_github.tex       Using GitHub for coursework
  app_e_hardware.tex     Building the physical PRG32 board
  app_f_environment.tex  Environment setup (Windows / Linux / macOS)
examples/                Verbatim PRG32 example sources used by Appendix C
```

## About the platform

The textbook is built around the PRG32 open educational runtime for RISC-V
assembly and C games, by Raffaele Montella and Ivan Cafiero (University of
Naples "Parthenope"), distributed under the MIT License:

  https://github.com/riscv-prg32/PRG32

The verbatim source files under `examples/` are reproduced from that
repository's `examples/games` directory and remain under their original MIT
License. If you use PRG32 in coursework, cite it via the repository's
`CITATION.cff`.

## Notes

- All callout boxes, code listings, and diagrams are defined in
  `prg32style.sty`; edit colours and styles there in one place.
- The book compiles cleanly with no errors and no overfull boxes.
- Every code example in the book is designed to run on both the QEMU emulator
  (ESP32-C3 graphics target) and the physical ESP32-C6 board.
