# Entwicklung der Elektronik-Testfirmware

Stand: 2026-10-06. Dieser Ordner ist absichtlich vom Produktivcode getrennt. Änderungen an `firmware/`, `core/`, `communication/`, `docs/` oder der Root-`CMakeLists.txt` gehören nicht zu diesem Test.

## Ziel

Vor der weiteren STM32-Integration soll die Verdrahtung des Prüfstands allein prüfbar sein:

- jeder Motor in fünf festen Geschwindigkeitsstufen
- Kraftmessdose nur als Spannung in Volt
- Messtaster, Endanschläge und E-Stop nur als High/Low
- Bedienung und Anzeige gleichzeitig am Raspberry Pi (USART3) und am Laptop über den ST-Link (USART2)

Der Test ist ein Wegwerf-Image für die Inbetriebnahme, kein Schritt der Produktivphasen.

## Warum ein eigener Ordner

Die Produktivfirmware unter `firmware/` ist bei Phase 2.1: ADC, GPIO, USART3-Echo, Core-Tick. Schrittimpulse sind dort noch nicht implementiert (Phase 3). Diesen Stand mit einem vereinfachten Motorlauf zu vermischen würde den Core, den Watchdog und das spätere M12-Protokoll auf demselben Bild mitlaufen lassen. Ein ADC-Rohwert könnte dort schon einen Fault auslösen.

Deshalb linkt `tec_hwtest` weder `core/` noch `communication/` noch eine Quelldatei aus `firmware/src`. Pinbelegung und Toolchain sind lokale Kopien. Sie werden nicht automatisch nachgezogen, wenn sich der Produktivcode ändert.

Das Hexfile liegt im selben Flash des STM32F446RE. Flashen von `tec_hwtest.hex` überschreibt das Produktivimage auf dem Chip. Der Quellbaum `firmware/` bleibt dabei unverändert. Zurück geht es nur durch erneutes Flashen von `tec_firmware.hex`.

## Quellen, die dieser Test übernimmt

Kanonisch ist der Stand nach dem 2026-10-05 aus dem Chat zur STM32-Integration und aus diesen Dateien (nur gelesen, nicht geändert):

- `firmware/include/PinMap.hpp`
- `firmware/README.md` (Phase-2.1-Bench, Takt, Build-Werkzeuge)
- `firmware/CMakeLists.txt` (HAL-Versionen und Compilerflags)
- `.cursor/plans/m19.x_stm32_plan_5f56eecd.plan.md`
- `docs/Force_Calibration.md` (Ruhepunkt etwa 1,5 V, Anzeige hier trotzdem nur Volt)
- `docs/Homing.md` (Endanschlag High = ausgelöst, Referenz am Plus-Maximum)

Ältere Festlegungen gelten nicht mehr: Pi-UART ist nicht USART2 auf PA2/PA3, die Step-Pins sind nicht PC0/PC2/PC4 und nicht PB7 für Y. Y-Step ist PB4. SB13/SB14 bleiben geschlossen.

## Kopien, Stand 2026-10-06

| Datei hier | Herkunft | Abweichung |
| --- | --- | --- |
| `cmake/arm-none-eabi-stm32f446.cmake` | `firmware/cmake/` | nur der Kommentar |
| `linker/STM32F446RETX_FLASH.ld` | `firmware/linker/` | nur der Kommentar |
| `include/stm32f4xx_hal_conf.h` | `firmware/include/` | TIM an, IWDG aus. DMA bleibt an, weil die HAL-Header den Typ `DMA_HandleTypeDef` verlangen. Gestartet wird kein DMA. |
| `include/PinMap.hpp` | `firmware/include/PinMap.hpp` | plus Makros für USART2 PA2/PA3, die dort nur im Kommentar stehen |
| `src/syscalls.cpp` | gleicher Stub-Satz wie `firmware/src/syscalls.cpp` | neu geschrieben, nicht eingebunden |

HAL, CMSIS und das Startup-Assemblerfile werden beim Build frisch nach `firmware_hwtest/build` geladen (CMSIS 5.9.0, Device F4 v2.6.10, HAL v1.8.3). Das sind dieselben Tags wie in der Produktiv-`CMakeLists.txt`.

## Verhalten, das vom Phase-2.1-Bench abweicht

Der Phase-2.1-Bench spricht nur USART3 und hält die Step-Pins Low. Das war richtig für den Core-Bring-up. Für diesen Test gilt:

- USART2 und USART3 gleichzeitig. Jede UART hat einen eigenen Empfangspuffer, damit sich zwei Eingaben nicht zu einer Zeile vermischen. Jede Ausgabe geht an beide Schnittstellen, damit beide Monitore dasselbe zeigen.
- Fallback ist eine Konstante, keine Laufzeitumschaltung: `kConsole` in `include/HwTestConfig.hpp`.
- Kraft als Volt, nicht als Newton und nicht als ADC-Counts. Drei Kanäle, weil die Dose Fx/Fy/Fz auf PA0/PA1/PA4 liegt.
- Messtaster und Endanschläge werden als Rohpegel ausgegeben. Die Produktivfirmware deutet den Messtaster zusätzlich (Low = Kontakt). Das bleibt hier dem Leser der Statuszeile überlassen.
- Schrittimpulse über TIM4 (X, PB6), TIM3 (Y, PB4) und TIM10 (Z, PB8). Die Produktivfirmware hat diese Timer noch nicht.
- Kein unabhängiger Watchdog. Ein Breakpoint am ST-Link soll den Chip nicht zurücksetzen. Dafür gibt es keinen automatischen Reset, wenn die Schleife hängen bleibt.
- Pneumatik wird nicht geschaltet. PC8, PC12 und PC9 sind Ausgänge und bleiben Low. Beide Spreizerleitungen High ist verboten und wird nicht erzeugt. Beide Low ist der stromlose Zustand, solange dieser Test die Ventile nicht anfasst.
- PC7 blinkt mit 1 Hz als Lebenszeichen. Die Nucleo-LED auf PA5 wird nicht benutzt.

