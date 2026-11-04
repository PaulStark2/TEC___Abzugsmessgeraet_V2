# XY-Kraftkorrektur vor dem Einspannen

Stand: Implementierung im Core, Meilensteine M10 (Regler) und M17/M18 (Einbindung in die Prüfprozedur). Dieses Dokument beschreibt nur das, was der Code tut. Es ist die Grundlage, um die Querkräfte vor dem Spreizen wissenschaftlich einzuordnen. Es ist keine Identifikation einer mechanischen Steifigkeit und kein Nachweis, dass die Korrektur an der realen Dose stabil ist.

Die Querkräfte werden **nicht** als XY-Bahnregelung während des Z-Einfahrhubs genutzt. `Fx` und `Fy` steuern die Bohrungssuche nur in `PreClampXyCorrect` (zweimal pro Prüfpunkt: vor Seat und vor der Prüfphase). Während `TestOrAge` werden Lateralkräfte zusätzlich gegen `test_lateral_force_N` überwacht (Anomalie, keine XY-Korrektur).

```text
Safe-Z → XY zum Prüfpunkt → Z grob → Z fein
  → PreClampXyCorrect (BoreSearch: Fx, Fy)
  → Clamp → SeatToForce → Unclamp AfterSeat
  → PreClampXyCorrect → Clamp → TestOrAge → Unclamp AfterTest
  → ProbeRemeasure → NextPoint
```

Belegt in `docs/M18_Procedure.md` (Ablauf und Regel S10) und im Phasenwechsel des Sequencers.

## 1. Rolle im System

Der Regler liegt im plattformunabhängigen Core (`Machine`). Die GUI rechnet keine Kraft und keine Bahn. Der Host (Simulationsbrücke heute, STM32-Host später) liefert `HostInputs.force` in Newton. Der Core kennt keinen ADC.

`Fx`, `Fy`, `Fz` sind Komponenten entlang der positiven Maschinenachsen. Ein positiver Wert zeigt in Richtung +Achse. `{0,0,0}` ist der Nullvektor.

```6:14:core/HostInputs.hpp
struct ForceVector
{
    // Force component along machine +X axis [N].
    float fx;
    // Force component along machine +Y axis [N].
    float fy;
    // Force component along machine +Z axis [N].
    float fz;
};
```

Die Simulation bildet für den Prozess nur eine Z-Feder ab (`Fx = Fy = 0` im Prozessmodell). Injizierte Querkräfte aus der Sim-Oberfläche bleiben erhalten und werden nicht aus der Pose berechnet. Eine laterale Kontaktsteifigkeit gibt es im Plant nicht. Querkräfte an der echten Dose sind damit in der Simulation nicht geschlossen validierbar.

```10:14:simulation_plant/ProcessForceModel.hpp
// 1D process force (Fx=Fy=0). Sign matches Core:
//   InsertInitial unload uses |Fz| for force-free, insert uses Fz >= target_force_N.
```

```168:170:simulation_bridge/SimProtocolBridge.cpp
        // Keep Sim-UI injected Fx/Fy/Fz and add the process Z-spring on top.
        inputs.force.fz +=
            simplant::processFzZ(machine_.z(), process_z_test_, target, k, true, process_force_unloaded_);
```

Der STM32-Host mittelt den ADC, rechnet die Kennlinie und taktet `Machine` mit 1 ms. Die Simulationsbrücke taktet mit 50 ms. Die Glättung in Newton und die XY-Korrektur liegen im Core, damit beide Taktungen dieselbe Zeitkonstante und dieselbe 50-ms-Entscheidung benutzen. Die Prozedur hängt am `ProcedureController`, nicht am Firmware-Stub.

## 2. Wann die Korrektur läuft

`PointApproach` senkt Z ohne XY-Kraftkorrektur ab. Nach dem Stillstand der groben Anfahrt läuft eine Ruheprobe (`ForceTare`). Erst danach beginnt die feine Z-Abfahrt, bereits mit dem neuen Offset. Die Phase endet, wenn die Feinabfahrt steht oder — noch während der Feinabfahrt — `Fz` die Sollkraft erreicht. Dann startet `PreClampXyCorrect` direkt, ohne eine zweite Ruheprobe. Sonst wäre die Kontaktkraft vor der XY-Korrektur wieder genullt.

Beim Eintritt wird die Kollisionsgrenze auf `Contact` gestellt und das Bewegungsprofil auf `v_bore_search`. Beschleunigung und Eckenradius bleiben `a_testpoints` und `corner_testpoints`. Die feine Z-Anfahrt davor bleibt bei `v_fine_testpoints`. `setPreClampXyEnabled` bleibt aus: die alte Hysterese in `Machine` läuft in dieser Phase nicht. Z bleibt stehen. Der Ursprung der Suche ist die Pose beim Eintritt.

Die Suche fährt jede Achse genau einmal, beide Wände. Zuerst die Achse mit dem größeren Betrag, aber nur wenn dieser Betrag die Schwelle `xy_tolerance_N` erreicht. Sonst X. Das Vorzeichen entlastet die Kraft: positives `F` fährt in die negative Achsrichtung. Liegt keine Kraft über der Schwelle, ist das Vorzeichen positiv. Am Sehnenmittelpunkt wird die Achse nicht neu gewählt. Es bleibt die Achse, die noch nicht gefahren wurde. Nur ihr Vorzeichen kommt aus der Kraft an dieser Stelle, wieder positiv, wenn nichts über der Schwelle liegt.

