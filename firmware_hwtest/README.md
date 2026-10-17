# Elektroniktest STM32F446RE

Eigenständige Testfirmware für den Prüfstand. Sie prüft Motoren, Kraftmessdose, Messtaster, Endanschläge und E-Stop. Sie ersetzt beim Flashen das Image auf dem Chip. Der Quellcode unter `firmware/` bleibt unberührt.

Ausführliche Entwurfsnotizen: [docs/Entwicklung.md](docs/Entwicklung.md).

## Was sie tut

- Drei Schrittachsen einzeln oder gleichzeitig in fünf Geschwindigkeitsstufen.
- Kraft Fx, Fy, Fz als Spannung in Volt (keine Newton-Umrechnung).
- Messtaster, sechs Endanschläge und E-Stop als `High` oder `Low`.
- Dieselbe Textausgabe auf dem ST-Link-Serial-Monitor (Laptop, USART2) und auf dem Raspberry Pi (USART3).

## Was sie nicht tut

- Kein Core, kein M12-Protokoll, keine Pneumatik, kein Positionszähler, kein Watchdog.
- Kein Enable-Pin. Die DM542-ENA-Leitung hängt nicht am STM32.
- Step/Dir verlassen den STM32 mit 3,3 V. Die DM542 brauchen den vorgesehenen 5-V-Pegelwandler. Ohne Wandler können die Treiber die Impulse ignorieren.

## Flash

Das Hexfile liegt nach dem Build unter `firmware_hwtest/build/tec_hwtest.hex`. Flashen mit STM32CubeProgrammer oder OpenOCD über den ST-Link.

Danach läuft auf dem Nucleo nur noch dieser Test. Zurück zur Produktivfirmware: den Build aus `firmware/README.md` erneut flashen (`tec_firmware.hex`). Deren Quelltext wird durch diesen Test nicht verändert.

Lötbrücken SB13 und SB14 geschlossen lassen. Sonst ist der ST-Link-COM-Port weg.

## Serielle Konsole

115200 Baud, 8N1, keine Flusskontrolle, Zeilenende CR oder LF.

| Weg | Peripherie | Pins | Gegenstelle |
| --- | --- | --- | --- |
| Laptop | USART2 | PA2 TX, PA3 RX | ST-Link Virtual COM Port |
| Raspberry Pi | USART3 | PC10 TX, PC11 RX | GPIO15 RX, GPIO14 TX, gemeinsames GND |

Am Pi die PL011-UART auf GPIO14/GPIO15 verwenden und kein Login-Getty auf dieser Schnittstelle laufen lassen, sonst mischt sich der Kernel in den Dialog.

Beide Konsolen sind gleichzeitig aktiv. Befehle dürfen von beiden kommen; praktisch bedient eine Person. Umschalten ohne den restlichen Code anzufassen: in `include/HwTestConfig.hpp` die Zeile

```cpp
constexpr Console kConsole = Console::Both;
```

auf `Console::Usart2Only` (nur Laptop) oder `Console::Usart3Only` (nur Pi) setzen und neu bauen. Die Startzeile nennt die Auswahl (`console=both`, `usart2` oder `usart3`).

## Befehle

| Eingabe | Wirkung |
| --- | --- |
| `?` | Status sofort |
| `H` oder `HELP` | Kurzhilfe, inklusive der Stufen aus der Tabelle |
| `STOP` | alle Achsen aus |
| `+` | **Z:** genau **800** STEP-Pulse in Plusrichtung, dann Stop |
| `-` | **Z:** genau **800** STEP-Pulse in Minusrichtung, dann Stop |
| `+1600` / `-400` | Z-Burst mit anderer Pulsanzahl |
| `X+3` | X in Plusrichtung, Stufe 3 (Dauerlauf) |
| `X-1` | X in Minusrichtung, Stufe 1 |
| `X0` | nur X aus |
| `Y…` `Z+3` … | Dauerlauf wie X (nicht exakte Pulsanzahl) |

