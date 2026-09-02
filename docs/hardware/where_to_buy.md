# PRG32 bill of materials and where to buy

Hardware and supplier reference reviewed: **2026-09-02**.

This is the purchasing list for one physical ESP32-C6 PRG32 unit. Use the
[hardware guide](hardware.md) for authoritative wiring, the
[display notes](ili9341.md) for the ILI9341 backend, and the
[audio guide](../tools/audio.md) for mono/stereo configuration. The
[PCB reference](../pcb/README.md) defines mechanical placement and footprints.
Supplier examples below are candidates for a breadboard build; they have not
all been assembled and tested with PRG32 or verified to fit the reference PCB.

## Bill of materials

Quantities are individual parts, not retail packs. Stereo quantities are totals,
not additions to the mono column. The classroom baseline includes mono audio.

| ID | Part and purchasing specification | Mono | Stereo |
|---|---|---:|---:|
| MCU | ESP32-C6 development board with headers, at least 4 MB flash, and all GPIOs required by the hardware guide exposed; ESP32-C6-DevKitC-1-N8 is a candidate with 8 MB flash | 1 | 1 |
| TFT | ILI9341 2.8-inch SPI TFT, 320x240 in landscape, compatible with 3.3 V logic and the documented power/backlight connections | 1 | 1 |
| JOY | Five-way **digital** joystick/navigation module: four directions plus center press, separate active-low switch outputs | 1 | 1 |
| BTN | Normally-open momentary pushbuttons for A and B; breadboard-compatible leads | 2 | 2 |
| AMP | MAX98357A I2S DAC/amplifier breakout, with accessible channel-selection pin or jumper | 1 | 2 |
| SPK | Passive 4–8 ohm speaker, 1–3 W; choose 3 W for more power headroom and dimensions suitable for your enclosure | 1 | 2 |
| BB | Full-size solderless breadboard, approximately 830 tie points; add another if the selected modules obstruct usable holes | 1 | 1–2 |
| WIRE | Assorted 2.54 mm male/male and male/female jumpers; plan approximately 40 wires for mono or 50 for stereo, including rail bridges (assembly allowance, not an exact net count) | 1 set | 1 set |
| USB | USB **data** cable matching the development board and computer; the official DevKitC-1 uses Micro-USB, while compatible boards may use USB-C | 1 | 1 |
| HDR | 2.54 mm breakaway headers, only where modules arrive without fitted headers; determine lengths from the chosen modules | as needed | as needed |
| MODE | Channel-selection resistor(s) or jumper(s) matching the exact amplifier breakout; the reference schematic labels its right-channel resistor 47 kohm, but this is not a universal value for other boards | none for default mono | as required |
| CONN | Optional speaker wire/connectors with matching pitch, polarity and mating halves; direct wires to the amplifier terminals are sufficient | 0–1 pair | 0–2 pairs |

USB power must cover the board, display and audio load. Check the selected
board's power limits and the [audio power guidance](hardware.md#safety-notes)
before choosing an additional regulated supply. A computer and a suitable USB
power source are assumed available. A soldering iron, solder and a multimeter
are shared assembly tools rather than components of each unit.

For a PCB build, replace the breadboard with the board fabricated from the
[reference design](../pcb/README.md), and choose headers, buttons, speakers and
mounting hardware against its actual footprints. PCB fabrication and enclosure
hardware require a quote for the chosen design; this list is not a claim that
all linked modules are mechanically interchangeable.

## Check before ordering