Jede Wandfahrt sucht eine steigende Flanke eines Vorzeichens. Fahrtrichtung und gesuchtes Kraftvorzeichen sind dasselbe: `+Fx` fährt zuerst nach `-X` und speichert den Punkt erst, wenn `-Fx` die Schwelle von unten überschreitet. Die Rückfahrt nach `+X` speichert erst, wenn `+Fx` steigend über die Schwelle geht. Die Rückfahrtrichtung ist die **Umkehr der Out-Richtung** (latched in `BoreSearch`), nicht erneut `seekSign` aus der Momentankraft — sonst würde ein Kraftabfall unter die Schwelle nach dem Stopp wieder `+` wählen und dieselbe Wand erfassen. Ein Fallen durch die Schwelle, auch mit Rauschen auf derselben Flanke, speichert keinen Punkt. Liegt die gesuchte Kraft beim Start einer Out-Fahrt schon über der Schwelle, wird erst wieder darunter gewartet. Die Pose wird gespeichert, nachdem diese steigende Flanke erreicht ist und die Bahn steht. Dieselbe Geschwindigkeit `v_bore_search` und dieselbe Schwelle an allen vier Wänden: die Gruppenlaufzeit des 100-ms-Fensters (50 ms, etwa 0,05 mm bei 1 mm/s) liegt auf beiden Seiten einer Sehne gleich und fällt im Mittelpunkt heraus.

Nach der ersten Sehne fährt die Maschine deren Mittelpunkt an. Von dort folgt die zweite Achse. Die Endlage ist der Mittelpunkt der zweiten Sehne, nicht ein Kreisfit über alle vier Punkte. Liegt am **Ziel** der aktuellen Wandfahrt (`origin ± seek_sign · xy_search_limit`) nach Stillstand keine Wand, bricht die Phase ab. Nicht schon, wenn die Pose nach der Gegenwand noch nahe dem anderen Limit liegt (sonst würde `FirstBack` sofort abbrechen). Die Zeit `xy_correct_timeout_s` begrenzt die ganze Suche inklusive optionaler Wiederholungen (`bore_refine_passes`). Während einer Bestätigungspause läuft sie nicht weiter. `xy_max_attempts` bleibt unbenutzt.

Mit `bore_refine_passes > 0` startet die Suche vom gefundenen Mittelpunkt erneut, solange am Endpunkt keine Restkraft über der Schwelle liegt. Das Zeitbudget wird nicht zurückgesetzt. Bei Restkraft und `preclamp_confirm` hält die Maschine an; nach Resume folgt Clamp, keine weitere Wiederholung.

Am Sehnenmittelpunkt ist die Lage auffällig, wenn `|Fx|` oder `|Fy|` über der Schwelle liegt. Mit `preclamp_confirm` (Default an) hält die Maschine an. Die GUI zeigt Kräfte und den Radius der zweiten Sehne und wartet auf Weiter (`CmdResume` → Clamp) oder Stopp. Ohne Bestätigung wird derselbe Bericht gesendet und Clamp startet sofort.

Die angefahrene Pose wird in `InsertInitial` als `point_x_` / `point_y_` übernommen. Der spätere Einfahrhub benutzt diese XY-Lage und regelt sie nicht mehr. Ein Test sperrt, dass die Hysterese in `InsertInitial` aus bleibt.

```text
Clamp schaltet setPreClampXyEnabled noch einmal aus.
```

Der Bericht geht als `MsgBoreCenter` (`0xA6`), sobald er frisch ist. StatusReport bleibt 41 Byte.

Die Sehnen-Logik liegt in `core/BoreSearch` und wird auch von der Bauplatten-Kalibrierung (KM/K1–K3) genutzt — dort mit denselben `probe`-Parametern und `z_first` (Floor → Lift → XY) wie das Einmessprotokoll. Siehe [Calibration.md](Calibration.md).

## 2.1 Kraftsignal vom ADC bis zum Entscheid

Der Core sieht keine Zählwerte. Was in `HostInputs.force` steht, ist schon Newton, tariert und — wenn `force_filter > 0` — tiefpassgefiltert. Der Watchdog benutzt dieselbe Tara, aber den ungefilterten Rohwert.

### Mittel auf dem STM32

ADC1 scannt PA0, PA1 und PA4 kontinuierlich per DMA (12 Bit, Abtastzeit 84 Zyklen). Bei 180 MHz SYSCLK ist PCLK2 90 MHz, der ADC-Takt PCLK2/4 also 22,5 MHz. Eine Wandlung dauert 84 + 12 Takte, ein Scan der drei Kanäle 288 Takte, also 78 125 Scans je Sekunde. Der Ring hält 256 Scans, etwa 3,3 ms, damit ein 1-ms-Tick den Puffer nicht überrundet. `consumeMillisecondMean` liest den DMA-Zähler NDTR einmal, bildet daraus den Schreibindex und summiert nur die seit dem letzten Tick fertigen XYZ-Tripletts:

```text
ā_ms = (1/N) · Σ adc_i      N = Anzahl neuer Scans in dieser Millisekunde
```

Fehlt ein neuer Scan, wird der letzte Millisekundenmittelwert gehalten. Gerundet wird hier noch nicht.

### Fenster über 100 ms, vor der Kennlinie

`CountWindow` (`core/CountWindow.hpp`) mittelt diese Zählwerte über `T = 100 ms`. Jeder Tick geht mit seiner Dauer ein, damit 1 ms auf dem STM32 und 50 ms in der Simulation dasselbe Fenster beschreiben:

```text
ā(t) = ( Σ ā_ms,k · Δt_k ) / ( Σ Δt_k )     über alle Slots mit Σ Δt = T
```

