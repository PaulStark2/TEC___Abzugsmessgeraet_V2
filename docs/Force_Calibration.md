# Z-Kraftkalibrierung (Meilenstein A)

Diese Seite beschreibt, welche Messwerte in die Z-Kennlinie eingehen und wie daraus die gespeicherte Funktion entsteht. Die Rechnung liegt im plattformunabhängigen Core. Die GUI sammelt die Punkte; Simulation und STM32-Firmware (`ForceCalibHost`) wenden dieselbe Kennlinie an und senden `MsgRawAdc`.

Die Kennlinie ist eine Regression der Sensorkraft auf den ADC-Zählwert. Sie wird nicht durch die Stützpunkte gezwungen. Ein Punkt, der daneben liegt, bleibt sichtbar als Residuum.

## Vorzeichen

Zug ist negativ, Druck ist positiv. Das gilt für die Kennlinie und für `HostInputs`. Es gibt keine zweite Umkehr an der Host-Grenze. Dieselbe gespeicherte Z-Funktion wird auf die Rohzählwerte aller drei Achsen angewendet:

```191:198:simulation_bridge/SimProtocolBridge.cpp
    force_calib_.observeRaw(simplant::syntheticFzCounts(inputs.force.fx),
                            simplant::syntheticFzCounts(inputs.force.fy),
                            simplant::syntheticFzCounts(inputs.force.fz), true);
    if (force_calib_.active())
    {
        inputs.force.fx = static_cast<float>(force_calib_.evaluateSensorNewton(force_calib_.raw().x));
        inputs.force.fy = static_cast<float>(force_calib_.evaluateSensorNewton(force_calib_.raw().y));
        inputs.force.fz = static_cast<float>(force_calib_.evaluateSensorNewton(force_calib_.raw().z));
    }
```

`evaluateForceCalibration` liefert die Kraft, die der Core sieht. Fx und Fy werden nicht selbst kalibriert; ihre ADC-Werte laufen durch dieselbe Kurve. Ohne gespeicherte Kennlinie bleibt die Plant-Kraft unverändert.

## Welche Daten verwendet werden

| Größe | Herkunft | Verwendung |
|--------|----------|------------|
| `adc_free` | Mittel der kraftfreien Z-Rohwerte | Stützpunkt 0 g und Bezug für den Adapter |
| `adc_adapter` | Mittel der Z-Rohwerte mit Adapter, ohne Zusatzlast | Nur zur Differenz `adc_adapter − adc_free` |
| `adc_net` | Mittel der Lastmessung minus `adc_adapter − adc_free` | Abszisse jedes Lastpunktes |
| `mass_g` | Eingabe in Gramm | Ordinate, nach Umrechnung in Newton |
| `nominal_g` | Sollstufe des Schritts | Nur die ±100-g-Prüfung, nicht die Regression |
| Richtung | Schritt, nicht die Eingabe | Vorzeichen der Referenzkraft |

Jede Aufnahme liest 20 Z-Rohwerte im Abstand von 100 ms. Gespeichert wird der gerundete Mittelwert der gültigen Werte. Fehlen einzelne Werte, bleibt der Punkt gültig. Die kraftfreie Messung speichert diesen Mittelwert als Nullpunkt. Jeder Lastpunkt zieht vorher das Adaptergewicht ab: Mittelwert minus `adc_adapter − adc_free`.

Die Regression sieht diese Nettozählwerte. Sie sieht nicht die beiden Rohmessungen einzeln und nicht die Grammzahl der Sollstufe.

## Gramm nach Newton

Normfallbeschleunigung `g0 = 9,80665 m/s²` (`core/ForceCalibration.hpp`, Zeile 20). Die Masse wird in Kilogramm umgerechnet. Zug bekommt ein negatives, Druck ein positives Vorzeichen, der Nullpunkt bleibt 0:

```284:295:core/ForceCalibration.cpp
double gramsToSensorNewton(double mass_g, ForceCalibDirection direction)
{
    const double magnitude = (mass_g / 1000.0) * kForceCalibStandardGravity;
    if (direction == ForceCalibDirection::Tension)
    {
        return -magnitude;
    }
    // ...
}
```