- Compare the full development-board pinout with [PRG32 wiring](hardware.md#pinouts-and-wiring).
  Small ESP32-C6 boards may omit required GPIOs. An ESP32-C3, ESP32-S3 or classic
  ESP32 board is not a replacement for the physical C6 configuration. See the
  [Espressif DevKitC-1 guide](https://documentation.espressif.com/esp-dev-kits/en/latest/esp32c6/esp32-c6-devkitc-1/user_guide.html).
- Buy an SPI ILI9341 breakout, not a parallel-only shield or a different display
  controller. Check the board's supply requirements and backlight circuitry;
  touch and microSD features are not required for this build. The
  [Adafruit display example](https://www.adafruit.com/product/1770) supports SPI,
  but its header layout must be mapped to the PRG32 signals.
- A KY-023/PS2 analog joystick with VRx/VRy outputs does not provide the five
  switch signals expected by [the controller implementation](external_controllers.md).
  Center press is START/SELECT. Separate A/B buttons retain the reference layout;
  spare buttons on a navigation module can replace them only when wired to the
  corresponding inputs. No extra SETUP button is needed.
- Use MAX98357**A** I2S modules. For stereo, inspect the exact breakout's
  channel-selection network before buying mode resistors. The
  [Adafruit SD/MODE explanation](https://learn.adafruit.com/adafruit-max98357-i2s-class-d-mono-amp/pinouts)
  shows why a resistor value depends on supply voltage and existing resistors.
  Follow [PRG32 audio wiring](hardware.md#audio-configuration) for speakers.
- Prefer pre-soldered headers for solderless classroom kits. Check connector
  types, pack quantities and breadboard clearance before ordering a class set.
  No separate Wi-Fi module, passive buzzer, microSD card or USB gamepad is
  required by the reference configuration.

## Specialist suppliers: links for each part

These product pages document candidate specifications; catalog searches provide
alternatives when a product is unavailable. Set the delivery country in the
supplier's site before comparing the delivered total. Availability and shipping
have **not** been verified for every destination, and no prices are promised.

| ID | Non-Amazon product or catalog link | Selection note |
|---|---|---|
| MCU | [Adafruit 5672](https://www.adafruit.com/product/5672); [DigiKey search](https://www.digikey.com/en/products?keywords=ESP32-C6-DevKitC-1-N8) | Development board, not a bare WROOM module |
| TFT | [Adafruit 1770](https://www.adafruit.com/product/1770); [DigiKey search](https://www.digikey.com/en/products?keywords=Adafruit%201770) | Configure the breakout for SPI; check physical fit |
| JOY | [Joy-IT COM-5WS at GoTronic](https://www.gotronic.fr/art-module-a-joystick-5-directions-com-5ws-38766.htm); [manufacturer manual](https://joy-it.net/files/files/Produkte/COM-5WS/COM-5WS_Manual-EN_2025-08-25.pdf) | Verify individual switch outputs and 3.3 V operation; extra module buttons are optional |
| BTN | [Adafruit 1119](https://www.adafruit.com/product/1119); [DigiKey search](https://www.digikey.com/en/products?keywords=momentary%20tactile%20switch%20through%20hole) | Two buttons, not two packs; PCB footprint may differ |
| AMP | [Adafruit 3006](https://www.adafruit.com/product/3006); [same module at DigiKey](https://www.digikey.com/en/products/detail/adafruit-industries-llc/3006/6058477) | Buy one for mono or two for stereo |
| SPK | [Adafruit 1314](https://www.adafruit.com/product/1314); [DigiKey search](https://www.digikey.com/en/products?keywords=speaker%204%20ohm%203W) | 1314 was listed out of stock at review; its approximately 78 mm body is only a breadboard candidate |
| BB | [Adafruit 239](https://www.adafruit.com/product/239); [DigiKey search](https://www.digikey.com/en/products?keywords=solderless%20breadboard%20830) | Check usable width around the MCU |
| WIRE | [Male/male jumpers](https://www.adafruit.com/product/1956); [male/female jumpers](https://www.adafruit.com/product/1954) | Choose lengths and pack counts for your layout |
| USB | [Micro-USB data cable example](https://www.adafruit.com/product/2185); [DigiKey USB-C cable search](https://www.digikey.com/en/products?keywords=USB%20C%20data%20cable) | Choose the connector fitted to your board |
| HDR | [Adafruit 392](https://www.adafruit.com/product/392); [DigiKey search](https://www.digikey.com/en/products?keywords=2.54mm%20male%20header) | Only buy missing headers |
| MODE | [DigiKey resistor search](https://www.digikey.com/en/products?keywords=47k%20through%20hole%20resistor) | Search is for the reference value; determine the value for your amplifier before ordering |
| CONN | [DigiKey connector search](https://www.digikey.com/en/products?keywords=2%20pin%20wire%20connector) | Match both halves and wire rating; optional |

## Amazon searches by country

Each country below links to a search for **every BOM ID**, not to a verified
in-stock offer. Search results may include incompatible parts: apply the BOM
specifications before purchasing. The same model/part terms are intentionally
used across stores; translate generic terms locally if a search is sparse.
These are ordinary links without affiliate tags.

Country-store references: [Amazon Global Selling](https://sell.amazon.com/global-selling),
[European stores including Ireland](https://sellercentral.amazon.ie/welcome/sell-across-europe),
[South Africa](https://www.aboutamazon.com/news/retail/amazon-south-africa),
[Turkey](https://satis.amazon.com.tr/satis), and
[Singapore's International Store](https://www.aboutamazon.sg/news/company-news/amazon-singapore-to-expand-international-store-selection-in-response-to-customer-demand).
Singapore searches now depend on the International Store selection.

The specialist column names another online resource for that country; search
there using the part numbers above. Local distributor examples come from the
[Adafruit distributor directory](https://www.adafruit.com/distributors).
A distributor listing does not mean that store carries every BOM item.
“International catalog” means use [DigiKey's country/region selector](https://www.digikey.com/)
and the per-part links above; it does not assert local stock or guaranteed delivery.

| Country | Amazon: electronics | Amazon: assembly supplies | Other online resource |
|---|---|---|---|
| Italy | [MCU](https://www.amazon.it/s?k=ESP32-C6+DevKitC-1+N8), [TFT](https://www.amazon.it/s?k=ILI9341+2.8+SPI+320x240), [JOY](https://www.amazon.it/s?k=5+way+digital+navigation+joystick+module), [BTN](https://www.amazon.it/s?k=momentary+tactile+push+button), [AMP](https://www.amazon.it/s?k=MAX98357A+I2S+amplifier), [SPK](https://www.amazon.it/s?k=speaker+4+ohm+3W) | [BB](https://www.amazon.it/s?k=830+solderless+breadboard), [WIRE](https://www.amazon.it/s?k=2.54mm+jumper+wires+male+female), [USB](https://www.amazon.it/s?k=USB+data+cable), [HDR](https://www.amazon.it/s?k=2.54mm+male+pin+header), [MODE](https://www.amazon.it/s?k=47k+ohm+resistor), [CONN](https://www.amazon.it/s?k=2+pin+wire+connector) | [Melopero](https://www.melopero.com/) |
| Australia | [MCU](https://www.amazon.com.au/s?k=ESP32-C6+DevKitC-1+N8), [TFT](https://www.amazon.com.au/s?k=ILI9341+2.8+SPI+320x240), [JOY](https://www.amazon.com.au/s?k=5+way+digital+navigation+joystick+module), [BTN](https://www.amazon.com.au/s?k=momentary+tactile+push+button), [AMP](https://www.amazon.com.au/s?k=MAX98357A+I2S+amplifier), [SPK](https://www.amazon.com.au/s?k=speaker+4+ohm+3W) | [BB](https://www.amazon.com.au/s?k=830+solderless+breadboard), [WIRE](https://www.amazon.com.au/s?k=2.54mm+jumper+wires+male+female), [USB](https://www.amazon.com.au/s?k=USB+data+cable), [HDR](https://www.amazon.com.au/s?k=2.54mm+male+pin+header), [MODE](https://www.amazon.com.au/s?k=47k+ohm+resistor), [CONN](https://www.amazon.com.au/s?k=2+pin+wire+connector) | [Core Electronics](https://www.core-electronics.com.au/) |
| Belgium | [MCU](https://www.amazon.com.be/s?k=ESP32-C6+DevKitC-1+N8), [TFT](https://www.amazon.com.be/s?k=ILI9341+2.8+SPI+320x240), [JOY](https://www.amazon.com.be/s?k=5+way+digital+navigation+joystick+module), [BTN](https://www.amazon.com.be/s?k=momentary+tactile+push+button), [AMP](https://www.amazon.com.be/s?k=MAX98357A+I2S+amplifier), [SPK](https://www.amazon.com.be/s?k=speaker+4+ohm+3W) | [BB](https://www.amazon.com.be/s?k=830+solderless+breadboard), [WIRE](https://www.amazon.com.be/s?k=2.54mm+jumper+wires+male+female), [USB](https://www.amazon.com.be/s?k=USB+data+cable), [HDR](https://www.amazon.com.be/s?k=2.54mm+male+pin+header), [MODE](https://www.amazon.com.be/s?k=47k+ohm+resistor), [CONN](https://www.amazon.com.be/s?k=2+pin+wire+connector) | [MC Hobby](https://shop.mchobby.be/) |
| Brazil | [MCU](https://www.amazon.com.br/s?k=ESP32-C6+DevKitC-1+N8), [TFT](https://www.amazon.com.br/s?k=ILI9341+2.8+SPI+320x240), [JOY](https://www.amazon.com.br/s?k=5+way+digital+navigation+joystick+module), [BTN](https://www.amazon.com.br/s?k=momentary+tactile+push+button), [AMP](https://www.amazon.com.br/s?k=MAX98357A+I2S+amplifier), [SPK](https://www.amazon.com.br/s?k=speaker+4+ohm+3W) | [BB](https://www.amazon.com.br/s?k=830+solderless+breadboard), [WIRE](https://www.amazon.com.br/s?k=2.54mm+jumper+wires+male+female), [USB](https://www.amazon.com.br/s?k=USB+data+cable), [HDR](https://www.amazon.com.br/s?k=2.54mm+male+pin+header), [MODE](https://www.amazon.com.br/s?k=47k+ohm+resistor), [CONN](https://www.amazon.com.br/s?k=2+pin+wire+connector) | [International catalog](https://www.digikey.com/) |
| Canada | [MCU](https://www.amazon.ca/s?k=ESP32-C6+DevKitC-1+N8), [TFT](https://www.amazon.ca/s?k=ILI9341+2.8+SPI+320x240), [JOY](https://www.amazon.ca/s?k=5+way+digital+navigation+joystick+module), [BTN](https://www.amazon.ca/s?k=momentary+tactile+push+button), [AMP](https://www.amazon.ca/s?k=MAX98357A+I2S+amplifier), [SPK](https://www.amazon.ca/s?k=speaker+4+ohm+3W) | [BB](https://www.amazon.ca/s?k=830+solderless+breadboard), [WIRE](https://www.amazon.ca/s?k=2.54mm+jumper+wires+male+female), [USB](https://www.amazon.ca/s?k=USB+data+cable), [HDR](https://www.amazon.ca/s?k=2.54mm+male+pin+header), [MODE](https://www.amazon.ca/s?k=47k+ohm+resistor), [CONN](https://www.amazon.ca/s?k=2+pin+wire+connector) | [RobotShop](https://www.robotshop.com/) |
| Egypt | [MCU](https://www.amazon.eg/s?k=ESP32-C6+DevKitC-1+N8), [TFT](https://www.amazon.eg/s?k=ILI9341+2.8+SPI+320x240), [JOY](https://www.amazon.eg/s?k=5+way+digital+navigation+joystick+module), [BTN](https://www.amazon.eg/s?k=momentary+tactile+push+button), [AMP](https://www.amazon.eg/s?k=MAX98357A+I2S+amplifier), [SPK](https://www.amazon.eg/s?k=speaker+4+ohm+3W) | [BB](https://www.amazon.eg/s?k=830+solderless+breadboard), [WIRE](https://www.amazon.eg/s?k=2.54mm+jumper+wires+male+female), [USB](https://www.amazon.eg/s?k=USB+data+cable), [HDR](https://www.amazon.eg/s?k=2.54mm+male+pin+header), [MODE](https://www.amazon.eg/s?k=47k+ohm+resistor), [CONN](https://www.amazon.eg/s?k=2+pin+wire+connector) | [International catalog](https://www.digikey.com/) |
| France | [MCU](https://www.amazon.fr/s?k=ESP32-C6+DevKitC-1+N8), [TFT](https://www.amazon.fr/s?k=ILI9341+2.8+SPI+320x240), [JOY](https://www.amazon.fr/s?k=5+way+digital+navigation+joystick+module), [BTN](https://www.amazon.fr/s?k=momentary+tactile+push+button), [AMP](https://www.amazon.fr/s?k=MAX98357A+I2S+amplifier), [SPK](https://www.amazon.fr/s?k=speaker+4+ohm+3W) | [BB](https://www.amazon.fr/s?k=830+solderless+breadboard), [WIRE](https://www.amazon.fr/s?k=2.54mm+jumper+wires+male+female), [USB](https://www.amazon.fr/s?k=USB+data+cable), [HDR](https://www.amazon.fr/s?k=2.54mm+male+pin+header), [MODE](https://www.amazon.fr/s?k=47k+ohm+resistor), [CONN](https://www.amazon.fr/s?k=2+pin+wire+connector) | [GoTronic](https://www.gotronic.fr/) |
| Germany | [MCU](https://www.amazon.de/s?k=ESP32-C6+DevKitC-1+N8), [TFT](https://www.amazon.de/s?k=ILI9341+2.8+SPI+320x240), [JOY](https://www.amazon.de/s?k=5+way+digital+navigation+joystick+module), [BTN](https://www.amazon.de/s?k=momentary+tactile+push+button), [AMP](https://www.amazon.de/s?k=MAX98357A+I2S+amplifier), [SPK](https://www.amazon.de/s?k=speaker+4+ohm+3W) | [BB](https://www.amazon.de/s?k=830+solderless+breadboard), [WIRE](https://www.amazon.de/s?k=2.54mm+jumper+wires+male+female), [USB](https://www.amazon.de/s?k=USB+data+cable), [HDR](https://www.amazon.de/s?k=2.54mm+male+pin+header), [MODE](https://www.amazon.de/s?k=47k+ohm+resistor), [CONN](https://www.amazon.de/s?k=2+pin+wire+connector) | [BerryBase](https://www.berrybase.de/) |
| India | [MCU](https://www.amazon.in/s?k=ESP32-C6+DevKitC-1+N8), [TFT](https://www.amazon.in/s?k=ILI9341+2.8+SPI+320x240), [JOY](https://www.amazon.in/s?k=5+way+digital+navigation+joystick+module), [BTN](https://www.amazon.in/s?k=momentary+tactile+push+button), [AMP](https://www.amazon.in/s?k=MAX98357A+I2S+amplifier), [SPK](https://www.amazon.in/s?k=speaker+4+ohm+3W) | [BB](https://www.amazon.in/s?k=830+solderless+breadboard), [WIRE](https://www.amazon.in/s?k=2.54mm+jumper+wires+male+female), [USB](https://www.amazon.in/s?k=USB+data+cable), [HDR](https://www.amazon.in/s?k=2.54mm+male+pin+header), [MODE](https://www.amazon.in/s?k=47k+ohm+resistor), [CONN](https://www.amazon.in/s?k=2+pin+wire+connector) | [Robu](https://robu.in/) |
| Ireland | [MCU](https://www.amazon.ie/s?k=ESP32-C6+DevKitC-1+N8), [TFT](https://www.amazon.ie/s?k=ILI9341+2.8+SPI+320x240), [JOY](https://www.amazon.ie/s?k=5+way+digital+navigation+joystick+module), [BTN](https://www.amazon.ie/s?k=momentary+tactile+push+button), [AMP](https://www.amazon.ie/s?k=MAX98357A+I2S+amplifier), [SPK](https://www.amazon.ie/s?k=speaker+4+ohm+3W) | [BB](https://www.amazon.ie/s?k=830+solderless+breadboard), [WIRE](https://www.amazon.ie/s?k=2.54mm+jumper+wires+male+female), [USB](https://www.amazon.ie/s?k=USB+data+cable), [HDR](https://www.amazon.ie/s?k=2.54mm+male+pin+header), [MODE](https://www.amazon.ie/s?k=47k+ohm+resistor), [CONN](https://www.amazon.ie/s?k=2+pin+wire+connector) | [International catalog](https://www.digikey.com/) |
| Japan | [MCU](https://www.amazon.co.jp/s?k=ESP32-C6+DevKitC-1+N8), [TFT](https://www.amazon.co.jp/s?k=ILI9341+2.8+SPI+320x240), [JOY](https://www.amazon.co.jp/s?k=5+way+digital+navigation+joystick+module), [BTN](https://www.amazon.co.jp/s?k=momentary+tactile+push+button), [AMP](https://www.amazon.co.jp/s?k=MAX98357A+I2S+amplifier), [SPK](https://www.amazon.co.jp/s?k=speaker+4+ohm+3W) | [BB](https://www.amazon.co.jp/s?k=830+solderless+breadboard), [WIRE](https://www.amazon.co.jp/s?k=2.54mm+jumper+wires+male+female), [USB](https://www.amazon.co.jp/s?k=USB+data+cable), [HDR](https://www.amazon.co.jp/s?k=2.54mm+male+pin+header), [MODE](https://www.amazon.co.jp/s?k=47k+ohm+resistor), [CONN](https://www.amazon.co.jp/s?k=2+pin+wire+connector) | [Switch Science](https://www.switch-science.com/) |
| Mexico | [MCU](https://www.amazon.com.mx/s?k=ESP32-C6+DevKitC-1+N8), [TFT](https://www.amazon.com.mx/s?k=ILI9341+2.8+SPI+320x240), [JOY](https://www.amazon.com.mx/s?k=5+way+digital+navigation+joystick+module), [BTN](https://www.amazon.com.mx/s?k=momentary+tactile+push+button), [AMP](https://www.amazon.com.mx/s?k=MAX98357A+I2S+amplifier), [SPK](https://www.amazon.com.mx/s?k=speaker+4+ohm+3W) | [BB](https://www.amazon.com.mx/s?k=830+solderless+breadboard), [WIRE](https://www.amazon.com.mx/s?k=2.54mm+jumper+wires+male+female), [USB](https://www.amazon.com.mx/s?k=USB+data+cable), [HDR](https://www.amazon.com.mx/s?k=2.54mm+male+pin+header), [MODE](https://www.amazon.com.mx/s?k=47k+ohm+resistor), [CONN](https://www.amazon.com.mx/s?k=2+pin+wire+connector) | [UNIT Electronics](https://uelectronics.com/) |
| Netherlands | [MCU](https://www.amazon.nl/s?k=ESP32-C6+DevKitC-1+N8), [TFT](https://www.amazon.nl/s?k=ILI9341+2.8+SPI+320x240), [JOY](https://www.amazon.nl/s?k=5+way+digital+navigation+joystick+module), [BTN](https://www.amazon.nl/s?k=momentary+tactile+push+button), [AMP](https://www.amazon.nl/s?k=MAX98357A+I2S+amplifier), [SPK](https://www.amazon.nl/s?k=speaker+4+ohm+3W) | [BB](https://www.amazon.nl/s?k=830+solderless+breadboard), [WIRE](https://www.amazon.nl/s?k=2.54mm+jumper+wires+male+female), [USB](https://www.amazon.nl/s?k=USB+data+cable), [HDR](https://www.amazon.nl/s?k=2.54mm+male+pin+header), [MODE](https://www.amazon.nl/s?k=47k+ohm+resistor), [CONN](https://www.amazon.nl/s?k=2+pin+wire+connector) | [Kiwi Electronics](https://www.kiwi-electronics.com/) |
| Poland | [MCU](https://www.amazon.pl/s?k=ESP32-C6+DevKitC-1+N8), [TFT](https://www.amazon.pl/s?k=ILI9341+2.8+SPI+320x240), [JOY](https://www.amazon.pl/s?k=5+way+digital+navigation+joystick+module), [BTN](https://www.amazon.pl/s?k=momentary+tactile+push+button), [AMP](https://www.amazon.pl/s?k=MAX98357A+I2S+amplifier), [SPK](https://www.amazon.pl/s?k=speaker+4+ohm+3W) | [BB](https://www.amazon.pl/s?k=830+solderless+breadboard), [WIRE](https://www.amazon.pl/s?k=2.54mm+jumper+wires+male+female), [USB](https://www.amazon.pl/s?k=USB+data+cable), [HDR](https://www.amazon.pl/s?k=2.54mm+male+pin+header), [MODE](https://www.amazon.pl/s?k=47k+ohm+resistor), [CONN](https://www.amazon.pl/s?k=2+pin+wire+connector) | [Botland](https://botland.com.pl/) |
| Saudi Arabia | [MCU](https://www.amazon.sa/s?k=ESP32-C6+DevKitC-1+N8), [TFT](https://www.amazon.sa/s?k=ILI9341+2.8+SPI+320x240), [JOY](https://www.amazon.sa/s?k=5+way+digital+navigation+joystick+module), [BTN](https://www.amazon.sa/s?k=momentary+tactile+push+button), [AMP](https://www.amazon.sa/s?k=MAX98357A+I2S+amplifier), [SPK](https://www.amazon.sa/s?k=speaker+4+ohm+3W) | [BB](https://www.amazon.sa/s?k=830+solderless+breadboard), [WIRE](https://www.amazon.sa/s?k=2.54mm+jumper+wires+male+female), [USB](https://www.amazon.sa/s?k=USB+data+cable), [HDR](https://www.amazon.sa/s?k=2.54mm+male+pin+header), [MODE](https://www.amazon.sa/s?k=47k+ohm+resistor), [CONN](https://www.amazon.sa/s?k=2+pin+wire+connector) | [International catalog](https://www.digikey.com/) |
| Singapore | [MCU](https://www.amazon.sg/s?k=ESP32-C6+DevKitC-1+N8), [TFT](https://www.amazon.sg/s?k=ILI9341+2.8+SPI+320x240), [JOY](https://www.amazon.sg/s?k=5+way+digital+navigation+joystick+module), [BTN](https://www.amazon.sg/s?k=momentary+tactile+push+button), [AMP](https://www.amazon.sg/s?k=MAX98357A+I2S+amplifier), [SPK](https://www.amazon.sg/s?k=speaker+4+ohm+3W) | [BB](https://www.amazon.sg/s?k=830+solderless+breadboard), [WIRE](https://www.amazon.sg/s?k=2.54mm+jumper+wires+male+female), [USB](https://www.amazon.sg/s?k=USB+data+cable), [HDR](https://www.amazon.sg/s?k=2.54mm+male+pin+header), [MODE](https://www.amazon.sg/s?k=47k+ohm+resistor), [CONN](https://www.amazon.sg/s?k=2+pin+wire+connector) | [Amicus](https://amicus.com.sg/) |
| South Africa | [MCU](https://www.amazon.co.za/s?k=ESP32-C6+DevKitC-1+N8), [TFT](https://www.amazon.co.za/s?k=ILI9341+2.8+SPI+320x240), [JOY](https://www.amazon.co.za/s?k=5+way+digital+navigation+joystick+module), [BTN](https://www.amazon.co.za/s?k=momentary+tactile+push+button), [AMP](https://www.amazon.co.za/s?k=MAX98357A+I2S+amplifier), [SPK](https://www.amazon.co.za/s?k=speaker+4+ohm+3W) | [BB](https://www.amazon.co.za/s?k=830+solderless+breadboard), [WIRE](https://www.amazon.co.za/s?k=2.54mm+jumper+wires+male+female), [USB](https://www.amazon.co.za/s?k=USB+data+cable), [HDR](https://www.amazon.co.za/s?k=2.54mm+male+pin+header), [MODE](https://www.amazon.co.za/s?k=47k+ohm+resistor), [CONN](https://www.amazon.co.za/s?k=2+pin+wire+connector) | [Micro Robotics](https://www.robotics.org.za/) |
| Spain | [MCU](https://www.amazon.es/s?k=ESP32-C6+DevKitC-1+N8), [TFT](https://www.amazon.es/s?k=ILI9341+2.8+SPI+320x240), [JOY](https://www.amazon.es/s?k=5+way+digital+navigation+joystick+module), [BTN](https://www.amazon.es/s?k=momentary+tactile+push+button), [AMP](https://www.amazon.es/s?k=MAX98357A+I2S+amplifier), [SPK](https://www.amazon.es/s?k=speaker+4+ohm+3W) | [BB](https://www.amazon.es/s?k=830+solderless+breadboard), [WIRE](https://www.amazon.es/s?k=2.54mm+jumper+wires+male+female), [USB](https://www.amazon.es/s?k=USB+data+cable), [HDR](https://www.amazon.es/s?k=2.54mm+male+pin+header), [MODE](https://www.amazon.es/s?k=47k+ohm+resistor), [CONN](https://www.amazon.es/s?k=2+pin+wire+connector) | [BricoGeek](https://tienda.bricogeek.com/) |
| Sweden | [MCU](https://www.amazon.se/s?k=ESP32-C6+DevKitC-1+N8), [TFT](https://www.amazon.se/s?k=ILI9341+2.8+SPI+320x240), [JOY](https://www.amazon.se/s?k=5+way+digital+navigation+joystick+module), [BTN](https://www.amazon.se/s?k=momentary+tactile+push+button), [AMP](https://www.amazon.se/s?k=MAX98357A+I2S+amplifier), [SPK](https://www.amazon.se/s?k=speaker+4+ohm+3W) | [BB](https://www.amazon.se/s?k=830+solderless+breadboard), [WIRE](https://www.amazon.se/s?k=2.54mm+jumper+wires+male+female), [USB](https://www.amazon.se/s?k=USB+data+cable), [HDR](https://www.amazon.se/s?k=2.54mm+male+pin+header), [MODE](https://www.amazon.se/s?k=47k+ohm+resistor), [CONN](https://www.amazon.se/s?k=2+pin+wire+connector) | [Electrokit](https://www.electrokit.com/) |
| Turkey | [MCU](https://www.amazon.com.tr/s?k=ESP32-C6+DevKitC-1+N8), [TFT](https://www.amazon.com.tr/s?k=ILI9341+2.8+SPI+320x240), [JOY](https://www.amazon.com.tr/s?k=5+way+digital+navigation+joystick+module), [BTN](https://www.amazon.com.tr/s?k=momentary+tactile+push+button), [AMP](https://www.amazon.com.tr/s?k=MAX98357A+I2S+amplifier), [SPK](https://www.amazon.com.tr/s?k=speaker+4+ohm+3W) | [BB](https://www.amazon.com.tr/s?k=830+solderless+breadboard), [WIRE](https://www.amazon.com.tr/s?k=2.54mm+jumper+wires+male+female), [USB](https://www.amazon.com.tr/s?k=USB+data+cable), [HDR](https://www.amazon.com.tr/s?k=2.54mm+male+pin+header), [MODE](https://www.amazon.com.tr/s?k=47k+ohm+resistor), [CONN](https://www.amazon.com.tr/s?k=2+pin+wire+connector) | [Robotistan](https://www.robotistan.com/) |
| United Arab Emirates | [MCU](https://www.amazon.ae/s?k=ESP32-C6+DevKitC-1+N8), [TFT](https://www.amazon.ae/s?k=ILI9341+2.8+SPI+320x240), [JOY](https://www.amazon.ae/s?k=5+way+digital+navigation+joystick+module), [BTN](https://www.amazon.ae/s?k=momentary+tactile+push+button), [AMP](https://www.amazon.ae/s?k=MAX98357A+I2S+amplifier), [SPK](https://www.amazon.ae/s?k=speaker+4+ohm+3W) | [BB](https://www.amazon.ae/s?k=830+solderless+breadboard), [WIRE](https://www.amazon.ae/s?k=2.54mm+jumper+wires+male+female), [USB](https://www.amazon.ae/s?k=USB+data+cable), [HDR](https://www.amazon.ae/s?k=2.54mm+male+pin+header), [MODE](https://www.amazon.ae/s?k=47k+ohm+resistor), [CONN](https://www.amazon.ae/s?k=2+pin+wire+connector) | [International catalog](https://www.digikey.com/) |
| United Kingdom | [MCU](https://www.amazon.co.uk/s?k=ESP32-C6+DevKitC-1+N8), [TFT](https://www.amazon.co.uk/s?k=ILI9341+2.8+SPI+320x240), [JOY](https://www.amazon.co.uk/s?k=5+way+digital+navigation+joystick+module), [BTN](https://www.amazon.co.uk/s?k=momentary+tactile+push+button), [AMP](https://www.amazon.co.uk/s?k=MAX98357A+I2S+amplifier), [SPK](https://www.amazon.co.uk/s?k=speaker+4+ohm+3W) | [BB](https://www.amazon.co.uk/s?k=830+solderless+breadboard), [WIRE](https://www.amazon.co.uk/s?k=2.54mm+jumper+wires+male+female), [USB](https://www.amazon.co.uk/s?k=USB+data+cable), [HDR](https://www.amazon.co.uk/s?k=2.54mm+male+pin+header), [MODE](https://www.amazon.co.uk/s?k=47k+ohm+resistor), [CONN](https://www.amazon.co.uk/s?k=2+pin+wire+connector) | [Pimoroni](https://shop.pimoroni.com/) |
| United States | [MCU](https://www.amazon.com/s?k=ESP32-C6+DevKitC-1+N8), [TFT](https://www.amazon.com/s?k=ILI9341+2.8+SPI+320x240), [JOY](https://www.amazon.com/s?k=5+way+digital+navigation+joystick+module), [BTN](https://www.amazon.com/s?k=momentary+tactile+push+button), [AMP](https://www.amazon.com/s?k=MAX98357A+I2S+amplifier), [SPK](https://www.amazon.com/s?k=speaker+4+ohm+3W) | [BB](https://www.amazon.com/s?k=830+solderless+breadboard), [WIRE](https://www.amazon.com/s?k=2.54mm+jumper+wires+male+female), [USB](https://www.amazon.com/s?k=USB+data+cable), [HDR](https://www.amazon.com/s?k=2.54mm+male+pin+header), [MODE](https://www.amazon.com/s?k=47k+ohm+resistor), [CONN](https://www.amazon.com/s?k=2+pin+wire+connector) | [Adafruit](https://www.adafruit.com/) |

## Other countries and destinations

For **any country not listed above**, use the same per-part specialist links,
select your destination in [DigiKey](https://www.digikey.com/), and consult the
[country-based distributor directory](https://www.adafruit.com/distributors)
for a local alternative. Amazon has no dedicated shopping store for every
country. Where cross-border Amazon delivery is offered, choose a store above
and set the delivery address first; eligibility is checked per item.

For example, readers in Austria or Switzerland can try the Germany searches,
and readers in Portugal can try the Spain searches, subject to delivery checks.
The directory also provides local options for China, South Korea, Taiwan,
Malaysia, the Philippines, and other countries without a row here. This is a
worldwide sourcing route, not a country-by-country shipping guarantee.

The existing [Digirak Amazon Italy listing](https://www.amazon.it/dp/B07HBPW3DF)
was recorded with the PCB reference. Its live availability could not be verified
during this review; use the JOY searches or the documented alternative above.

## Keeping this list current

Follow [the hardware agent instructions](AGENTS.md). Update quantities,
compatibility notes and affected buying links as part of any hardware change.
Record the review date and any links that could not be checked. Keep wiring in
[hardware.md](hardware.md), audio behavior in [the audio guide](../tools/audio.md),
and fabrication details in [the PCB reference](../pcb/README.md).
This document is maintained with repository changes; it is not a live stock or
price monitor.