Nach einem Z-Burst kommt `DONE Z+` bzw. `DONE Z-`. Bei 1/4-µStep und 200 Vollschritten/U sind 800 Pulse **eine Motorumdrehung**.

Groß- und Kleinschreibung ist egal. Leerzeichen um Vorzeichen und Stufe sind erlaubt (`X + 3`). Eine neue Zeile für dieselbe Achse ersetzt den vorherigen Auftrag. Mehrere Achsen dürfen gleichzeitig laufen. Nach `STOP`, E-Stop oder Endanschlag startet nichts von selbst wieder.

Antworten: `OK X+3`, `OK X0`, `OK STOP`, `ERR cmd`, `ERR estop`, `ERR limit`.

Alle 500 ms, und zusätzlich auf `?`:

```
HW Fx=1.523 Fy=1.501 Fz=1.488 V | probe=High | estop=Low | X-=Low X+=High Y-=Low Y+=Low Z-=Low Z+=Low | mot X=0 Y=+3 Z=0
```

`mot` zeigt `0`, `+3` oder `-1`. Die Pegel sind die Rohpegel am Pin, nicht umgedeutet. Spannung ist `counts * 3,3 / 4095`, auf Millivolt gerundet. Ein fehlgeschlagenes ADC-Reading erscheint als `?.???`.

PC7 blinkt mit 1 Hz, solange die Firmware läuft. PA5 ist der Messtaster und nicht die Nucleo-LED.

## Geschwindigkeit

Die PWM-Frequenz ist die Schrittfrequenz (eine Flanke pro Periode, 50 % Duty). Bei 200 Schritten/mm (SFU1204, 1/4 Mikroschritt):

| Stufe | Frequenz | Geschwindigkeit |
| --- | --- | --- |
| 1 | 100 Hz | 0,5 mm/s |
| 2 | 200 Hz | 1 mm/s |
| 3 | 500 Hz | 2,5 mm/s |
| 4 | 1000 Hz | 5 mm/s |
| 5 | 2000 Hz | 10 mm/s |

Dieselbe Frequenz gilt für X, Y und Z. Die Tabelle steht nur in `include/HwTestConfig.hpp` (`kSpeedHz`). Richtung High an PC1/PC3/PC5 ist die positive Maschinenrichtung.

## Sicherheit in diesem Test

- E-Stop (PA9 High): laufende Achsen stoppen sofort, Fahrbefehle antworten mit `ERR estop`. Die Hardware trennt Motor- und Pneumatikversorgung parallel dazu. Nach dem Lösen des E-Stops bleiben die Motoren aus, bis ein neuer Befehl kommt.
- Endanschlag in Fahrtrichtung High: nur diese Achse stoppt (`STOP X limit`). Die Statuszeile zeigt den Pegel weiter an. Ein Befehl in die bereits ausgelöste Richtung antwortet mit `ERR limit`. Die Gegenrichtung bleibt möglich.
- Pneumatik PC8, PC12 und PC9 bleiben Low. Beide Spreizerausgänge gleichzeitig High kommt nicht vor.
- Nach dem Einschalten sind Step, Richtung, Pneumatik und LED Low.

## Build

GNU Arm Embedded Toolchain 14.x (`arm-none-eabi-gcc`) und Ninja. Eine ältere Nanotec-Toolchain (GCC 4.8) darf nicht zuerst im PATH stehen. Bekannter 14.2-Pfad auf diesem Rechner:

`C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.2 rel1\bin`

Aus der Repo-Wurzel in PowerShell:

```powershell
cmake -S firmware_hwtest -B firmware_hwtest/build -G Ninja --toolchain "$PWD\firmware_hwtest\cmake\arm-none-eabi-stm32f446.cmake"
cmake --build firmware_hwtest/build
```

Ergebnis: `firmware_hwtest/build/tec_hwtest.elf`, `.hex` und `.bin`.

Der Build lädt CMSIS 5.9.0, cmsis_device_f4 v2.6.10 und HAL v1.8.3 per FetchContent in `firmware_hwtest/build`. Das ist dieselbe HAL-Version wie die Produktivfirmware, aber eine eigene Kopie im Test-Buildordner.