## Takt und Pulsfrequenz

Takt wie in der Produktivfirmware, ohne LSI (der war nur für den Watchdog): HSE 8 MHz vom ST-Link, PLL auf 180 MHz, APB1 = 45 MHz, APB2 = 90 MHz. Timer an einem geteilten APB-Bus bekommen den doppelten Takt: TIM3/TIM4 = 90 MHz, TIM10 = 180 MHz. Diese beiden Zahlen stehen als `kTimClkApb1Hz` und `kTimClkApb2Hz` in `src/main.cpp`. Wer den Taktbaum ändert, muss sie mitändern.

Daraus wird ein 1-MHz-Tick (Prescaler 89 bzw. 179) und daraus die Stufenfrequenz über das Auto-Reload-Register. 50 % Duty. Die PWM-Frequenz ist die Schrittfrequenz.

Stufen, absichtlich unter den späteren Soft-Limits (XY 50 mm/s, Z 25 mm/s):

| Stufe | Hz | mm/s bei 200 Schritten/mm |
| --- | --- | --- |
| 1 | 100 | 0,5 |
| 2 | 200 | 1 |
| 3 | 500 | 2,5 |
| 4 | 1000 | 5 |
| 5 | 2000 | 10 |

Richtung wird mindestens 1 ms vor dem ersten Impuls gesetzt. Beim Stopp geht der Step-Pin zurück auf GPIO Low, damit der Treiber keine offenen PWM-Flanken sieht.

PB4 ist nach dem Reset NJTRST. Der Test schaltet den Pin auf GPIO und später auf TIM3. PA13/PA14 (SWD) werden nicht angefasst, der ST-Link-Debug bleibt nutzbar.

## Serielles Protokoll

ASCII, eine Zeile, abgeschlossen mit CR oder LF. Kein M12, keine CRC.

Befehle: `?`, `H`/`HELP`, `STOP`, `X+3`, `X-1`, `X0` und dasselbe für Y und Z. Stufe nur 1 bis 5. Antwort `OK …` oder `ERR cmd` / `ERR estop` / `ERR limit`.

Status alle 500 ms:

```
HW Fx=1.523 Fy=1.501 Fz=1.488 V | probe=High | estop=Low | X-=Low X+=High Y-=Low Y+=Low Z-=Low Z+=Low | mot X=0 Y=+3 Z=0
```

Volt = gerundete Millivolt aus `counts * 3300 / 4095`. 4095 entspricht 3,300 V.

E-Stop High stoppt alle laufenden Achsen und lehnt neue Fahrbefehle ab. Ein Endanschlag stoppt nur die Achse, die in diese Richtung läuft. Die Meldung `STOP estop` oder `STOP X limit` kommt einmal beim Übergang, nicht in jeder Statuszeile. Die Statuszeile zeigt die Pegel weiter roh an.

Die Sicherheitsabfrage läuft in der Hauptschleife und zwischen den gesendeten Bytes. Die Hardware-Abschaltung des E-Stops ist davon unabhängig und bleibt die eigentliche Leistungsabschaltung.

## Build-Ergebnis

Werkzeug: `arm-none-eabi-gcc` 14.2.1, CMake/Ninja, Befehl wie in `README.md`.

```
text    data    bss    dec
21296   976     3496   25768
```

Artefakte: `firmware_hwtest/build/tec_hwtest.elf`, `.hex`, `.bin`. Der Ordner `build/` ist über die Root-`.gitignore` und über `firmware_hwtest/.gitignore` vom Commit ausgenommen.

Am Prüfstand ist das Image hier nicht gelaufen. Flash, Pi-Konsole und ST-Link-COM prüft die Inbetriebnahme.

## Bekannte Grenzen

- Ohne 5-V-Pegelwandler sind die Step/Dir-Pegel außerhalb der DM542-Spezifikation.
- ENA wird von dieser Firmware nicht geschaltet. Ob der Treiber freigegeben ist, hängt an der bestehenden Verdrahtung und an der Hardware-Freigabe des E-Stops.
- Es gibt keinen Positionszähler. Fünf Stufen sind feste Frequenzen, keine geregelte Fahrt auf einen Weg.
- Zwei Bediener gleichzeitig können sich die Achsen gegenseitig überschreiben. Die Zeilen bleiben je UART intakt, die Wirkung ist die zuletzt ausgeführte.
- Schwebende Analogeingänge zeigen beliebige Spannungen. Das ist hier kein Fault. Für eine ruhige Anzeige die Eingänge beschalten; der erwartete lastfreie Punkt der Dose liegt bei etwa 1,5 V.
- Dieser Test kennt die Produktiv-Softlimits und den Arbeitsraum nicht. Die niedrigen Stufen sind der Schutz, nicht eine Endlagenüberwachung über den Weg.
