# XY-Kraftkorrektur vor dem Einspannen

Stand: Implementierung im Core, Meilensteine M10 (Regler) und M17/M18 (Einbindung in die Prüfprozedur). Dieses Dokument beschreibt nur das, was der Code tut. Es ist die Grundlage, um die Querkräfte vor dem Spreizen wissenschaftlich einzuordnen. Es ist keine Identifikation einer mechanischen Steifigkeit und kein Nachweis, dass die Korrektur an der realen Dose stabil ist.

Die Querkräfte werden **nicht** während des Z-Einfahrhubs geregelt. `Fx` und `Fy` wirken nur in der Prozessphase `PreClampXyCorrect`: nach der Feinabfahrt auf den programmierten Prüfpunkt, vor dem Öffnen des Spreizers und vor Phase 1. Sobald die Phase endet, ist die XY-Korrektur aus. Phase 1, Phase 2 und Phase 3 fahren X und Y nicht mehr kraftabhängig.

```text
Safe-Z → XY zum Prüfpunkt → Z grob → Z fein
  → PreClampXyCorrect (nur hier: Fx, Fy)
  → Clamp (Spreizer)
  → Phase 1 InsertInitial (nur Fz, konstante Geschwindigkeit)
  → Phase 2 OptimizeInsert (nur Fz, diskret zwischen Versuchen)
  → Phase 3 Test/Aging (keine Kraftregelung)
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

Der STM32-Plan trennt Messung und Regelung: ADC und Skalierung auf dem Host, die Korrektur bleibt im Core. `force_filter` bleibt aus, bis das Rauschen der Dose gemessen ist. Der Firmware-Stub taktet `Machine` mit 1 ms; die laufende Simulationsbrücke taktet mit 50 ms. Die Prozedur selbst hängt im Core am `ProcedureController`, nicht am Firmware-Stub.

## 2. Wann die Korrektur läuft

`PointApproach` senkt Z ohne XY-Kraftkorrektur ab. Nach dem Stillstand der groben Anfahrt läuft eine Ruheprobe (`ForceTare`). Erst danach beginnt die feine Z-Abfahrt, bereits mit dem neuen Offset. Die Phase endet, wenn die Feinabfahrt steht oder — noch während der Feinabfahrt — `Fz` die Sollkraft erreicht. Dann startet `PreClampXyCorrect` direkt, ohne eine zweite Ruheprobe. Sonst wäre die Kontaktkraft vor der XY-Korrektur wieder genullt.

Beim Eintritt wird die Kollisionsgrenze auf `Contact` gestellt, das Bewegungsprofil auf die Feingeschwindigkeit, der Versuchszähler genullt, die Kraftparameter aus `MachineConfig` geladen und die Korrektur eingeschaltet. Das Bewegungsziel ist die **aktuelle** Pose. Die nachfolgenden XY-Schritte werden gegen dieses Ziel begrenzt, nicht gegen einen zweiten, versteckten Prüfpunkt.

```403:410:core/ProcedureController.cpp
    case ProcedurePhase::PreClampXyCorrect:
        machine_.setCollisionContext(CollisionContext::Contact);
        machine_.setActiveMotionProfile(motion.v_fine_testpoints, motion.a_testpoints,
                                        motion.corner_testpoints);
        preclamp_elapsed_s_ = 0.0f;
        machine_.loadPreClampForceParams();
        machine_.setPreClampXyEnabled(true);
        machine_.moveTo(machine_.x(), machine_.y(), machine_.z());
```

Erfolg: beide Beträge liegen bei oder unter `xy_tolerance_N` und die Bahn steht. Dann wird die Korrektur ausgeschaltet und `Clamp` startet. Die korrigierte Pose wird erst in `InsertInitial` als `point_x_` / `point_y_` übernommen. Der spätere Einfahrhub benutzt also die korrigierte XY-Lage, regelt sie aber nicht mehr.

```880:885:core/ProcedureController.cpp
    if (std::fabs(fx) <= force_cfg.xy_tolerance_N && std::fabs(fy) <= force_cfg.xy_tolerance_N &&
        motionSettled())
    {
        machine_.setPreClampXyEnabled(false);
        enterPhase(ProcedurePhase::Clamp);
        return;
    }
```

```413:415:core/ProcedureController.cpp
        point_x_ = machine_.x();
        point_y_ = machine_.y();
```

Abbruch, wenn die Zeit `xy_correct_timeout_s` erreicht ist oder der Schrittzähler `xy_max_attempts` erreicht. Beides ruft `abortProcess()` auf (STOP, Homing ungültig, Prozess `Aborted`). Es gibt keinen Fallback, der die begrenzte Pose als gültigen Prüfpunkt akzeptiert.

```810:814:core/ProcedureController.cpp
    if (preclamp_elapsed_s_ >= force_cfg.xy_correct_timeout_s ||
        machine_.xyCorrectionSteps() >= force_cfg.xy_max_attempts)
    {
        abortProcess();
    }