500 g Druck sind damit 4,903325 N, nicht 5 N. Dieselbe Konstante steht in der GUI nur für die Anzeige in Gramm (`gui/public/js/pages/forceWizard.js`, Zeile 14). Gespeichert und gerechnet wird in Newton.

Ein Lastpunkt wird verworfen, wenn die eingegebene Masse mehr als 100 g vom Soll abweicht oder negativ ist (`referenceMassAccepted`, `core/ForceCalibration.cpp` ab Zeile 298). Der Nullpunkt verlangt Masse und Soll exakt 0. Die GUI prüft dasselbe Fenster, bevor sie den Punkt überhaupt sendet.

## Regressionsmodell

Gesucht ist

```text
F_sensor(adc) = β0 + β1·adc + β2·adc²
```

mit `β2 = 0` bei der Geraden. Auswertung:

```319:322:core/ForceCalibration.cpp
double evaluateForceCalibration(const ForceCalibModel& model, double adc_counts)
{
    return model.beta0 + model.beta1 * adc_counts + model.beta2 * adc_counts * adc_counts;
}
```

`adc` sind Zählwerte, `F_sensor` ist Newton im Sensorvorzeichen. Es wird Kraft auf ADC regressiert, weil das Gerät aus Zählwerten eine Kraft ausgeben muss. Die Umkehrung eines quadratischen Spannungsverlaufs wäre mehrdeutig.

### Zentrierung

Die Normalgleichungen werden nicht in rohen Zählwerten aufgestellt. Für die Stichprobe mit Mittelwert `ā` und Skala `s = sqrt(Σ(adc − ā)² / n)` lautet die Hilfsgröße

```text
u = (adc − ā) / s
```

`fitDegree` bildet die Gram-Matrix von `{1, u}` oder `{1, u, u²}` und die rechte Seite `Xᵀy` (`core/ForceCalibration.cpp`, Zeilen 115–137). Das Gleichungssystem wird mit Gauß-Elimination und Teilpivot gelöst (`solveNormal`, ab Zeile 38). Es gibt keine externe Algebra-Bibliothek.

Die Lösung in `u` wird anschließend in Koeffizienten von `adc` zurückgerechnet:

```97:112:core/ForceCalibration.cpp
void betasFromScaled(const double* scaled, int degree, double mean, double scale, ForceCalibModel& model)
{
    const double inv_s = 1.0 / scale;
    if (degree == 1)
    {
        model.beta1 = scaled[1] * inv_s;
        model.beta0 = scaled[0] - model.beta1 * mean;
        // ...
    }
    model.beta2 = scaled[2] * inv_s * inv_s;
    model.beta1 = scaled[1] * inv_s - 2.0 * mean * model.beta2;
    model.beta0 = scaled[0] - scaled[1] * inv_s * mean + model.beta2 * mean * mean;
}
```

Gespeichert werden nur `β0`, `β1`, `β2` in Zählwerten. Mittelwert und Skala braucht die Auswertung nicht mehr.

### Güte

Für das gewählte Modell sind Residuum, Bestimmtheitsmaß und Pearson-Korrelation definiert als

```text
e_i = F_i − F̂(adc_i)
R²  = 1 − Σ e_i² / Σ (F_i − F̄)²
r   = Σ (adc_i − ā)(F_i − F̄) / sqrt(Σ(adc_i − ā)² · Σ(F_i − F̄)²)
```

`r` wird berichtet (`pearsonR`, ab Zeile 196) und entscheidet das Modell nicht. `R²` und das maximale `|e|` stehen im Bericht. Die mittleren Residuen von Zug und Druck werden getrennt ausgegeben; daraus wird kein zweites Modell.

## Wann die Parabel die Gerade ersetzt

Beide Grade werden gerechnet, sobald mindestens drei Punkte vorliegen (Gerade ab zwei Punkten). Automatisch gewinnt die Parabel nur, wenn alle folgenden Bedingungen gelten (`quadraticBeatsLine`, Zeilen 251–279):