Bis das Fenster voll ist, ist der Nenner die bisher aufgelaufene Zeit. Der erste Sample ist damit identisch mit sich selbst. Ein konstanter Zählwert bleibt in jedem Tick exakt. Das Fenster hängt nicht an `force_filter`: ein gespeichertes `force_filter = 0` schaltet nur den Newton-Tiefpass aus.

`T = 100 ms` sind fünf Netzperioden. Die Summe einer Sinusschwingung von 50 Hz und ihrer Harmonischen über eine ganze Zahl ihrer Perioden ist null. Unkorreliertes Rauschen auf den 1-ms-Mitteln fällt in der Standardabweichung um `sqrt(100) = 10`. Eine Streuung von etwa ±1 N auf der Millisekunde liegt danach bei etwa ±0,1 N, wenn sie weiß oder netzfrequent ist. Eine Drift über Sekunden bleibt stehen. 0,1 N ist diese Streuungsgrenze, nicht die Kalibriergenauigkeit.

Die Gruppenlaufzeit eines Rechteckfensters der Länge `T` ist `T/2 = 50 ms`. Bei 2 mm/s sind das 0,1 mm Weg. Ein Sprung ist nach 50 ms zur Hälfte, nach 100 ms vollständig im Mittel.

Die Kennlinie sieht den ungerundeten Mittelwert. Runden davor würde die Quantisierung wieder einführen, die das Fenster gerade entfernt hat. Ohne gespeicherte Kennlinie mappt `countsToNewton` denselben Float linear auf ±`range_N`. Mit Kennlinie gilt dieselbe Z-Funktion für alle drei Kanäle:

```text
u = β0 + β1·ā + β2·ā²
```

Für `β2 ≠ 0` ist `F(E[adc])` nicht `E[F(adc)]`. Der Unterschied ist bei kleiner Streuung `σ²` ungefähr `β2·σ²`. Steht die Parabel auf dem Mittelwert, fällt dieser Term mit `σ²`. `MsgRawAdc` trägt `round(ā)` auf 0…4095, also denselben Mittelwert, den Kennlinie und provisorische Gerade benutzen. Der Wizard liest diese Meldung; seine 20 Aufnahmen im Abstand von 100 ms sind damit Fenster, nicht einzelne Wandlungen.

Die Simulation hat keinen ADC. Sie schiebt die synthetischen Zählwerte der Pflanzenkraft in dasselbe Fenster. Ohne Kennlinie bleibt die Pflanzenkraft selbst der Newtonwert. Mit Kennlinie ersetzt die Kurve auf `ā` die Pflanzenkraft.

### Tiefpass im Core

`u` ist der Newtonwert nach Kennlinie, vor der Tara. Pro Achse, Zeitkonstante `tau = force_filter` in Sekunden:

```text
tau · dy/dt + y = u
Y(s)/U(s) = 1 / (1 + s·tau)
```

Über einen Tick mit gehaltenem `u` ist das exakt

```text
α = 1 − exp(−dt / tau)
y ← y + α · (u − y)
```

`tau = 0` setzt `y = u`. Dieselbe `tau` gilt bei `dt = 1 ms` und bei `dt = 50 ms`. Ein Alpha pro Sample würde das nicht. Default `tau = 0,05 s`, gleich der Entscheidperiode. Die Sprungantwort aus der Ruhe ist `y(t) = u · (1 − exp(−t/tau))`, ohne Überschwingen. Ein konstanter Eingang, der exakt auf einer `≥`-Schwelle liegt, erreicht die Schwelle nur im Grenzwert; eine reale Kraft, die durch die Schwelle läuft, kreuzt sie nach etwa einer Zeitkonstante.

Der erste gültige Rohwert setzt `y = u`. Jeder folgende Kinematik-Tick macht genau einen Schritt. Mehrere `setHostInputs` zwischen zwei Ticks ersetzen nur den Rohwert, sie rechnen den Tiefpass nicht mehrfach.

Die Tara zieht den Offset nach dem Tiefpass ab. Die Ruheprobe selbst mittelt 20 Rohwerte, nicht 20 gefilterte Werte. Der veröffentlichte Wert ist

```text
F = y − o     wenn eine Ruheprobe abgeschlossen ist
F = y         sonst
```

`o` ist der Mittelwert der letzten abgeschlossenen Ruheprobe, achsweise. Der Watchdog und die Fz-Notschwelle in `tickActiveMotion` vergleichen `u − o`, nicht `F`. Eine Überlast muss nicht auf den Tiefpass warten. Status, PreClamp, Einfahrschwellen und der Kraftpfad lesen `F`.

```171:188:core/Machine.cpp
void Machine::publishFilteredForce()
{
    host_inputs_.force = force_tare_.apply(force_filter_.value());
}

void Machine::advanceForceFilter(float dt)
{
    const float tau = machine_config_.force.force_filter;
    if (!(tau > 0.0f) || !(dt > 0.0f))
    {
        force_filter_.reset(raw_force_);
    }
    else
    {
        force_filter_.update(raw_force_, dt, tau);
    }
    publishFilteredForce();
}
```

## 3. Bohrungsmittelpunkt

Vier Wandpunkte eines runden Lochs liegen auf einem Kreis um die Bohrungsmitte. Eine Sehne parallel zur X-Achse hat ihren Mittelpunkt immer auf der Geraden `x = x_c`, unabhängig vom Y-Versatz der Startpose. Das ist die Mittelsenkrechte der Sehne. Entsprechend gilt für eine Y-Sehne `y = y_c`.

Ablauf:

```text
1. Zwei Wände auf der ersten Achse → Sehne 1
2. Mitte von Sehne 1 anfahren
3. Zwei Wände auf der anderen Achse → Sehne 2 (ideal ein Durchmesser)
4. Mitte von Sehne 2 = Bohrungsmittelpunkt
```

```text
cx, cy = Mittelpunkt der zweiten Sehne
radius = halbe Länge der zweiten Sehne
```

`MsgBoreCenter.residual` bleibt im Wire und wird mit 0 gemeldet. Es gibt keinen algebraischen Kreisfit mehr.

Die alte Hysterese in `Machine` (Abschnitt 3.1) wird von der Phase nicht mehr gestartet.

## 3.1 Hysterese, von der Phase nicht mehr gestartet

`tickActiveMotion` enthält weiter den entkoppelten Zweipunkt. Er läuft nur, wenn `pre_clamp_xy_enabled_` gesetzt ist. `PreClampXyCorrect` setzt das Flag nicht. `tests/forcecontrol_tests.cpp` schaltet es direkt am `Machine` ein. `F_i` in den Formeln ist die veröffentlichte Kraft aus Abschnitt 2.1, nicht der ADC-Rohwert.

Der Index `k` zählt Entscheidungen, nicht Maschinen-Ticks. Der Abstand ist fest

```text
T = 0,05 s
t_k = t_Eintritt + k · T
```

Der Zähler läuft, sobald `pre_clamp_xy_enabled_` gesetzt ist, auch wenn beide Hysteresen aus sind. Ein einzelner Ausreißer zwischen zwei Entscheidungen löscht die verstrichene Zeit nicht. An `t_k` werden Hysterese und Schritt aus dem dann gültigen `F` gebildet. Dazwischen bleibt `s_i` stehen und `u_i = 0`.

Einschalt- und Ausschaltschwelle:

```text
F_on  = xy_tolerance_N
F_off = 0.5 · xy_tolerance_N
```

Der Faktor 0,5 ist fest im Code, kein eigener Parameter.

Zustand der Hysterese `s_i(k) ∈ {0, 1}`:

```text
s_i(k) = 1    wenn s_i(k−1) = 0 und |F_i(k)| ≥ F_on
s_i(k) = 0    wenn s_i(k−1) = 1 und |F_i(k)| ≤ F_off
s_i(k) = s_i(k−1)   sonst
```

Stellgröße, nur wenn `s_i(k) = 1` und `F_i(k) ≠ 0`:

```text
u_i(k) = −sign(F_i(k)) · Δ
Δ = xy_correction_step
```

Sonst `u_i(k) = 0`. Positives `Fx` verkleinert das X-Ziel, negatives `Fx` vergrößert es. Entsprechend für Y. Die Achsen sind entkoppelt: `Fx` bewegt nur X, `Fy` nur Y.

Begrenzung um die Pose `p_cmd,i` vom Phasenstart (`commanded_target_*`):

```text
p*_i(k) = clamp( p*_i(k−1) + u_i(k),  p_cmd,i − P_max,  p_cmd,i + P_max )
P_max = xy_max_correction
```

Der äußere Abschluss ist eine zweite Bedingung. Er nutzt `F_on`, nicht `F_off`, und verlangt zusätzlich Stillstand:

```text
Erfolg, wenn |Fx| ≤ F_on und |Fy| ≤ F_on und MotionState = AtTarget
```

Solange die Bahn noch läuft, kann die Hysterese im Band `F_off < |F| < F_on` an der nächsten Entscheidung weiter einen Schritt ausgeben. Steht die Bahn und liegt die veröffentlichte Kraft schon bei oder unter `F_on`, endet die Phase, auch wenn die innere Hysterese noch aktiv gewesen wäre. `setPreClampXyEnabled(false)` löscht die Hysterese und den Periodenzähler.

Entscheidung am Periodenende. Die Hysterese liest die veröffentlichte Kraft, danach folgt der Schritt nur für Achsen, deren Hysterese an ist:

```1144:1152:core/Machine.cpp
            const float fx = host_inputs_.force.fx;
            const float absFx = std::fabs(fx);
            if (!x_force_correction_active_ && absFx >= force_params_.fx_threshold_on_N)
            {
                x_force_correction_active_ = true;
            }
            if (x_force_correction_active_ && absFx <= force_params_.fx_threshold_off_N)
            {
                x_force_correction_active_ = false;
            }
```

```1166:1175:core/Machine.cpp
            if (x_force_correction_active_)
            {
                ++xy_correction_steps_;
                if (fx > 0.0f)
                {
                    stored_target_x_ -= force_params_.dx_step_units;
                }
                else if (fx < 0.0f)
                {
                    stored_target_x_ += force_params_.dx_step_units;
                }
```

Laden der Schwellen aus der Maschinenkonfiguration:

```1506:1516:core/Machine.cpp
void Machine::loadPreClampForceParams()
{
    force_params_.fx_threshold_on_N = machine_config_.force.xy_tolerance_N;
    force_params_.fx_threshold_off_N = machine_config_.force.xy_tolerance_N * 0.5f;
    force_params_.fy_threshold_on_N = machine_config_.force.xy_tolerance_N;
    force_params_.fy_threshold_off_N = machine_config_.force.xy_tolerance_N * 0.5f;
    force_params_.dx_step_units = machine_config_.force.xy_correction_step;
    force_params_.dy_step_units = machine_config_.force.xy_correction_step;
    force_params_.max_x_correction_units = machine_config_.force.xy_max_correction;
    force_params_.max_y_correction_units = machine_config_.force.xy_max_correction;
}
```