```

`Clamp` schaltet die Korrektur noch einmal aus. Ein Test sperrt, dass sie in `InsertInitial` aus bleibt und ein hohes `Fx` die X-Lage dort nicht mehr verschiebt.

```405:407:core/ProcedureController.cpp
    case ProcedurePhase::Clamp:
        machine_.setPreClampXyEnabled(false);
```

Ohne gesetztes `pre_clamp_xy_enabled_` ignoriert `tickActiveMotion` Querkräfte. Das bestätigen `tests/preclamp_tests.cpp` und `tests/forcecontrol_tests.cpp`.

## 3. Mathematisches Modell

Das ist kein PID-Regler und keine Steifigkeitsregelung. Es gibt keine Streckenübertragungsfunktion `F = K (x − x0)` im Regler. Pro Achse ist es ein entkoppelter, getakteter Zweipunkt mit Hysterese und fester Positionsschrittweite.

Abtastindex `k`, Achse `i ∈ {x, y}`, Kraft `F_i` in Newton, Sollpose `p*_i` in Millimetern (Maschinenkoordinate).

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

Solange die Bahn noch läuft, kann die Hysterese im Band `F_off < |F| < F_on` weiter Schritte ausgeben. Steht die Bahn und liegt die Kraft schon bei oder unter `F_on`, endet die Phase, auch wenn die innere Hysterese noch aktiv gewesen wäre. `setPreClampXyEnabled(false)` löscht die Hysterese.

Implementierung, X-Kanal (Y ist dieselbe Struktur):

```1078:1114:core/Machine.cpp
    if (force_phase_ == ForceControlPhase::SlowZApproach)
    {
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

            const float min_x = commanded_target_x_ - force_params_.max_x_correction_units;
            const float max_x = commanded_target_x_ + force_params_.max_x_correction_units;
            if (stored_target_x_ < min_x)
            {
                stored_target_x_ = min_x;
            }
            if (stored_target_x_ > max_x)
            {
                stored_target_x_ = max_x;
            }
        }
    }
```

Laden der Schwellen aus der Maschinenkonfiguration:

```1425:1435:core/Machine.cpp
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

`ForceControlPhase::SlowZApproach` heißt nur noch so. Die Phase wird ausschließlich durch `pre_clamp_xy_enabled_` gesetzt. Der Abstand zum Z-Ziel (`slow_z_distance_threshold_units`) wird im Tick nicht gelesen. Ohne den Schalter bleibt die Korrektur aus, auch nahe am Z-Ziel.

```1069:1076:core/Machine.cpp
    const bool apply_xy = pre_clamp_xy_enabled_;
    force_phase_ = apply_xy ? ForceControlPhase::SlowZApproach : ForceControlPhase::NormalZApproach;

    if (force_phase_ != ForceControlPhase::SlowZApproach)
    {
        x_force_correction_active_ = false;
        y_force_correction_active_ = false;
    }
```

Die Vorzeichenregel ist in `tests/forcecontrol_tests.cpp` festgehalten: `Fx > 0` bewegt X in negative Richtung, `Fy > 0` bewegt Y in negative Richtung, die andere Achse bleibt liegen, solange ihre Kraft in der Aus-Schwelle ist. Die Begrenzung ist achsweise und unabhängig (`max_x_correction_units` und `max_y_correction_units`).

Mechanische Annahme, die der Code **nicht** prüft: Eine positive Sensorkraft soll bedeuten, dass der Prüfkörper den TCP in +Achsrichtung belastet, und ein Schritt in −Achsrichtung soll diese Kraft abbauen. Wenn die Dose, die Einbaurichtung oder die Vorzeichenverdrahtung anders sind, läuft die Korrektur in die Last hinein, bis Zeit, Versuchszahl oder die Positionsbegrenzung abbrechen. Eine Stabilitätsaussage braucht deshalb eine gemessene laterale Kennlinie `F_i(p_i)` mit demselben Vorzeichen wie `HostInputs`.

## 4. Innerer Lageregler

Der Kraftschritt ändert das Positionsziel **einmal pro 50 ms**, nicht einmal pro `tick`. Das ist die Taktperiode der Simulation. Der STM32 ruft `tick(0.001)` auf; der Core sammelt diese Millisekunden, bis 50 ms voll sind, und gibt dann denselben Schritt aus. `xy_max_attempts` zählt diese Schritte. Bei 1 kHz ohne diese Teilung wäre das Limit von 80 Versuchen in 80 ms erreicht.