1. Mindestens sechs Punkte.
2. Die Gerade hat ein maximales Residuum größer als 0,3 % der Spanne. Die Spanne ist der doppelte Messbereich. Beim Default ±30 N sind das 60 N und eine Schwelle von 0,18 N.
3. Die partielle F-Statistik für den einen zusätzlichen Koeffizienten liegt über dem kritischen Wert `F(1, n−3)` bei `α = 0,05`. Die Tabelle steht in `kFCrit05` (Zeilen 10–16); Freiheitsgrade über 30 verwenden den Wert für 30.
4. Die Parabel ist auf dem ADC-Intervall der Punkte streng steigend.

```423:426:core/ForceCalibration.cpp
    const bool want_quadratic =
        requested == ForceCurveSource::Quadratic ||
        (requested == ForceCurveSource::Auto &&
         quadraticBeatsLine(report.linear, report.quadratic, count, full_scale_span_N));
```

Die Steigung `β1 + 2·β2·adc` wird an beiden Intervallenden geprüft, weil die Ableitung einer Parabel linear ist (`forceCurveMonotonicRising`, Zeilen 324–337). Eine fallende oder umkehrende Kennlinie wird nicht übernommen.

Liegt das maximale Residuum über 1 % der Spanne (beim Default 0,6 N), speichert der Automatikmodus nicht. Eine manuell festgelegte Gerade oder Parabel darf diese Grenze nach einer zweiten Bestätigung überschreiten (`fitForceCalibration`, Zeilen 462–468). Nicht endliche Koeffizienten und eine nicht monotone Kurve werden immer abgelehnt.

Grad größer als 2 ist nicht vorgesehen.

## Manuelle und externe Kennlinie

Im Kalibriermenü kann die Quelle jederzeit gewechselt werden:

- Automatisch verwendet die Kriterien oben und rechnet die gespeicherten Punkte neu.
- Gerade fest und Parabel fest rechnen dieselben Punkte mit festem Grad. Ohne gespeicherte Punkte ist das nicht möglich.
- Eine externe Gleichung `F = β0 + β1·x + β2·x²` ersetzt die Koeffizienten. `x` darf ein Zählwert oder eine Spannung 0…3,3 V sein. Spannung wird mit `V = adc · 3,3 / 4095` umgerechnet, gespeichert wird nur die ADC-Form:

```500:503:core/ForceCalibration.cpp
        const double k = kForceCalibVref / static_cast<double>(kForceCalibAdcFullScale);
        model.beta1 = beta1 * k;
        model.beta2 = beta2 * k * k;
```

Die externe Kurve muss auf dem ganzen Bereich 0…4095 streng steigen. Ihre Abweichung zu alten Punkten wird angezeigt und ist kein Ablehnungsgrund. Die alten Punkte bleiben gespeichert, damit Automatik, Gerade und Parabel wieder wählbar sind.

Eine Vorschau (`commit = 0`) ersetzt die aktive Kennlinie nicht. Erst `commit = 1` bei einem angenommenen Bericht schreibt sie nach `calibration.json` unter `forceZ` und in den Controller.

## Ablauf in der GUI

1. Bestätigung, dass neu kalibriert werden soll. Die bisherige Kennlinie bleibt bis zum Speichern gültig.
2. Fahrt zum Tastermittelpunkt bei Z = −5 mm. Während der Fahrt ist Jog gesperrt und die Phasengrenze `collisionApproach` bleibt aktiv. Danach ist diese Phasengrenze aus, Jog und Spreizer bleiben verfügbar. Die harte Grenze ist der Parameter `force_hard_limit_N`, Default 30 N. STOP bricht ab, schaltet die Phasengrenze wieder ein und verwirft nur die laufende Sitzung.
3. Kraftfreie Messung, dann Adapter einspannen, dann Adaptermessung. Jede Aufnahme mittelt bis zu 20 Werte in 2 Sekunden. Der Bediener übernimmt den Mittelwert oder wiederholt den Punkt.
4. Zug- und Druckstufen aus den Parametern. Default sind fünf Stufen je Richtung mit 500 g Schritt, also 500, 1000, 1500, 2000 und 2500 g. Anzahl 1…16 und Schrittweite größer als 200 g stehen in der Parameterdatei und nicht im MachineConfig-Frame. Die letzte Stufe muss im Messbereich liegen.
5. Druck: Waage nullen, auf die Waage fahren, abgelesene Gramm eintragen.
6. Nach dem Speichern zeigt dieselbe Ansicht die Kennlinie und die Live-Kräfte. Fx, Fy und Fz erscheinen in Newton im Vorzeichen der Kennlinie. Der Satz zur Vorzeichenkonvention steht auf der Seite.