`ForceControlPhase::SlowZApproach` heißt nur noch so. Die Phase wird ausschließlich durch `pre_clamp_xy_enabled_` gesetzt. Der Abstand zum Z-Ziel (`slow_z_distance_threshold_units`) wird im Tick nicht gelesen. Ohne den Schalter bleibt die Korrektur aus, auch nahe am Z-Ziel, und der Periodenzähler wird genullt.

```1121:1129:core/Machine.cpp
    const bool apply_xy = pre_clamp_xy_enabled_;
    force_phase_ = apply_xy ? ForceControlPhase::SlowZApproach : ForceControlPhase::NormalZApproach;

    if (!apply_xy)
    {
        x_force_correction_active_ = false;
        y_force_correction_active_ = false;
        xy_correction_accum_s_ = 0.0f;
    }
```

Die Vorzeichenregel ist in `tests/forcecontrol_tests.cpp` festgehalten: `Fx > 0` bewegt X in negative Richtung, `Fy > 0` bewegt Y in negative Richtung, die andere Achse bleibt liegen, solange ihre Kraft in der Aus-Schwelle ist. Die Begrenzung ist achsweise und unabhängig (`max_x_correction_units` und `max_y_correction_units`).

Mechanische Annahme, die der Code **nicht** prüft: Eine positive Sensorkraft soll bedeuten, dass der Prüfkörper den TCP in +Achsrichtung belastet, und ein Schritt in −Achsrichtung soll diese Kraft abbauen. Wenn die Dose, die Einbaurichtung oder die Vorzeichenverdrahtung anders sind, läuft die Korrektur in die Last hinein, bis Zeit, Versuchszahl oder die Positionsbegrenzung abbrechen. Eine Stabilitätsaussage braucht deshalb eine gemessene laterale Kennlinie `F_i(p_i)` mit demselben Vorzeichen wie `HostInputs`.

## 4. Innerer Lageregler

Der Kraftschritt ändert das Positionsziel **einmal pro 50 ms**, nicht einmal pro `tick`. Der STM32 ruft `tick(0.001)` auf; der Core summiert diese Millisekunden. Sobald 50 ms voll sind, wird die Hysterese einmal ausgewertet und höchstens ein Schritt je aktiver Achse ausgegeben. `xy_max_attempts` zählt diese Schritte. Die Entscheidrate ist damit auf beiden Hosts 20 Hz. 80 Versuche sind 4,0 s, wenn jede Entscheidung einen Schritt zählt; das Zeitlimit von 2,0 s kommt vorher.

Die Hysterese wird nicht in den Millisekunden dazwischen nachgeführt. Ein Dip unter `F_off` zwischen zwei Entscheidungen lässt den Zähler weiterlaufen. Die Achse folgt dem zuletzt gesetzten Ziel mit dem Trapezprofil von `Axis`. Bei Beschleunigung 0 (Default `a_testpoints = 0`) ist das konstante Geschwindigkeit:

```text
Δp = v_fine · dt          wenn der Restweg größer ist
p  = p*                   wenn v_fine · dt den Restweg erreicht oder übersteigt
```

```187:203:core/Axis.cpp
    const float remaining = target - position;
    const float direction = (remaining > 0.0f) ? 1.0f : -1.0f;
    const float step = max_velocity * dt;
    if (step >= std::fabs(remaining))
    {
        position = target;
        current_velocity = 0.0f;
    }
    else
    {
        position += direction * step;
        current_velocity = direction * max_velocity;
    }
```

In `PreClampXyCorrect` ist `v_fine` = `motion.v_fine_testpoints` (Default 2 mm/s). Z bleibt das Ziel vom Phasenstart. Bei Default-Beschleunigung 0 und Eckenradius 0 gibt es keinen Corner-Blend.

Der `MotionPlanner` fährt Achsen nacheinander: zuerst X, dann Y, wenn beide Ziele von der aktuellen Pose abweichen. Ein geändertes Ziel baut die Beine neu und startet wieder bei X. `startLeg` hält die nicht aktive Achse auf der aktuellen Position.

```349:361:core/MotionPlanner.cpp
    if (std::fabs(tx - cx) > kPositionTolerance)
    {
        legs_[leg_count_].axis = LegAxis::X;
        legs_[leg_count_].machine_target = tx;
        ++leg_count_;
    }

    if (std::fabs(ty - cy) > kPositionTolerance)
    {
        legs_[leg_count_].axis = LegAxis::Y;
        legs_[leg_count_].machine_target = ty;
        ++leg_count_;
    }
```

Folge, wenn **beide** Kanäle an derselben Entscheidung einen neuen Schritt bekommen: `moveTo` baut den Pfad neu und fängt bei X an. Y ist das folgende Bein. Bei 0,05 mm und 2 mm/s ist X nach 25 ms fertig, Y kann in der restlichen Periode fahren. Ein kleineres `v_fine` oder ein größeres `Δ` lässt Y unfertig, weil die nächste Entscheidung den Pfad wieder bei X aufsetzt. Die Reihenfolge bleibt X vor Y.

`kPositionTolerance` ist `1e-5` mm (`core/Types.hpp`). Ein Schritt von 0,05 mm liegt weit darüber, ein unverändertes begrenztes Ziel nicht.

## 5. Takt und Ein-Schritt-Verzug

Die Bohrungssuche entscheidet in jedem Prozedur-Tick. Der 50-ms-Takt weiter unten gehört nur zur Hysterese aus Abschnitt 3.1, und die startet die Phase nicht.

Im automatischen Prozess ist die Reihenfolge pro Tick:

1. Kinematik, inklusive XY-Schritt, mit der **bisherigen** Kraftprobe
2. Host-Eingänge neu laden (neue Kraft)
3. Kraft-Watchdog
4. Phasenlogik (`handlePreClamp`) auf der neuen Probe

```189:217:core/ProcedureController.cpp
    // Pose(t) then Force(t) then watchdog, then phase logic on the same sample.
    machine_.advanceKinematics(dt);
    machine_.invokeHostInputUpdateCallback();
    machine_.evaluateHostSafety(dt);
    ...
    case ProcedurePhase::PreClampXyCorrect:
        handlePreClamp(dt);
```

Die Kinematik am Anfang des Prozedur-Ticks macht genau einen Tiefpass-Schritt auf den Rohwert, der zu Tick-Beginn schon gespeichert ist, und entscheidet die XY-Korrektur auf diesem `F`. Der Simulations-Callback danach schreibt einen neuen Rohwert, rechnet den Tiefpass aber nicht noch einmal. Phasenlogik und Korrektur sehen deshalb dasselbe `F`. Der Watchdog sieht den neuen Rohwert abzüglich Tara, nicht `F`. Auf dem STM32 liegt `setHostInputs` vor `tick`, der Tick-Beginn hat also schon die aktuelle Millisekunde.

Ein Entscheid benutzt das `F` dieses Ticks. Die nächste Host-Probe wirkt erst beim nächsten Tiefpass-Schritt. Fällt die veröffentlichte Kraft erst nach dem Entscheid unter `F_on`, kann die Phase enden, sobald die Bahn des schon ausgegebenen Schritts steht.

Die Schrittweite und der Versuchszähler hängen an der 50-ms-Entscheidung, nicht am Maschinen-Tick. Simulation (ein Tick = eine Entscheidung) und STM32 (50 Ticks = eine Entscheidung) haben dieselbe Korrekturrate.

| Laufzeit | Maschinen-`dt` | Entscheidung | Weg der Achse bei 2 mm/s in 50 ms | 80 Zähler-Inkremente, eine Achse |
|---|---|---|---|---|
| Simulationsbrücke | 0,05 s | jede Periode ein Tick | 0,10 mm, der 0,05-mm-Schritt ist im selben Tick erreicht | 4,0 s; das Zeitlimit 2,0 s kommt vorher |
| STM32 `tickOneMillisecond` | 0,001 s | alle 50 Ticks | 0,10 mm, über die 50 Ticks verteilt (0,002 mm je Tick) | ebenfalls 4,0 s; das Zeitlimit kommt vorher |

Der Zähler steigt je aktiver Achse und Entscheidung, auch wenn die Pose schon an der Begrenzung liegt. Beide Achsen aktiv: zwei Inkremente je 50 ms, also 80 Inkremente in 2,0 s. Das trifft das Default-Zeitlimit, wenn beide Kanäle jede Entscheidung einen Schritt ausgeben.

`xy_max_attempts` steht nur im Core-Default (80) und wird von `validateMachineConfig` geprüft (`1 … 10000`). Es liegt nicht im M12-`MachineConfig`-Payload und nicht im GUI-Katalog. Über die Parametermaske ist es nicht änderbar.

## 6. Parameter

Defaults aus `MachineConfig` und dem GUI-Katalog. Einheiten der Pose sind Millimeter.

| Parameter | Default | Wirkung | Sichtbar in der GUI |
|---|---|---|---|
| `v_bore_search` | 1 mm/s | Wand-zu-Wand-Fahrt und Anfahrt auf die Sehnenmittelpunkte. Nicht die feine Z-Anfahrt. | ja, Reiter Regelung (`vBoreSearch`) |
| `xy_tolerance_N` | 0,5 N | Rising-Edge-Schwelle der gesuchten Kraft. Am Mittelpunkt `|Fx|` und `|Fy|`. | ja, Reiter Regelung |
| `xy_search_limit` | 5 mm | Weg einer Wandfahrt ab dem programmierten Punkt. Keine Wand → Abbruch. Höchstens 50 mm. | ja (`xySearchLimit`) |
| `bore_refine_passes` | 0 | Zusätzliche Zwei-Sehnen-Durchläufe vom gefundenen Mittelpunkt. Höchstens 5. | ja (`boreRefinePasses`) |
| `preclamp_confirm` | an | Anhalten, wenn der Sehnenmittelpunkt noch belastet ist. Aus: nur Bericht, dann Clamp. | ja (`preclampConfirm`, 0/1) |
| `xy_correct_timeout_s` | 30 s | Abbruchzeit der ganzen Suche inkl. Wiederholungen, aufsummiertes `dt`, nicht während Pause. Höchstens 600 s. | ja (`preClampTimeout`) |
| `xy_correction_step` | 0,05 mm | nur die ungenutzte Hysterese; bleibt im Wire | nein |
| `xy_max_correction` | 2 mm | nur die ungenutzte Hysterese; bleibt im Wire | nein |
| `xy_max_attempts` | 80 | von der Suche nicht gelesen | nein |
| `v_fine_testpoints` | 2 mm/s | nur die feine Z-Anfahrt vor der Suche | ja, Verfahren |
| `a_testpoints` | 0 | 0 = konstante Geschwindigkeit | ja |
| `corner_testpoints` | 0 | 0 = voller Stopp zwischen X und Y | ja |
| `collision.contact_N` | 30 N | Watchdog-Grenze in dieser Phase, für `|Fx|`, `|Fy|` und `|Fz|`. Höchstens der Messbereich. | ja |
| `sample_timeout_s` | 0,5 s | stehende Kraftprobe → Fault | ja |
| `force_filter` | 0,05 s | Zeitkonstante `tau`. 0 = Durchleitung. Watchdog bleibt ungefiltert. | ja, Gruppe Force |