Die Hysterese wird jeden Tick aktualisiert. Der Schritt auf das Positionsziel folgt erst, wenn die 50-ms-Periode voll ist. Die Achse folgt diesem Ziel mit dem Trapezprofil von `Axis`. Bei Beschleunigung 0 (Default `a_testpoints = 0`) ist das konstante Geschwindigkeit:

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

Folge, solange **beide** Kanäle jeden Tick einen neuen Schritt bekommen: Das X-Ziel ändert sich, die Bahn wird neu aufgebaut, Y wird gehalten. Y fährt erst, wenn das X-Ziel aufhört sich zu ändern (Hysterese aus oder Positionsbegrenzung erreicht, sodass `moveTo` denselben Goal sieht und ein No-Op ist). Gleichzeitige Querkräfte werden also nicht gleichzeitig ausgeregelt. Die Reihenfolge ist X vor Y.

`kPositionTolerance` ist `1e-5` mm (`core/Types.hpp`). Ein Schritt von 0,05 mm liegt weit darüber, ein unverändertes begrenztes Ziel nicht.

## 5. Takt und Ein-Schritt-Verzug

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

Die Korrektur sieht die Kraft um einen Tick verzögert. Der Abschluss sieht die neue Probe. Ein letzter Schritt auf Basis der alten Kraft ist möglich, danach kann die Phase enden, sobald die Bahn steht.

Die Schrittweite und der Versuchszähler sind **pro Tick**, nicht pro Sekunde. Dieselbe Konfiguration bedeutet bei anderem `dt` eine andere Korrekturrate und eine andere Zeit bis `xy_max_attempts`.

| Laufzeit | `dt` | Soll-Schritt Default 0,05 mm | Weg der Achse bei 2 mm/s pro Tick | 80 Zähler-Inkremente |
|---|---|---|---|---|
| Simulationsbrücke `simulation_protocol/main.cpp` | 0,05 s (20 Hz) | 0,05 mm | 0,10 mm, der 0,05-mm-Schritt ist in einem Tick erreichbar | 4,0 s, wenn nur eine Achse zählt; das Zeitlimit 2,0 s kommt vorher |
| Firmware-Stub `FirmwareHost::tickOneMillisecond` | 0,001 s | 0,05 mm | 0,002 mm, das Ziel läuft der Achse davon | 0,080 s, das Versuchslimit kommt vor dem Zeitlimit |

Der Zähler steigt **je aktivem Kanal und Tick**, auch wenn die Pose schon an der Begrenzung liegt und sich nicht mehr bewegt. Beide Kanäle aktiv: zwei Inkremente pro Tick.

```1092:1094:core/Machine.cpp
        if (x_force_correction_active_)
        {
            ++xy_correction_steps_;
```

```1131:1133:core/Machine.cpp
        if (y_force_correction_active_)
        {
            ++xy_correction_steps_;
```

Bei 1 kHz und beiden Kanälen sind 80 Inkremente 40 ms. In der Zeit fährt die Achse bei 2 mm/s etwa 0,08 mm. Eine laterale Korrektur, die mehr Weg braucht, bis `|F|` unter `F_on` fällt, bricht am Versuchszähler ab, bevor das Zeitlimit von 2 s eine Rolle spielt. An der Simulationsbrücke (20 Hz) ist es umgekehrt: das Zeitlimit von 2 s entspricht 40 Ticks und kommt vor 80 Inkrementen, sofern nicht beide Kanäle jeden Tick zählen (dann 40 Ticks × 2 = 80, Zeit und Zähler treffen sich ungefähr).

`xy_max_attempts` steht nur im Core-Default (80) und wird von `validateMachineConfig` geprüft (`1 … 10000`). Es liegt nicht im M12-`MachineConfig`-Payload und nicht im GUI-Katalog. Über die Parametermaske ist es nicht änderbar.

## 6. Parameter

Defaults aus `MachineConfig` und dem GUI-Katalog. Einheiten der Pose sind Millimeter.

| Parameter | Default | Wirkung | Sichtbar in der GUI |
|---|---|---|---|
| `xy_tolerance_N` | 0,5 N | `F_on`. Abschluss der Phase. Darüber startet die Hysterese. | ja, Gruppe Force |
| `F_off` | 0,25 N | `0,5 · xy_tolerance_N`, fest | nein |
| `xy_correction_step` | 0,05 mm | `|u|` pro aktivem Tick und Achse | ja |
| `xy_max_correction` | 2 mm | Betragsgrenze um die Pose beim Phasenstart, je Achse | ja |
| `xy_correct_timeout_s` | 2 s | Abbruchzeit, aufsummiertes `dt` | ja (`preClampTimeout`) |
| `xy_max_attempts` | 80 | Abbruch über den Schrittzähler | nein |
| `v_fine_testpoints` | 2 mm/s | Geschwindigkeit der Korrekturbewegung | ja, Motion |
| `a_testpoints` | 0 | 0 = konstante Geschwindigkeit | ja |
| `corner_testpoints` | 0 | 0 = voller Stopp zwischen X und Y | ja |
| `collision.contact_N` | 30 N | Watchdog-Grenze in dieser Phase, für `|Fx|`, `|Fy|` und `|Fz|`. Höchstens der Messbereich. | ja |
| `sample_timeout_s` | 0,5 s | stehende Kraftprobe → Fault | ja |
| `force_filter` | 0 | wird geprüft und gespeichert, im Tick nicht angewendet | ja, als zukünftig markiert |