## Prüfung in der Simulation

Die Zählwerte sind hier keine Messung. Der Simulationshost rechnet sie aus der injizierten Prozesskraft. Zwei Fenster: die Maschinen-GUI unter Kalibrieren und die Sim-Oberfläche unter `/simulation`. GUI und `simulation_protocol` müssen aus diesem Stand laufen, sonst bleibt `adcValid` leer.

1. MCS, Messtaster und Referenzfahrt sind gültig.
2. Kraftmessdose starten und die Anfahrt ausführen. Während der Anfahrt gilt `collisionApproach`. Danach nicht mehr, deshalb lösen 24,5 N kein `ForceCollision` aus, solange die harte Grenze darüber liegt. Default der harten Grenze ist 30 N. Bei gelöster Kraft muss die Live-Zeile einen ADC Z zeigen. Kraftfreie Messung und Adaptermessung beide ohne eingerastete Kraft aufnehmen, damit die Adapterdifferenz 0 ist.
3. Pro Stufe den Betrag unten eintragen, in der Sim-Oberfläche +Z oder −Z anklicken und im Wizard die Soll-Gramm eintragen. +Z und −Z bleiben stehen, bis „Kraft lösen“ gedrückt wird. −Z ist Zug, +Z ist Druck. X und Y nicht einrasten.

- 500 g → 4,903 N
- 1000 g → 9,807 N
- 1500 g → 14,710 N
- 2000 g → 19,613 N
- 2500 g → 24,533 N

4. Speichern. Dieselbe Stufe noch einmal einrasten: die angezeigte Prozesskraft soll nahe der injizierten Kraft liegen. Automatik bleibt bei dieser synthetischen Dose eine Gerade, weil die Krümmung unter 0,18 N liegt.

## Vertrag für den Controller

Nachrichten, ohne die 41-Byte-Statusmeldung zu verändern:

| ID | Name | Inhalt |
|----|------|--------|
| `0x59` | `CmdSetForceCalibration` | Kopf 4 Byte, danach Punkte zu 12 Byte (ADC, Richtung, Masse, Soll) |
| `0x5A` | `CmdSelectForceCurve` | 16 Byte: Quelle, Bestätigung, Einheit, drei Koeffizienten |
| `0x5B` | `CmdSetCollisionGuard` | 1 Byte: Phasenprüfung ein (1) oder aus (0). Die harte Grenze bleibt |
| `0xA4` | `MsgRawAdc` | 7 Byte: drei uint16 und ein Gültigkeitsflag |
| `0xA5` | `MsgForceCalibration` | 88 Byte Kopf mit beiden Modellen, danach die Referenzkräfte |

`CmdSetMachineConfig` ist 296 Byte. Am Ende stehen `force_range_N` und `force_hard_limit_N`, beide Default 30 N. Die Notbremse im Core ist 500 N. StatusReport bleibt 41 Byte. Anzahl und Schrittweite der Kalibrierung gehen nicht in diesen Frame.

Die Firmware soll `MsgRawAdc` ausgeben, `0x59`, `0x5A` und `0x5B` mit demselben Core rechnen und die gespeicherte Kurve auf alle drei Kanäle anwenden: `F = β0 + β1·adc + β2·adc²`, danach `HostInputs.fx/fy/fz = F` ohne Vorzeichenwechsel. Bis Phase 7 gilt auf dem STM32 die provisorische Gerade 0…4095 → ±`force_range_N` (Default ±30 N). Die Simulation bildet für X, Y und Z dieselbe synthetische Kennlinie aus den Stützpunkten (−25 N, 0,3 V), (0 N, 1,5 V) und (+25 N, 2,6 V) und wertet die gespeicherte Regression darauf aus. Senkt ein neuer Messbereich die gespeicherte Kennlinie unter ihren äußersten Punkt, verwirft der Host die Kurve. Anheben des Messbereichs lässt sie stehen.