Harte Grenzen, nicht aus der GUI schreibbar (`core/SafetyLimits.hpp`):

| Grenze | Wert |
|---|---|
| `|Fx|`, `|Fy|`, `|Fz|` | `force_hard_limit_N`, Default 30 N |
| Geschwindigkeit | 50 mm/s |
| Beschleunigung | 500 mm/s² |
| `xy_max_correction`, `xy_search_limit` | 50 mm |
| PreClamp-Zeit | 0,05 … 600 s |
| `bore_refine_passes` | 0 … 5 |
| `xy_max_attempts` | 1 … 10000 |

`validateMachineConfig` lehnt nicht positive Toleranzen, nicht positive Schrittweite und Werte über diesen Grenzen ab. Es wird nicht still auf die Grenze geklemmt.

Der separate M10-Parameter `fz_safety_threshold_N` (Default `1e6` N in `ForceControlParams`) wird von `loadPreClampForceParams` nicht gesetzt. In der Prozedur ist die Fz-Prüfung inside `tickActiveMotion` damit praktisch aus. Fz, Fx und Fy begrenzt der Watchdog, der vor der Kinematik in `Machine::tick` und nach der Kinematik im Prozedur-Tick läuft.

In `PreClampXyCorrect` ist der Kontext `Contact`. Default `contact_N` ist 30 N je Achse und liegt auf der harten Grenze dieser Achse. Eine Querkraft unter der X- oder Y-Grenze bricht die Phase nicht als Kollision ab; sie wird korrigiert oder läuft in Zeit- bzw. Versuchsabbruch. Schwellen über dem Messbereich lehnt die Konfiguration ab. Die Notbremse dafür ist 500 N.

```28:44:core/ForceWatchdog.hpp
    if (std::fabs(force.fx) > config.force.hard_limit_x_N ||
        std::fabs(force.fy) > config.force.hard_limit_y_N ||
        std::fabs(force.fz) > config.force.hard_limit_N)
    {
        return ForceWatchdogTrip::HardLimit;
    }

    if (!phase_enabled)
    {
        return ForceWatchdogTrip::None;
    }

    const AxisForceLimit limit = config.collisionLimit(context);
    if (std::fabs(force.fx) > limit.x || std::fabs(force.fy) > limit.y || std::fabs(force.fz) > limit.z)
    {
        return ForceWatchdogTrip::CollisionContext;
    }
```

Nicht endlich (`NaN`/`Inf`) oder eine als ungültig markierte Probe (`forceSampleValid == false`) führt zum Fault, bevor Beträge verglichen werden. Eine fehlende Wandlung wird nicht als 0 N gelesen.

## 7. Grenzen für eine wissenschaftliche Bewertung

Was der Regler ist:

- Entkoppelte, getaktete Hysterese mit fester Schrittweite.
- Positionsbegrenzung um die angefahrene Pose.
- Abschluss über eine Totzone in Newton plus Stillstand.
- Abbruch über Zeit oder Zähler, danach STOP.

Was er nicht ist:

- Kein PID, kein Zustandsregler, kein Beobachter.
- Keine Kraft-Sollwertregelung auf 0 N mit stetiger Stellgröße. Der „Sollwert“ ist die Totzone `±F_on`, die Stellgröße ist ein fester Millimeter-Schritt je 50 ms.
- Keine Abhängigkeit von `Fz`, von der Einfahrtiefe oder von der lateralen Steifigkeit.
- Kein Zustandsbeobachter. Der Tiefpass ist nur die Aufbereitung von `u`, siehe Abschnitt 2.1.
- Keine gleichzeitige Achsbewegung im Planner: X wird vor Y ausgeführt. Bei Default 0,05 mm und 2 mm/s dauert ein Schritt 25 ms, die andere Achse kann in der restlichen Periode noch fahren. Ein kleineres `v_fine` lässt Y unfertig, wenn die nächste Entscheidung den Pfad wieder bei X aufbaut.
- Keine Regelung auf dem Einfahrhub. Nach `Clamp` sind `Fx` und `Fy` nur noch Watchdog-Größen.

Weitere Grenzen, die eine Arbeit ausweisen sollte:

1. **Entscheidrate.** `Δ` und `xy_max_attempts` skalieren mit der 50-ms-Entscheidung, nicht mit dem Maschinen-Tick. 20 Hz (Simulation) und 1 kHz (STM32) sind für den Zähler dasselbe Experiment. Anzugeben sind `tau`, `T = 0,05 s`, `v_fine` und `Δ`.
2. **Ziel und Achse.** Die Zielrate ist `Δ / 0,05 s`. Bei den Defaults sind das 1 mm/s, die Achse fährt mit 2 mm/s. Der Schritt ist innerhalb der Periode erreicht. Ein größeres `Δ` oder ein kleineres `v_fine` lässt das Ziel der Achse davonlaufen; der Zähler zählt den Entscheid trotzdem.
3. **Y nach X.** Beide Entscheidungen fallen zusammen, der Planner fährt X zuerst. Siehe die Grenze oben.
4. **Vorzeichen ist eine Host-Annahme.** Der Core spiegelt `Fx`/`Fy` nicht (`u = −sign(F)`). Am STM32 tauscht `FirmwareHost` die ADC-Kanäle X↔Y (mechanisch gekreuzte Dose) und negiert Fx/Fy einmal, damit Reaktionskraft der Dose als Aktionskraft (+F entlang +Maschine) ankommt. Negation **oder** Core-Unload umdrehen — nicht beides. Simulation injiziert bereits Core-Frame-Kräfte.
5. **Eine Periode Verzug** zur Host-Probe, die erst im Callback nach der Kinematik ankommt, und ein möglicher letzter Schritt, nachdem `F` schon wieder in der Totzone liegt.
6. **Hysterese und Abschluss nutzen verschiedene Schwellen** (`F_off` gegen `F_on`). Im offenen Band kann die nächste Entscheidung weiter korrigieren, bis die Bahn steht.
7. **Simulation schließt die Schleife lateral nicht.** Tests prüfen die Abbildung Kraft → Zielschritt an injizierten Konstanten und an einem synthetischen Wechsel 1,2 N / 0 N (`tests/force_filter_tests.cpp`), nicht eine gemessene Kontaktkennlinie.
8. **Grenzwert `≥`.** `y(t) = u (1 − exp(−t/tau))` bleibt für endliches `t` streng unter einem konstanten `u`. Eine Schwelle `F ≥ u` wird bei exakt diesem `u` erst numerisch erreicht, wenn der Rest in der float32-Rundung verschwindet. Kräfte, die durch die Schwelle laufen, sind davon nicht betroffen.
9. **Soft-Limits.** Eine blockierte Richtung hält die wirksame Achse auf der aktuellen Position (`applyEffectiveTargets`), der interne Zähler und das gespeicherte Ziel laufen weiter, bis Zeit oder Versuche abbrechen.
10. **Pause ist in dieser Phase erlaubt** (`pauseAllowed` schließt nur Clamp, Insert, Optimize, Test/Aging und Unclamp aus). Ein Pause-Resume setzt die Bewegung mit dem letzten gespeicherten Ziel fort; die Hysterese wird durch `moveTo` beim Phasenstart gelöscht, nicht durch Pause.
11. **100-ms-Fenster.** Es senkt weißes und netzfrequentes Rauschen auf den Zählwerten. Es entfernt keine Drift und ist keine Genauigkeit der Kennlinie. Die Residuen der Kalibrierung (0,18 N Gerade, 0,6 N Parabel) bleiben die Grenze der systematischen Zuordnung. Die Gruppenlaufzeit ist 50 ms. An allen vier Wänden sind `v_bore_search` und die Schwelle gleich, deshalb fällt diese Laufzeit im Sehnenmittelpunkt heraus.

## 8. Abgrenzung zur Z-Kraft beim Einfahren

Damit die Querregelung nicht mit dem Einfahrhub verwechselt wird:

**Phase 1** fährt mit konstanter Geschwindigkeit `phase1_v_insert` in −Z, bis `Fz ≥ target_force_N`. Das ist eine Schwellwertsuche, kein Regler. Wird das Suchende, `AtTarget` oder Z-min erreicht, ohne die Schwelle zu treffen, ist das ein Abbruch. X und Y bleiben `point_x_` / `point_y_`.

**Phase 2** misst `Fz` einmal am Ende des Einfahrhubs (Bahn steht) und korrigiert die Einfahrtiefe zwischen den Versuchen:

```text
e  = Fz_gemessen − target_force_N
ΔZ = clamp( K · e,  −z_correction_max,  +z_correction_max )
```

`K` = `z_correction_gain` (Default 0,1 mm/N). Liegt `|e|` in `force_tolerance_N` (Default 0,5 N), ist die Phase fertig. `|ΔZ|` unter `minimum_correction` (Default 0,01 mm) wird ignoriert. Zu viele Vorzeichenwechsel, zu viele Versuche oder eine Tiefe unter `target_z − insert_search_distance` brechen ab. Das ist ein diskreter Proportional-Schritt pro Zyklus, ausdrücklich kein PID (`gui/server/config/parameterCatalog.js`, Feld `z_correction_gain`).

**Phase 3** wiederholt Aus- und Einfahren zwischen den beiden Z-Lagen. Im Testmodus wird `(z, Fz)` aufgezeichnet. Es gibt keine Kraftkorrektur.

## 9. Was bereits geprüft ist

| Prüfung | Aussage |
|---|---|
| `tests/forcecontrol_tests.cpp` | Vorzeichen, Hysterese-Band, Achsenentkopplung, Positionsbegrenzung, Fz-Safety-Fault friert die Pose ein |
| `tests/preclamp_tests.cpp` | Hysterese nur, wenn sie am `Machine` eingeschaltet ist. Die Phase schaltet sie nicht ein. Timeout bricht ab. In `InsertInitial` kein XY-Nachführen |
| `tests/bore_search_tests.cpp` | Versetzte runde Bohrung endet an der zweiten Sehnenmitte, dann Clamp. Restkraft am Mittelpunkt mit Bestätigung pausiert, ohne Bestätigung geht es weiter. Eine Refine-Wiederholung bleibt am Zentrum. Keine Wand im Suchweg bricht ab. Rauschen auf der fallenden Flanke speichert keinen Punkt. Timeout bis 600 s zulässig |
| `docs/M18_Procedure.md` S10 | Bohrungssuche nur in `PreClampXyCorrect` |

Nicht geprüft und für eine Arbeit offen: laterale Steifigkeit des Prüfkörpers, Sensormontage und Vorzeichen an der Dose, Wiederholbarkeit der korrigierten Pose an der echten Dose. Der synthetische Wechsel um `F_on` ist in `tests/force_filter_tests.cpp` geprüft, eine gemessene Rauschdichte der Dose nicht.