Harte Grenzen, nicht aus der GUI schreibbar (`core/SafetyLimits.hpp`):

| Grenze | Wert |
|---|---|
| `|Fx|`, `|Fy|`, `|Fz|` | `force_hard_limit_N`, Default 30 N |
| Geschwindigkeit | 50 mm/s |
| Beschleunigung | 500 mm/s² |
| `xy_max_correction` | 50 mm |
| PreClamp-Zeit | 0,05 … 30 s |
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
- Keine Kraft-Sollwertregelung auf 0 N mit stetiger Stellgröße. Der „Sollwert“ ist die Totzone `±F_on`, die Stellgröße ist ein fester Millimeter-Schritt pro Tick.
- Keine Abhängigkeit von `Fz`, von der Einfahrtiefe oder von der lateralen Steifigkeit.
- Kein Filter. `force_filter` wirkt nicht.
- Keine Kopplung und keine gleichzeitige XY-Bewegung, solange beide Ziele jeden Tick neu gesetzt werden.
- Keine Regelung auf dem Einfahrhub. Nach `Clamp` sind `Fx` und `Fy` nur noch Watchdog-Größen.

Weitere Grenzen, die eine Arbeit ausweisen sollte:

1. **Abtastabhängigkeit.** `Δ` und `xy_max_attempts` skalieren mit dem Tick, nicht mit der Zeit. 20 Hz (Simulation) und 1 kHz (geplanter STM32-Tick) sind nicht dasselbe Experiment. Eine Validierung muss `dt`, `v_fine`, `Δ` und den Zähler gemeinsam angeben.
2. **Ziel läuft davon, wenn `Δ / dt > v_fine`.** Bei 1 kHz und den Defaults ist die Zielrate 50 mm/s, die Achse fährt 2 mm/s. Das Ziel sitzt nach wenigen zehn Millisekunden an `±xy_max_correction`, die Achse hat kaum Weg gemacht, der Zähler läuft weiter.
3. **Y bleibt zurück, solange X jeden Tick nachzieht.** Die ausgeführte Y-Korrektur ist nicht der zeitgleiche Gegenschritt zu `Fy`.
4. **Vorzeichen ist eine Verdrahtungsannahme.** Der Core spiegelt `Fx`/`Fy` nicht. Nur `Fz` ist in `docs/M18_Procedure.md` (S6) als zug-positiv für den Prozess festgelegt. Für X und Y gilt nur: positiv entlang +Maschinenachse, Korrektur entgegengesetzt.
5. **Ein Tick Verzug** und ein möglicher Extrastepp nach dem Kraftabfall.
6. **Hysterese und Abschluss nutzen verschiedene Schwellen** (`F_off` gegen `F_on`). Im offenen Band kann weiter korrigiert werden, bis die Bahn steht.
7. **Simulation schließt die Schleife lateral nicht.** Tests prüfen die Abbildung Kraft → Zielschritt an injizierten Konstanten, nicht eine gemessene Kontaktkennlinie.
8. **Soft-Limits.** Eine blockierte Richtung hält die wirksame Achse auf der aktuellen Position (`applyEffectiveTargets`), der interne Zähler und das gespeicherte Ziel laufen weiter, bis Zeit oder Versuche abbrechen.
9. **Pause ist in dieser Phase erlaubt** (`pauseAllowed` schließt nur Clamp, Insert, Optimize, Test/Aging und Unclamp aus). Ein Pause-Resume setzt die Bewegung mit dem letzten gespeicherten Ziel fort; die Hysterese wird durch `moveTo` beim Phasenstart gelöscht, nicht durch Pause.

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
| `tests/preclamp_tests.cpp` | Korrektur nur bei eingeschaltetem PreClamp; Timeout bricht ab; in `InsertInitial` kein XY-Nachführen |
| `docs/M18_Procedure.md` S10 | XY-Korrektur nur in `PreClampXyCorrect` |

Nicht geprüft und für eine Arbeit offen: laterale Steifigkeit des Prüfkörpers, Sensormontage und Vorzeichen an der Dose, Verhalten bei 1 kHz mit den Defaults, gleichzeitige `Fx`/`Fy`, Rauschen um `F_on`, Wiederholbarkeit der korrigierten Pose.
