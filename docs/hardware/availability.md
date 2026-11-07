# Hardware Availability

Reviewed: **2026-11-07**.

> [!IMPORTANT]
> **No agreement, no affiliation.** PRG32 is a public, open-source,
> open-hardware, non-profit research prototype. There is **no agreement,
> partnership, sponsorship, or other connection** between the University of
> Naples "Parthenope" (or the PRG32 project) and any company that sells
> hardware which could fit the PRG32 specification. Products listed here are
> pointers collected for students' convenience only. They are not endorsed,
> certified, or tested by the University or the PRG32 maintainers, and no one
> involved in PRG32 receives anything from their sale. Check every product
> against the [PRG32 specification](#what-fits-the-prg32-specification)
> before buying.

## What fits the PRG32 specification

A console runs the resident PRG32 firmware, so a board fits when it matches
one of the [pin profiles](profiles/README.md), or when you can write a new
profile for it:

| Part | Requirement | Reference |
|---|---|---|
| Microcontroller | Espressif ESP32-C6 (RV32IMAC), at least 4 MB flash, native USB exposed on GPIO12/GPIO13 | [Memory](memory.md) |
| Display | ILI9341 SPI TFT, 320x240 landscape (PRG32 draws a 320x200 game area) | [ILI9341](ili9341.md) |
| Input | Digital 5-way joystick or analog thumbstick, plus A/B (and optionally C, D, L, R) buttons on GPIO | [Joystick](joystick.md) |
| Audio | One or two MAX98357A I2S amplifiers (mono or stereo) | [Audio](audio.md) |

Other ESP32 chips (ESP32-C3, ESP32-S3, classic ESP32) are not substitutes for
the physical ESP32-C6 configuration.

## Build it yourself

The canonical route is a breadboard build from the
[bill of materials and where to buy](where_to_buy.md), wired as in
[`breadboard1`](profiles/breadboard1.md) or
[`breadboard2`](profiles/breadboard2.md). The [PCB reference design](pcb/README.md)
(`pcb1`) is open hardware and can be fabricated by anyone.

## Third-party boards

| Product | Seller | What the seller's page states | Status for PRG32 |
|---|---|---|---|
| [MAYPAD](https://madaboutyou.it/maker32/maypad/) (also "MAYPAD + LCD" and "MAYPAD FULL") | Mad About You - MAY SRL, Naples, Italy | ESP32-C6 board with the PRG32 connections on board; describes the PRG32 hardware requirements | Not tested by the PRG32 maintainers; pin mapping to a PRG32 profile not verified |

The table records what the seller's page said on the review date. Prices,
stock, and product contents are the seller's and can change; this page is not
a price or stock monitor.

To propose another product, open a pull request that adds a row with the link,
the seller, what the seller states, and what you verified (which profile you
flashed and which [setup checks](../usage/setup_verification.md) passed).
Keep the disclaimer above unchanged, and do not add affiliate links.
