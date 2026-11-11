# Under Investigation – A Race Director Simulator

Design-Dokument, Stand 2026-11-07. Lebendes Dokument: Entscheidungen werden hier nachgetragen, offene Fragen stehen am Ende.

---

## 1. Vision

Der Spieler sitzt in der Rennleitung einer fiktiven Formel-Rennserie. Er fährt nicht selbst, sondern sieht Timing-Screens, eine Kamerawand, eine Streckenkarte und den Funk. Er muss entscheiden: Flaggen, Safety Car, Untersuchungen, Strafen.

Das Spiel handelt von **Informationen und Entscheidungen unter Zeitdruck**. 3D-Grafik dient als Beweismittel (Kameras, Replays), nicht als Selbstzweck.

**Zielgruppe:** Motorsport-Fans, die Regelwerk und Rennleitung verstehen oder kennenlernen wollen.

**Status:** Zunächst privat, eine spätere Veröffentlichung ist möglich. Deshalb gilt von Anfang an: keine lizenzierten Namen oder Designs im Code oder in Datenformaten (siehe Kapitel 10).

---

## 2. Gameplay-Kern

### 2.1 Die Rolle

Der Spieler ist Race Director und gleichzeitig Vorsitzender der Stewards (später trennbar). Ein Rennen dauert in Echtzeit je nach Einstellung 15 bis 90 Minuten, mit optionaler Zeitraffung in ruhigen Phasen.

### 2.2 Werkzeuge des Spielers

| Werkzeug | Aktionen |
|---|---|
| Flaggen | Gelb / Doppelt Gelb pro Marshal-Sektor, Grün, Rot, Blau (automatisch mit Override), Ölflagge |
| Neutralisierung | VSC auslösen/beenden, Safety Car auslösen/hereinholen, Rennabbruch (Rot) |
| Boxengasse | Öffnen/Schließen |
| Vorfälle | Notieren → Untersuchen → Entscheiden (keine Aktion / Verwarnung / 5s / 10s / Drive-Through / Stop-and-Go / Strafpunkte) |
| Werkzeuge zur Prüfung | Kamerawand, Replay mit Zeitleiste, Telemetrie-Ausschnitt, Funk-Mitschnitte, Track-Limits-Zähler |

### 2.3 Spielschleife

```
Rennen läuft  ──►  Ereignis tritt ein (Crash, Kontakt, Track Limits, Wetter …)
     ▲                    │
     │                    ▼
     │            Spieler bemerkt es (Alarm, Funk, Kamera, Timing-Anomalie)
     │                    │
     │                    ▼
     │            Sofortmaßnahme? (Gelb, VSC, SC, Rot)
     │                    │
     │                    ▼
     │            Untersuchung: Replay, Telemetrie, Team-Stellungnahmen
     │                    │
     │                    ▼
     └──────────  Entscheidung + Konsequenzen (Rennverlauf, Reaktionen, Bewertung)
```

### 2.4 Spannung entsteht aus Zielkonflikten

- **Sicherheit vs. Show:** Zu viel SC macht das Rennen langweilig und verärgert Teams, zu wenig führt zu Folgeunfällen.
- **Tempo vs. Gründlichkeit:** Eine Untersuchung braucht Zeit, aber eine Strafe nach Rennende ist unbeliebter als eine während des Rennens.
- **Konsistenz:** Ähnliche Vorfälle sollten ähnlich bestraft werden. Das Spiel merkt sich frühere Urteile (Präzedenzfälle).
- **Druck:** Teams beschweren sich per Funk, fordern Untersuchungen und protestieren.

---

## 3. Regelwerk: FIA Formula One Sporting Regulations

Grundlage ist das **reale Reglement der FIA für die Formel 1**: die *Formula One Sporting Regulations*, dazu Anhang L des *International Sporting Code* (Flaggen, Fahrverhalten) und die *Race Directors Event Notes* des jeweiligen Rennens. Standard ist die **Saison 2026**.

Umsetzung:
- Die Regeln werden als **Spielmechanik** nachgebaut und liegen als Daten vor (`data/rules/fia-f1-2026.json`), nicht fest im Code. Parameter wie VSC-Delta, Anzahl der Blauflaggen-Signale oder Track-Limits-Grenzen sind konfigurierbar. Ein neues Saison-Reglement braucht dann nur eine neue Datei.
- Jede Regel im JSON verweist auf ihren Artikel (z. B. `"source": "F1 SR 2026 Section B, B5.13"`), damit sie sich gegen das Original prüfen lässt. Das Reglement wird während der Saison mehrfach geändert, deshalb ist pro Eintrag vermerkt, ob er geprüft ist (`verified`) und wo der Wert eine Sim-Vereinfachung ist (`note`).
- **Prüfstand (2026-11-08):** Section B, Issue 09 vom 2026-10-01. Relevante Artikel: B1.6.3 (Boxengasse, 80 km/h), B1.9.6 (Strafen absitzen; +20 s / +30 s, wenn Drive-Through bzw. Stop-and-Go nicht mehr absitzbar), B5.12 VSC, B5.13 Safety Car, B5.14 Unterbrechung, B5.15 Wiederaufnahme, B7.2 Overtake-Modus. Für M7 (Paket 7.1) kamen dazu: B1.6.2 (Unsafe Release), B1.8.6 (Track Limits), B1.9.5/B1.9.6 b (Strafen beim Boxenstopp), B5.14.4 a vii (Reifenwechsel unter Rot), B6.3.6 (zwei Trockenmischungen, sonst Disqualifikation). Für M8 (Paket 8.1) kamen dazu: B5.13.4 c (Überrundete dürfen unter SC vorbeifahren), B5.13.5 b (danach kommt das SC am Ende der folgenden Runde herein), B1.6.4 b (Einfahrt in die geschlossene Boxengasse im Rennen: Stop-and-Go). Flaggen: ISC Anhang H 2026 (veröffentlicht 2026-06-23), Art. 2.5.5 b (Gelb, doppelt Gelb) und 2.5.5 e (Blau: Platz machen „bei der ersten Gelegenheit“, keine Sektorzahl; laut Competition Notes São Paulo 2026 blaue Lichter ab 1,2 s Abstand). Strafmaße für Speeding in der Boxengasse, Unsafe Release und Track Limits (3. Verstoß schwarz-weiße Flagge, ab dem 4. je 5 s) stammen aus den Penalty Guidelines 2026 v01. Die FIA sagt, die Guidelines hätten keinen regulatorischen Wert. Deshalb stehen diese Werte in der Regeldatei mit `verified: false`.
- **Overtake-Modus (B7.2.2/B7.2.3):** gesperrt beim Start, bis der Führende die Detection Line zum ersten Mal überquert; gesperrt unter Safety Car, bis nach dem Einrücken alle Autos die Kontrolllinie überquert haben; aktiviert an der Activation Line für jeden, der an der Detection Line weniger als die Detection Gap hinter einem anderen Auto lag. Detection Gap, Detection Line und Activation Line legt die FIA pro Veranstaltung fest (Sim-Annahme: 1,0 s). Umsetzung in Paket 6.8.
- Die Stewards-Praxis (wie streng welcher Vorfall typischerweise bestraft wird) steht nicht im Reglement. Sie wird aus veröffentlichten Stewards-Entscheidungen als Richtwert-Tabelle abgeleitet und ist die Basis für die Bewertung "Fairness/Konsistenz".

### 3.1 Flaggen und erwartetes Fahrerverhalten (Sim-Regeln)

| Zustand | Verhalten der KI-Fahrer |
|---|---|
| Gelb (Sektor) | Deutlich langsamer im Sektor, kein Überholen bis hinter die Gefahrenstelle |
| Doppelt Gelb | Stark verlangsamen, kein Überholen, Bereitschaft zum Anhalten oder Richtungswechsel |
| VSC | Kein Überholen, jedes Auto muss in jedem Marshal-Abschnitt eine Mindestzeit einhalten (Delta). Abstände bleiben ungefähr erhalten. |
| Safety Car | Feld reiht sich hinter dem SC ein, kein Überholen. Überrundete dürfen auf Freigabe der Rennleitung zurückrunden, danach kommt das SC am Ende der Folgerunde herein. |
| Rot | Alle Autos fahren langsam in die Boxengasse und stellen sich in Reihenfolge auf. Reifenwechsel ist erlaubt. Neustart hinter dem SC oder als stehender Start. |
| Blau | Überrundetes Auto muss bei der ersten Gelegenheit Platz machen. Ignoriert es mehrere Blauflaggen-Signale (Wert aus dem Reglement), entsteht ein Vorfall "Ignoring blue flags". |

**Fehlverhalten der KI ist gewollt:** Mit kleiner Wahrscheinlichkeit verstoßen Fahrer gegen Regeln (Überholen unter Gelb, zu schnell unter VSC). Das erzeugt Vorfälle für den Spieler.

### 3.2 Vorfall-Typen (MVP-Auswahl fett)

- **Kollision / Kontakt** (Schuldfrage: wer, wie viel)
- **Track Limits** (Zähler pro Fahrer; Warnungen, dann Strafe)
- **Überholen unter Gelb / VSC-Delta verletzt**
- Unsafe Release (Boxengasse)
- Blocking / Abdrängen
- Speeding in der Boxengasse
- Ignorieren blauer Flaggen
- Vorteil durch Verlassen der Strecke

### 3.3 Strafenkatalog

Gemäß Sporting Regulations: keine weitere Aktion, Verwarnung (Reprimand), 5s- oder 10s-Zeitstrafe, Drive-Through, 10s Stop-and-Go, Zeitstrafe nach Rennende, Startplatzstrafe (nächstes Rennen, ab Karriere-Modus), Strafpunkte auf der Superlizenz, Disqualifikation.

---

## 4. Architektur

### 4.1 Grundprinzip: Simulation und Darstellung strikt trennen

```
┌─────────────────────────────────────────────────────────┐
│  UE 5.8 – Darstellungsschicht (dünn, austauschbar)      │
│  Actors für Autos/SC · Kameras · SceneCapture · UMG     │
│  liest Snapshots, interpoliert, sendet Spieler-Befehle  │
└──────────────▲───────────────────────────┬──────────────┘
               │ SimSnapshot (read-only)   │ Commands
┌──────────────┴───────────────────────────▼──────────────┐
│  UISim-Kern – reines C++20, KEINE Unreal-Abhängigkeit   │
│  Track · Cars · RaceControl-State · Incidents · Rules   │
│  fester Tick (20 Hz), deterministisch (Seed)            │
│  Recorder: Snapshots + Commands → Replay                │
└─────────────────────────────────────────────────────────┘
```

**Warum:**
1. **UE6-Portierbarkeit.** Epic will Actors, Blueprints und das C++-Gameplay-Framework langfristig durch Verse und Scene Graph ersetzen (UE6 Early Access frühestens Ende 2027). Der Kern überlebt das unverändert, neu geschrieben wird nur die Darstellungsschicht.
2. **Replays sind trivial.** Aufgezeichnete Snapshots lassen sich aus jeder Kamera abspielen.
3. **Testbarkeit.** Der Kern läuft als Konsolen-App und in Unit-Tests ohne Editor.
4. **Zeitraffer.** Die Sim kann schneller als Echtzeit laufen.

### 4.2 Regeln für den Kern

- Nur Standard-C++20 und die Standardbibliothek. Keine `UObject`, `FString`, `TArray`, kein `UE_LOG`.
- **Deterministisch:** gleicher Seed + gleiche Commands = gleiches Rennen. Eigener RNG (z. B. PCG32), keine Abhängigkeit von Wall-Clock oder Frame-Zeit.
- Schnittstelle: `Sim::Step(dt, commands) → SimSnapshot`. Der Snapshot ist ein flaches, kopierbares Struct.
- Daten (Strecken, Fahrer, Teams, Regeln) als JSON-Dateien. Parser im Kern (z. B. nlohmann/json als einzige Fremdbibliothek).
- Build mit CMake. Tests mit Catch2 oder doctest.
- Einbindung in Unreal als **Third-Party-Library in einem Plugin** (`Plugins/UISimBridge`). Das Plugin übersetzt zwischen Kern-Typen und UE-Typen und ist die einzige Stelle, die beides kennt.

### 4.3 Regeln für die Unreal-Schicht

- So wenig Logik wie möglich. Actors stellen dar, entscheiden nichts.
- Blueprints nur für Visuelles (Kamerafahrten, Effekte, Materialien).
- UMG zeichnet nur. Daten wie Timing, Strafenliste und Vorfälle kommen fertig aus dem Snapshot. Die UI-Logik (Sortierung, Filter) liegt in einer eigenen, UE-freien Schicht im Kern oder in der Bridge.
- Spieler-Eingaben werden zu `Command`-Objekten (z. B. `DeploySafetyCar`, `SetSectorFlag{sector, DoubleYellow}`, `IssuePenalty{incidentId, Penalty::TenSeconds}`).

### 4.4 Ordnerstruktur (geplant)

```
/core            UISim-Kern (CMake-Projekt)
  /include/uisim
  /src
  /tests
  /tools         Konsolen-Runner, Track-Importer
/data            Strecken, Fahrer, Teams, Reglement (JSON)
/unreal          UE-5.8-Projekt "UnderInvestigation"
  /Plugins/UISimBridge
/docs            Design, Entscheidungen (ADRs)
```

---

## 5. Simulationsmodell

Bewusst vereinfacht. Glaubwürdigkeit ist wichtiger als Physik.

### 5.1 Strecke

- Mittellinie als geschlossener Spline (Catmull-Rom oder kubisch), parametrisiert über die Distanz `s` in Metern.
- Pro Abschnitt: Breite, Kurventyp (schnell/mittel/langsam), Referenzgeschwindigkeit, Überholwahrscheinlichkeit.
- 3 Timing-Sektoren, 15 bis 25 Marshal-Sektoren mit Posten-Position (für Flaggen und Kameras).
- Boxengasse als eigener Pfad mit Ein- und Ausfahrt, Speed-Limit.
- Quelle für den Verlauf: eigene Fantasiestrecke oder Koordinaten realer Strecken (FastF1/OpenF1) für den privaten Gebrauch (siehe Kapitel 10).

### 5.2 Autos (kinematisch)

Zustand pro Auto: `s` (Distanz), `lateralOffset` (−1…+1 der Streckenbreite), `speed`, Reifen (Mischung, Alter, Abnutzung), Sprit, Schaden, Status (racing, pitting, retired, stopped).

Zielgeschwindigkeit pro Abschnitt:

```
v_target = v_ref(abschnitt)
         × Fahrer-Skill
         × Auto-Performance
         × Reifenfaktor(Abnutzung, Mischung, Temperatur später)
         × Spritfaktor(Masse)
         × Flaggenfaktor(Gelb/VSC/SC)
         + Rauschen (Fahrerform, kleine Fehler)
```

Ein einfaches Folgemodell (Abstand zum Vordermann, Windschatten-Bonus, Dirty-Air-Malus) erzeugt Positionskämpfe. Überholen ist ein Zustand mit seitlichem Versatz und Erfolgswahrscheinlichkeit.

**Keine Chaos-Vehicle-Physik.** Nur im Moment eines Unfalls übernimmt in UE eine Physik-Simulation oder eine vorgefertigte Animation. Der Kern kennt nur das Ergebnis (Auto steht bei `s`, Schaden X, blockiert Spur Y).

### 5.3 Vorfälle

Pro Tick und Fahrerpaar wird eine Vorfall-Wahrscheinlichkeit berechnet:

```
p = basis(kurventyp)
  × nähe(abstand, überholversuch)
  × reifenrisiko × wetterrisiko × fahrer-aggression
  × flaggenzustand (Fehlverhalten unter Gelb)
```

Jeder Vorfall erzeugt ein `Incident`-Objekt mit:
- beteiligten Autos, Zeit, Ort (`s`, Marshal-Sektor),
- **Ground Truth** (wer war wirklich schuld, zu wie viel Prozent; dem Spieler verborgen),
- **Beweisen**: Telemetrie-Fenster (Geschwindigkeit, Bremse, Lenkwinkel ±5 s), Replay-Zeitstempel, Kamera-IDs mit Sicht auf den Ort, Funk-Aussagen der Fahrer (gefärbt durch Persönlichkeit).

Die Ground Truth ist die Basis der Bewertung, aber nie eindeutig sichtbar. Der Spieler muss aus den Beweisen schließen.

### 5.4 Tick und Zeit

- Sim-Tick fest 20 Hz (`dt = 0,05 s`). Rendering interpoliert zwischen zwei Snapshots.
- Zeitraffer = mehrere Sim-Ticks pro Frame.
- Recorder speichert jeden Snapshot (20 Autos × ca. 64 Byte × 20 Hz ≈ 25 KB/s, ca. 90 MB pro Stunde unkomprimiert). Für später: nur Keyframes plus Deltas speichern.

---

## 6. Darstellung (UE 5.8)

### 6.1 Kameras

- **Streckenkameras** an Marshal-Posten (fest, mit Schwenk auf das nächste Auto)
- **Onboard** (vorne, hinten) pro Auto
- **Heli** (folgt einem Ziel)
- **Frei** (nur im Replay)
- **Kamerawand:** 4 bis 6 `SceneCapture2D` auf Render Targets mit reduzierter Auflösung und Framerate. Die Hauptansicht rendert in voller Qualität.
- Ein Vorfall schlägt automatisch die Kameras mit der besten Sicht auf den Ort vor.

### 6.2 UI-Bereiche (Rennleitungs-Arbeitsplatz)

1. **Timing-Tower:** Position, Fahrer, Intervall, Abstand zum Führenden, letzte Runde, Reifen, Boxenstopps, Strafen
2. **Streckenkarte (2D):** Autos als Punkte, Marshal-Sektoren mit Flaggenfarbe, Vorfall-Marker
3. **Kamerawand**
4. **Vorfall-Liste:** neu / notiert / in Untersuchung / entschieden
5. **Funk-Log:** Teams, Fahrer, Marshals (Text, später optional TTS)
6. **Steuerpult:** Flaggen, VSC/SC/Rot, Boxengasse
7. **Untersuchungs-Ansicht:** Replay mit Zeitleiste, Kamerawahl, Telemetrie-Plot, Strafen-Dialog

Die UI ist der größte Arbeitsblock des Projekts.

### 6.3 Assets

- Autos: generisches Formel-Auto-Modell. Für privat reichen Fab-/Sketchfab-Modelle, für eine Veröffentlichung braucht es ein eigenes, generisches Design. **Lackierungen und Teamnamen sind Daten**, damit ein Austausch ohne Codeänderung möglich ist.
- Strecke: Spline-Mesh aus den Kern-Streckendaten (Bridge erzeugt Straße, Kerbs, Randstreifen). Umgebung minimal (Leitplanken, Tribünen als Blockouts).

---

## 7. Bewertung

Nach dem Rennen (und als Live-Indikatoren) gibt es Punkte in vier Kategorien:

| Kategorie | Gemessen an |
|---|---|
| **Sicherheit** | Reaktionszeit auf Unfälle, Folgeunfälle in ungesicherten Zonen, Marshals in Gefahr |
| **Fairness** | Übereinstimmung der Urteile mit der Ground Truth |
| **Konsistenz** | Ähnliche Vorfälle → ähnliche Strafen (Vergleich mit eigenen Präzedenzfällen) |
| **Rennqualität** | Anteil der Runden unter Neutralisierung, Strafen nach Rennende, Spannung (Überholungen, Abstände) |

Dazu kommt Feedback in Textform: Team-Reaktionen, Presse-Schlagzeilen, Kommentar der Serienleitung.

---

## 8. MVP-Meilensteine

Jeder Meilenstein hat ein überprüfbares Abnahmekriterium.

| # | Meilenstein | Fertig, wenn … |
|---|---|---|
| **M1** | **Sim-Kern ohne Grafik** – Strecke aus JSON, 20 Autos, Rundenzeiten, Timing | Konsolen-Runner simuliert 50 Runden in < 2 s, gibt plausibles Ergebnis aus (Rundenzeiten-Streuung, Überholungen > 0). Gleicher Seed ⇒ identisches Ergebnis (Test). |
| **M2** | **UE-Anbindung** – Plugin, einfache Autos (Blockouts), eine Strecke, 3–4 Kameras, Timing-Tower in UMG | Rennen läuft im Editor flüssig sichtbar, Timing-Tower stimmt mit Konsolenausgabe überein. |
| **M3** | **Flaggen, VSC, SC, Rot** | Tests: unter SC/VSC keine Überholungen (außer gewollte Regelverstöße), Abstände unter VSC bleiben in Toleranz, Feld schließt beim SC auf. Bedienbar über Steuerpult. |
| **M4** | **Replay + Vorfall-Prüfung** | Vorfall erscheint in der Liste, Replay springt zum Zeitpunkt, mind. 2 Kameras zeigen den Ort, Telemetrie-Plot sichtbar. |
| **M5** | **Strafen + Bewertung** | Strafe wird im Rennen angewendet (5s/10s auf Rennzeit, Drive-Through erzwingt Boxendurchfahrt). Bewertungsbildschirm nach Rennende. |
| **M6** | **Unfälle & Sicherheit** (Plan in 8.1) | Siehe Abnahmekriterien der Arbeitspakete in 8.1. |
| **M7** | **Boxenstopps & Strategie** (Plan in 8.2) | Siehe Abnahmekriterien der Arbeitspakete in 8.2. |
| **M8** | **Bedienung per Maus** (Plan in 8.3) | Siehe Abnahmekriterien der Arbeitspakete in 8.3. |
| **M9** | **Wetter und echte Strecken** (Plan in 8.4) | Siehe Abnahmekriterien der Arbeitspakete in 8.4. |
| **M10** | **Saison und Karriere** (Plan in 8.5) | Siehe Abnahmekriterien der Arbeitspakete in 8.5. |
| **M11** | **Steward-Anhörungen** (Plan in 8.6) | Siehe Abnahmekriterien der Arbeitspakete in 8.6. |
| M12+ | Ausbau | Funk mit Persönlichkeiten, Training und Qualifying |

**Stand 2026-11-11:** M11.5 erledigt (Anhörung und Right of Review im Kern. *Anhörung:* Befehl `HoldHearing` für einen Vorfall. Im Rennen dauert sie 180 s Rennzeit; so lange nimmt der Kern kein Urteil zu diesem Vorfall an. Nach dem Ziel kostet sie keine Zeit. Der Vorfall merkt sich, dass angehört wurde. *Right of Review:* Ein Urteil ohne Anhörung, das deutlich neben der Ground Truth liegt (Wertung unter 0,5: falscher Fahrer oder zwei Stufen daneben), wird kassiert und durch das richtige ersetzt; die nicht angesehenen Beweise sind das neue Element. Nach einer Anhörung bleibt dasselbe Urteil bestehen. *Bewertung:* Jedes kassierte Urteil kostet 5 Punkte der Gesamtwertung zusätzlich zur Fairness (`RaceRating::overturned`). *Karriere:* Die Strafpunkte folgen dem Urteil, wie es nach der Überprüfung steht, treffen also den wirklich Schuldigen; die Zahl der kassierten Urteile steht pro Rennen im Spielstand. Abweichung vom Plan: Zurückgenommen werden nur die Strafpunkte. Zeitstrafen und Durchfahrten bleiben im Rennergebnis, weil sie sich nach dem Rennen nicht sauber herausrechnen lassen. Der Runner nennt die kassierten Urteile je Rennen. 115 Kern-Tests, davon 3 neue; der Ersatzspieler der Karriere-Tests urteilt jetzt nach dem Ziel und nach Anhörung. Einzelrennen unverändert, Runner und Unreal identisch. Unreal kennt den Befehl noch nicht (11.7)). M11.4 erledigt (Aussagen und Beweise im Kern, `Hearing.h`/`Hearing.cpp`. *Beweise:* `makeHearing` liefert zu einem Vorfall, was die Aufzeichnung zeigt; es stimmt immer. Bei einer Kollision sind es die drei Größen, aus denen die Schuld entsteht: Abstand beim Einlenken (0 m = daneben), wie viel später der Angreifer gebremst hat als sonst (bis 30 m) und wie weit der Verteidiger in der Bremszone herübergezogen ist (bis 2,5 m); weiter hinten und später gebremst belastet den Angreifer, Herüberziehen entlastet ihn. Bei Boxengassen-Tempo, Unsafe Release, Track Limits, Frühstart und Startposition ist es der Messwert, bei Flaggen-, Reifen- und Boxenverstößen die Feststellung des Verstoßes. Unfälle ohne Berührung haben keine Anhörung. *Aussagen:* Jede Seite sagt zu denselben Größen etwas, bei Kollisionen beide Autos, sonst das eine. Wer nach der Ground Truth zu bestrafen ist, lügt mit der Wahrscheinlichkeit 1 minus Ehrlichkeit und verschiebt seine Angaben dann zu seinen Gunsten, um das 1,5- bis 3-Fache der Toleranz; alle anderen bleiben innerhalb der halben Toleranz. Toleranzen: 1 m Abstand, 5 m Bremspunkt, 0,5 m Herüberziehen, 15 % beim Messwert. Jede Lüge widerspricht damit mindestens einem Beweis und ist an ihm prüfbar (`contradictsEvidence`). *Ehrlichkeit:* neue Eigenschaft der Fahrer (`honesty`, 0 bis 1, Standard 0,7); das fiktive Feld hat Werte von 0,40 bis 0,95. *Keine freien Texte:* Aussagen sind Größen mit Werten, die Anzeige macht daraus Sätze. Die Anhörung ist für ein Rennen immer gleich und ändert es nicht; Kollisionen merken sich dafür die drei Größen. Gemessen bei Ehrlichkeit 0,6 über 30 Rennen: 34 von 79 Schuldigen widersprechen den Beweisen (43 %). 112 Kern-Tests, davon 5 neue in `test_hearing.cpp`; Einzelrennen unverändert, Runner und Unreal identisch). M11.3 erledigt (Vollständiger Start im Kern. *Einführungsrunde:* Vor dem ersten Start fahren alle eine Runde in Startreihenfolge mit gleichem Tempo (60 % des Referenztempos), ohne Überholen, ohne Boxenstopps, ohne Fahrfehler und Track Limits; es gelten die Regeln des Safety Car, ohne dass eines fährt. Danach steht jedes Auto wieder in seiner Box (auf einen halben Meter genau), die Runde zählt nicht, und Distanzen und Zeitmessung beginnen neu; erst dann läuft die Ampel. Die Zeit der Einführungsrunde zählt nicht als Neutralisation. Die Rennuhr läuft aber ab dem Losfahren, der Start kommt auf dem Testring bei etwa 2:37. Abschaltbar über `RaceConfig::formationLap`. *Liegengebliebenes Auto:* Mit 1 % pro Auto und Start bleibt ein Auto in der Aufstellung stehen (Snapshot `stalled`); die Ampel wartet dann 12 s auf die Rennleitung. *Befehle:* `AbortStart` (B5.8: Wartezeit, hier 60 s, dann neue Einführungsrunde) und `ExtraFormationLap` (B5.9: sofort eine weitere Runde); beide kosten eine Rennrunde, das liegengebliebene Auto startet hinter der letzten Reihe und etwas später (Ersatz für den Start aus der Boxengasse). Nach dem Startsignal wirken beide nicht mehr. *Ohne Eingriff:* Das Auto steht beim Start im Weg, wird nach 15 s weggeschoben und fährt dann los; jedes Auto, das bis 45 m dahinter startet, trifft es mit 5 %, dann fallen beide aus. Im Test über 80 Starts wird ein Teil der stehenden Autos getroffen, aber weniger als die Hälfte. *Nebenbei behoben:* Ein Auto auf dem Weg in die Startaufstellung fährt nicht mehr in die Box; das hätte den stehenden Start nach SC oder Rot blockieren können. *SC-Start:* weiter vor dem Losfahren möglich, er ersetzt die Einführungsrunde. Neue Vergleichsbasis Seed 1: 37 Überholungen, 7 Vorfälle; Runner und Unreal identisch, auch die Beispiel-Saison nach zwei Rennen. Angepasste Tests: Die Tests zu Ampel und Regen-Rennleitung fahren ohne Einführungsrunde, weil sie an Zeiten ab der Ampel hängen; der Saison-Test erlaubt einem Fahrer bis 60 % der Siege (Seed 3 hat jetzt 13 von 24); eine Schwelle im Bewertungstest war rechnerisch nicht mehr erreichbar. 107 Kern-Tests, davon 5 neue. *Offen für 11.7 und 11.8:* Unreal zeigt während der Einführungsrunde den Zustand Safety Car und kennt die beiden neuen Befehle und das liegengebliebene Auto noch nicht; die Prüfskripte in `unreal/TestScripts` hängen wieder an alten Rennzeiten). M11.2 erledigt (Ausgeglichenere Saison. In Saisonrennen treten die Fahrer mit einer Tagesform an (`seasonRaceEntries`): Auto je Rennen bis ±1,2 %, Fahrer je Rennen bis ±0,8 %, dazu für die ganze Saison eine Vorliebe des Teams für schnelle oder langsame Strecken (bis ±0,5 %, nach dem Anteil schneller Abschnitte) und eine des Fahrers für Regen (bis ±0,4 %). Die Werte hängen an Saison-Seed, Rennnummer und Namen, nicht an der Reihenfolge im Feld; ein gesperrter Fahrer ändert deshalb nichts an den anderen. Einzelrennen nutzen die Tagesform nicht und sind bitgleich. Ergebnis auf der privaten Saison mit den Seeds 1 bis 6: 5 bis 8 verschiedene Sieger (vorher 4), höchstens 12 Siege für einen Fahrer (vorher 13), 19 bis 20 Fahrer mit Punkten (vorher 14); die Rangfolge der Stärksten bleibt erkennbar. Abweichung vom Plan: Die Grenze von 6 Siegern wird nicht in jeder Saison erreicht (Seed 2 und 5: fünf), der Test verlangt mindestens 5 je Saison und 6 im Mittel. Die Saisonergebnisse sind neu, Runner und Unreal weiter identisch (Beispiel-Saison nach zwei Rennen). 102 Kern-Tests, davon 2 neue; die Testsuite braucht jetzt gut zwei Minuten, weil ein Test drei private Saisons fährt). M11.1 erledigt (Reglement geprüft, dazu der International Sporting Code 2026 in der Fassung ab 26. Juni 2026. *Untersuchung:* Die Stewards entscheiden, ob sie untersuchen, und nennen die Beteiligten in einer Meldung; kommt sie bis 60 Minuten nach Rennende, dürfen die Fahrer die Strecke nicht verlassen (B1.9.2). *Keine Berufung:* Gegen Zeitstrafen, Drive-Through und Stop-and-Go aus dem Rennen gibt es keine Berufung, auch nicht bei Strafen in den letzten drei Runden oder nach dem Ziel (B1.10). *Right of Review:* Eine Entscheidung kann überprüft werden, wenn ein wesentliches neues Element auftaucht, das bei der Entscheidung nicht vorlag; ob es das ist, entscheiden allein die Stewards, sie können auch von sich aus neu prüfen; Frist 96 Stunden nach Ende des Wettbewerbs (ISC 14.1.1, 14.1.2, 14.3, 14.4.1). *Abweichung vom Plan:* Aus dem geplanten "Einspruch" wird deshalb das Right of Review, und Paket 11.5 ist angepasst: Als neues Element gelten Beweise, die der Spieler ohne Anhörung nicht gesehen hat. *Fahrer:* höchstens vier Fahrer je Team in den Rennen einer Saison, neue Fahrer punkten (B1, Changes of Driver); das erlaubt den Ersatzfahrer in 11.6. *Start:* Einführungsrunde in Startreihenfolge, Überholen nur zum Wiederherstellen der Reihenfolge; ein liegengebliebenes Auto wird mit gelben Flaggen angezeigt, in die Boxengasse geschoben und darf von dort nach dem Start losfahren (B5.6, B5.7). Fünf neue Werte in der Regeldatei. 100 Kern-Tests, Runner und Unreal unverändert und identisch). M10 erledigt (10.8: Die ganze private Saison lief unsichtbar in Unreal durch, in zwei Läufen mit je zwölf Rennen (`unreal/TestScripts/season_twelve_races.txt`, je etwa 2 Minuten bei Zeitraffer 500); der zweite Lauf setzt aus dem Spielstand fort. 24 Rennen gestartet und gebucht, keine Fehler. Endstand von Fahrer- und Team-WM und die Saisonwertung (57,9 ohne Eingriffe) sind identisch zu `uisim_runner --season` mit Seed 1. Der Saison-Bildschirm nach dem letzten Rennen nennt beide Weltmeister und bietet "NEUE SAISON" an. *Auffälligkeiten und offene Punkte nach M10:* Die Saison ist einseitig: 22 von 24 Siegen gehen an zwei Fahrer, nur vier Fahrer gewinnen überhaupt, und sechs Fahrer bleiben ohne Punkt; es fehlt Streuung zwischen den Rennen (Tagesform von Fahrer und Team je Strecke). Ohne Spieler entstehen keine Strafpunkte, Sperren gibt es nur mit Urteilen. Auf vier langsamen Strecken gibt es im Mittel weniger als 15 Überholungen. Nicht umgesetzt sind Einführungsrunde, Startabbruch und abgewürgtes Auto, der Verfall der Strafpunkte und ein Ersatzfahrer. Die kleineren Punktetabellen greifen nie, weil kein Rennen vorzeitig endet. In Unreal zeigt der Saison-Bildschirm nur den Gesamtwert der Bewertung, der Schalter "STEHEND" gibt keine Rückmeldung, und die Gischt ist ein undurchsichtiger Block. Vier Prüfskripte aus M8 und M9 sind seit dem neuen Start nicht neu gefahren (`sc_at_600`, `track_map_clicks`, `pit_closed`, `rain_start`)). M10.7 erledigt (Start und Gischt in Unreal. *Ampel:* Während des stehenden Starts zeigt das Bild oben in der Mitte fünf Lichter, die nacheinander rot werden und zum Start erlöschen; der Snapshot der Brücke hat dafür `bGridStart` und `StartLights`. Die Aufstellung ist mit der Boxengassen-Kamera (KAM 6) gut zu sehen. *Stehender Start:* Schalter "STEHEND" im Steuerpult; ist er an, schicken "SC REIN" nach den Einführungsrunden hinter dem SC und "NEUSTART" nach Rot das Feld in die Startaufstellung. Skript: `sc_in standing`, `restart [wet] [standing]`. Außerhalb dieser beiden Fälle wirkt "SC REIN" wie bisher. *Gischt:* Hinter jedem Auto liegt auf nasser Ideallinie ein heller Keil, der mit Nässe und Tempo wächst; im Blockout-Stil als Block, nicht durchscheinend. In der Boxengasse, an Unfallstellen und im Review gibt es keine. *Log für Prüfungen:* jeder Vorfall mit Nummer, Zeit, Typ und Autos; Beginn und Ende eines stehenden Starts. *Prüfung:* `start_lights.txt` (Bilder: Aufstellung mit drei roten Lichtern, Gischt bei nassem Start), `standing_start.txt` (nach `sc_in standing` Aufstellung bei 5:06 und Start bei 5:22, nach Rot und `restart standing` Aufstellung bei 42:31 und Start bei 42:48). *Alte Prüfskripte:* an den neuen Rennverlauf angepasst und neu gefahren: `incident_review_clicks`/`_commands` (Vorfall #3, Klicks 16 Pixel tiefer, 17 Vorfälle; gleiche Befehlsfolge, gleiches Urteil, gleiches Ergebnis), `pit_lane` (Unsafe Release ist jetzt Vorfall #8 bei 44:38), `dsq_unlap` (unverändert gültig: UNLAP zu den Bildzeiten, Disqualifikation greift), `control_panel_clicks`/`_commands` (gleiches Ergebnis). Nicht neu gefahren, weil sie nur an Zeiten hängen: `sc_at_600`, `track_map_clicks`, `pit_closed`, `rain_start`. Einzelrennen unverändert, Runner und Unreal identisch. *Offen:* Der Strafen-Dialog nennt B5.11 für Frühstart und Startposition nicht eigens; der Schalter "STEHEND" zeigt nicht an, ob er gerade wirken würde). M10.6 erledigt (Saison in Unreal. *Brücke:* `FUISimCareer` im Plugin hält Kalender, Karriere und Spielstand für die ganze Laufzeit; `FUISimSession` übernimmt ein von der Karriere gebautes Rennen. *Ablauf:* Saisonmodus mit `-UISimSeason=<Datei in data/seasons>` und als Standard, wenn kein Einzelrennen verlangt ist (keiner der Schalter `-UISimSeed`, `-UISimTrack`, `-UISimLaps`); ohne Angabe wird der private Kalender genommen, wenn er da ist, sonst das Beispiel. Spielstand in `Saved/Career/<Name>.json` (`-UISimCareer`, Standard `career`), `-UISimNewCareer` beginnt neu. Das Spiel startet auf dem Saison-Bildschirm; "RENNEN STARTEN" baut das nächste Rennen ohne die gesperrten Fahrer. Nach dem Ziel hat die Rennbewertung den Knopf "ZUR SAISON": Das Rennen wird gebucht und gesichert, und ein neuer Race Director zeigt wieder den Saison-Bildschirm; der alte verschwindet mit Autos, Kameras und Anzeigen. Nach dem letzten Rennen heißt der Knopf "NEUE SAISON" (gleicher Kalender, nächster Seed). *Saison-Bildschirm* (`UUISeasonWidget`): Kopf mit Saison, gefahrenen Rennen, Saisonwertung, nächstem Rennen und gesperrten Fahrern; Kalender mit Runden, Regen, Wertung und Sieger je Rennen; Fahrer-WM mit Punkten, Siegen, Strafpunkten und GESPERRT; Team-WM. *Befehle und Skripte:* `season_start`, `season_back`, `season_new`. Jeder Race Director lädt einen Teil des Skripts, Teile trennt eine Zeile `---`; Zeilen mit negativer Zeit laufen auf dem Saison-Bildschirm. Der Log nennt den Stand als `SEASON Drivers/Teams` im Format des Runners. *Prüfung:* `season_two_races.txt` fährt zwei Rennen der Beispiel-Saison; der Stand danach ist identisch zu `uisim_runner --season ... --seed 4 --races 2`, Bilder vor dem ersten und nach jedem Rennen. `season_clicks.txt` bedient beide Knöpfe per Klick mit demselben Ergebnis. Nach einem Neustart setzt das Spiel den Spielstand fort. Der private Kalender mit 24 Rennen passt auf den Bildschirm. Einzelrennen unverändert, Runner und Unreal identisch. *Offen:* Der Saison-Bildschirm zeigt die Bewertung nur als Gesamtwert pro Rennen, keine Einzelwerte; die Schrift ist klein). M10.5 erledigt (Karriere im Kern, `Career.h`/`Career.cpp`. *Ablauf:* `newCareer`, dann pro Rennen `makeCareerRace` und nach dem Ziel `finishCareerRace`; das bucht WM-Punkte, die Bewertung des Rennens, Strafpunkte und Sperren. *Saisonwertung:* Mittel der Rennbewertungen (0 bis 100), pro Rennen bleiben Gesamtwert, Fairness, Konsistenz, Rennqualität, Sicherheit und die Zahl der Vorfälle erhalten. *Strafpunkte:* Zu jeder Strafe des Spielers oberhalb der Verwarnung gehören Strafpunkte nach Art des Vorfalls: Kollision 2, gelbe Flagge 3, Überholen unter VSC, SC oder vor der Linie 2, unter Rot 4 (neuer Wert `penalty_points_red_flag` aus den Guidelines), alles andere 0. Sie treffen das Auto, das der Spieler bestraft, also auch den Falschen. *Sperre:* Ab 12 Punkten fehlt der Fahrer im nächsten Rennen, danach sind 12 Punkte gestrichen (A3.3.1 b). Es gibt keinen Ersatzfahrer, das Feld ist dann kleiner; die Ergebnisse werden trotzdem unter den Nummern der Meldeliste gebucht. *Spielstand:* `saveCareer`/`loadCareer` als JSON zwischen den Rennen. Laden prüft Saison und Meldeliste und lehnt unpassende Dateien ab. Weil jedes Rennen seinen Seed aus Saison-Seed und Rennnummer bekommt, braucht der Spielstand keinen Zufallszustand. *Runner:* Die Saison läuft als Karriere: `--career <Datei>` setzt einen Spielstand fort und sichert nach jedem Rennen, `--races N` fährt nur N Rennen, `--verdicts expected` lässt einen Ersatzspieler jedes Urteil nach der Ground Truth fällen. *Gemessen:* Private Saison mit diesem Ersatzspieler: eine Sperre in 24 Rennen. Seine Bewertung liegt meist bei 70, weil er Unfallstellen nicht absichert. Zwei Rennen fahren, sichern und fortsetzen ergibt denselben Endstand wie ein Durchlauf. *Nicht umgesetzt:* Verfall der Strafpunkte nach 12 Monaten, Ersatzfahrer. 100 Kern-Tests, davon 5 neue in `test_career.cpp`; Einzelrennen unverändert, Runner und Unreal identisch). M10.4 erledigt (Saison im Kern, `Season.h`/`Season.cpp`. *Kalender:* Daten in `data/seasons` (`loadSeason`): Rennen mit Name, Strecke, optional Wetter, `short_distance` und `laps`. Ohne `laps` ergibt sich die Rundenzahl aus der Renndistanz (kleinste Rundenzahl über 305 bzw. 260 km). Öffentlich `example.json` mit vier Rennen auf dem Testring (55 Runden, eines mit 47); privat `data/seasons/private/season-2026.json` (gitignored) mit 24 echten Strecken in der Reihenfolge des Kalenders 2026, Monaco mit kurzer Distanz, vier Rennen mit Regen. *Punkte:* `classifyRace` wertet ein beendetes Rennen: gewertet wird, wer nicht disqualifiziert ist und 90 % der Runden des Siegers hat; Punkte nach der Tabelle für den Anteil der Distanz (`racePointsTable`). Die Sim fährt immer die volle Distanz und zählt die Runden unter Grün nicht, deshalb greift in der Praxis immer die volle Tabelle; die kleineren Tabellen sind als Funktion vorhanden und getestet. *WM-Stand:* `Championship` für Fahrer und Teams (beide Autos zählen), Reihenfolge nach Punkten, dann Siegen, zweiten Plätzen und so weiter; bei völligem Gleichstand bleibt die Reihenfolge der Meldeliste. *Seed:* Jedes Rennen bekommt einen eigenen Seed aus Saison-Seed und Rennnummer. *Runner:* `--season <Datei> [--seed S]` fährt die Saison und gibt nach jedem Rennen Podium und Stand aus. Die private Saison läuft durch (24 Rennen, Rundenzahlen 44 bis 78). 95 Kern-Tests, davon 6 neue in `test_season.cpp`; Einzelrennen unverändert, Runner und Unreal identisch). M10.3 erledigt (Stehender Start im Kern. *Ampel:* Das Rennen beginnt mit der Ampelphase: fünf rote Lichter im Abstand von 1 s, danach 0,5 bis 3 s bis zum Erlöschen (aus dem Seed, eigener Zufallsstrom). Die Rennzeit läuft ab dem ersten Licht, das Startsignal kommt also bei 5,5 bis 8 s. Snapshot: `gridStart` und `startLights`. *Start:* Reaktionszeit 0,2 bis 0,5 s, mit 6 % ein schlechter Start (0,4 bis 1,2 s mehr). Im Mittel wechselt jedes Auto in Runde 1 etwa 1,4 Plätze; Berührungen entstehen aus dem dichten Feld von selbst (28 Kollisionen in Runde 1 über 40 Starts), ein eigener Mechanismus für Startunfälle war nicht nötig. *Neue Vorfälle:* Frühstart (1 % pro Auto und Start, 0,05 bis 0,4 s zu früh; richtig sind 5 s, über 0,2 s zu früh 10 s) und Startposition (0,5 %, Auto steht 0,5 bis 1,5 m außerhalb der Box; 5 s). Beide entstehen genau einmal, beim Startsignal. *Nach SC-Start und Rot:* `RecallSafetyCar` mit `standing` nach den Einführungsrunden hinter dem SC (B5.10.8) und `Restart` mit `standing` nach Rot (B5.15.4): Wenn das SC hereinkommt, fahren die Autos in der Reihenfolge der Schlange in die Startaufstellung, 8 m Abstand; steht das letzte Auto, läuft die Ampel. Bis zum Signal gilt weiter Safety Car, Rot bricht den Start ab. Autos, die dabei in der Boxengasse waren, stellen sich hinten an. Ohne `standing` bleibt es beim fliegenden Start. Während der Ampel sind VSC und SC nicht möglich. *Nicht umgesetzt:* Einführungsrunde vor dem normalen Start, Startabbruch, abgewürgtes Auto. *Unreal:* kennt die beiden Vorfallstypen (Fruehstart, Startposition); Ampel, Aufstellung im Bild und der Knopf kommen mit 10.7. Die Prüfskripte in `unreal/TestScripts` hängen an Rennzeiten des alten Rennverlaufs und müssen mit 10.7 neu geprüft werden. Neue Vergleichsbasis Seed 1: 46 Überholungen, 17 Vorfälle; Runner und Unreal identisch. Angepasste Tests: Der Test für Folgeunfälle läuft über 30 statt 15 Szenarien. 89 Kern-Tests, davon 9 neue in `test_standing_start.cpp`). M10.2 erledigt (BLUE und OVT gibt es im Snapshot nicht mehr für ausgefallene Autos und Autos im Ziel; ein Test prüft das in jedem Sim-Schritt über vier Rennen. Überholungen: Ursache der vielen Überholungen war der Overtake-Modus, dessen Zusatzleistung auf jedem schnellen Abschnitt der ganzen Runde wirkte; auf Strecken mit viel Vollgas gelang damit fast jeder Angriff. Jetzt reicht die Zusatzenergie pro Runde für 12 s auf schnellen Abschnitten, neu geladen an der Activation Line. Ergebnis mit Seed 1 auf den 40 privaten Strecken: 6 bis 56 Überholungen statt 6 bis 127 (Monza 56 statt 127, Albert Park 46 statt 102, Spa 52 statt 76); der Test für die privaten Strecken verlangt höchstens 70. Testring im Mittel 38 statt etwa 50, Seed 1 jetzt 49 (neue Vergleichsbasis, Runner und Unreal identisch). Abweichung vom Plan: Die Untergrenze von 15 wird nicht überall erreicht. Im Mittel über 6 Seeds liegen Nürburgring (9), Monaco (11), Algarve (12) und Bahrain (13) darunter; die Zusatzenergie hat auch dort Überholungen gekostet. Zwei geprüfte Alternativen brachten wenig und sind nicht drin: eine Sperre für den Gegenangriff nach dem Überholtwerden und der Verlust des Windschattens beim Ausscheren. Weniger Angriffe heißt auch weniger Kollisionen: auf dem Testring mit Seed 1 12 Vorfälle statt 14, mit den Seeds 2 bis 4 sind es 7 bis 9. Angepasste Tests: Die Straf-Tests fahren Seed 3, weil Seed 1 in 20 Runden keinen Vorfall mehr hat; der Regen-Test läuft über 12 Seeds und lässt das SC draußen, bis das stehende Wasser weg ist. 80 Kern-Tests). M10.1 erledigt (Reglement geprüft, jetzt auch Section A Issue 03 für Punkte und Strafpunkte. Punkte 25-18-15-12-10-8-6-4-2-1 für Fahrer und Teams, kein Punkt für die schnellste Runde; bei verkürztem Rennen gelten kleinere Tabellen ab 2 Runden, 25 % und 50 % der Distanz, und ohne zwei Runden unter Grün gibt es keine Punkte (A2.2.1). Gleichstand in der WM entscheidet die Zahl der Siege, dann der zweiten Plätze und so weiter. Gewertet wird, wer 90 % der Runden des Siegers hat (B2.5.5 b). Renndistanz: kleinste Rundenzahl über 305 km, für einen Stadtkurs 260 km (B2.5.2); der Kalender markiert dieses Rennen. Stehender Start mit fünf roten Lichtern im Abstand von 1 s (B5.7.1). Frühstart und falsche Position nach B5.11: Die Stewards wählen zwischen 5 s, 10 s, Drive-Through und Stop-and-Go; die Guidelines nennen 5 s oder 10 s ohne Strafpunkte. In der Sim sind 5 s richtig, 10 s bei gewonnenen Plätzen. Startabbruch und zusätzliche Einführungsrunde kosten je eine Rennrunde (B5.8, B5.9); der Wert ist hinterlegt, beides ist für 10.3 nicht vorgesehen. Nach Rot wählt der Rennleiter stehenden oder fliegenden Neustart (B5.15.4). Strafpunkte: bei 12 Punkten Sperre für das nächste Rennen, danach werden 12 Punkte gestrichen; Punkte verfallen nach 12 Monaten, in der Sim bleiben sie die Saison über (A3.3.1 b). Punkte pro Verstoß aus den Guidelines: Kollision 2, gelbe Flagge 3, Überholen unter SC/VSC 2, alles andere 0. Nicht modelliert: Zeitlimit von 2 Stunden. 79 Kern-Tests, Runner unverändert). M9 erledigt (9.8: DSQ und UNLAP im Bild bestätigt mit `unreal/TestScripts/dsq_unlap.txt` und dem Prüfschalter `-UISimCompoundMiss=1`: SC in Runde 45, Freigabe, UNLAP bei vier Überrundeten (OKA, SAN, LAU, WHI), nach dem Ziel Disqualifikation für einen Verstoß gegen die Mischungspflicht, DSQ ganz unten im Zeitenturm. Der Log meldet jetzt, welche Überrundeten gerade vorbeifahren. Trockene Regression identisch. Kleinigkeiten offen: ausgefallene Autos zeigen BLUE, Autos im Ziel noch OVT; zu viele Überholungen auf Strecken mit langen Vollgaspassagen; stehender Start und Gischt fehlen). M9.7 erledigt (Unreal: `-UISimWeather=<Datei in data/weather>`, neu `wet-start.json`. Wetter-Panel oben in der Mitte mit Regen, Nässe auf und neben der Linie, stehendem Wasser, Vorhersage für +5/+10/+15 Minuten, RAIN HAZARD (über 40 % in der nächsten Stunde, B1.5.11) und der Regenreifen-Pflicht. Bei Regen liegen Schlieren und eine dunkle Tönung über dem Bild, die Fahrbahn wird mit der Nässe dunkler. Steuerpult: SC-START (nur vor dem Start) und REGEN-PFLICHT (gilt für SC-Start und Neustart); Skript `sc_start [wet]`, `restart [wet]`. Zeitenturm zeigt I und W. Echte Strecken über `-UISimTrack=private/<Name>.json`. Prüfung mit `unreal/TestScripts/rain_start.txt` auf Spa: SC-Start mit Pflicht, alle auf W, 51 statt 50 Runden, Panel und Karte im Bild, nach sc_in "SC IN THIS LAP". Trockene Regression identisch zum Runner. Nicht umgesetzt: Gischt hinter den Autos, stehender Start). M9.6 erledigt (Alle 40 Strecken aus f1-circuits (MIT) als private Daten mit erkennbar abgewandelten Namen, z. B. "Autodromo Nazionale Monzza", "Circuit de Spa-Francorchomps", "Nuerburgrinck", "Hockenheimrink". Rohdaten, Namensliste und erzeugte Strecken liegen in `data/tracks/private/` (gitignored). Der öffentliche Konverter `core/tools/trackgen/convert_circuits.py` glättet die Linie, skaliert auf die offizielle Länge, zerlegt nach Krümmung, berechnet das Kurventempo mit Abtrieb (Grip wächst mit dem Tempo), korrigiert den Schluss der Runde über die Geradenlängen und begrenzt die Überholchance langer Geraden. Rundenzeiten mit Seed 1: Monaco 1:07, Hungaroring 1:24, Monza 1:28, Spa 1:49, Silverstone 1:44; also nah an echten Werten, auf schnellen Kursen eher zu langsam. Überholungen meist 20–50, aber auf Strecken mit sehr langen Vollgaspassagen zu viele (Monza 127, Albert Park 102, Jeddah 79, Baku 78, Spa 76, Watkins Glen 75, Suzuka 68). Valle Alta entfernt; öffentliche Tests laufen auf dem Testring, ein zusätzlicher Test fährt ein Rennen auf jeder privaten Strecke, wenn vorhanden. Runner und Unreal auf Monza identisch. Lizenz in `docs/ASSETS.md`). M9.5 erledigt (Gibt der Spieler die Überrundeten unter SC nicht frei, tut es Race Control selbst, sobald das SC zum zweiten Mal über die Linie fährt, aber nur, wenn wirklich Überrundete in der Schlange stecken; danach kommt das SC wie in 8.2 eine Runde später herein. Abschaltbar über `RaceConfig::autoReleaseLappedCars`, die Tests aus 8.2 laufen ohne. 79 Kern-Tests, trocken bitgleich). M9.4 erledigt (Neuer Befehl `SafetyCarStart` vor dem Start, wahlweise mit Regenreifen-Pflicht (B5.10.1 a); `Restart` nach Rot ebenfalls mit Pflicht (B5.15.2 a). Bis das SC hereinkommt, bleiben alle auf Regenreifen; wer trotzdem wechselt (2 % pro Team), erzeugt den Vorfall "Regenreifen-Pflicht", richtig ist Stop-and-Go (B6.3.7). Die Runden hinter dem SC zählen mit, das Rennen bekommt dafür eine Runde mehr (gleichwertig zu B5.10.7). Gestartet wird fliegend über "SC in dieser Runde"; den stehenden Start (B5.10.8) gibt es noch nicht, weil die Autos dafür zurück in die Startaufstellung müssten. Starkregen auf nasser Strecke hinterlässt stehendes Wasser: Aquaplaning-Gefahr in schnellen Abschnitten, steigt mit dem Tempo hoch drei, auf Regenreifen geringer, unter SC fast null. Die Sicherheitsbewertung verliert 0,5 pro 10 Minuten Rennen in stehendem Wasser ohne Neutralisation. Fahrfehler bei Nässe nehmen hinter SC/VSC nur noch zu 20 % zu. In der Gischt sinkt die Überholchance um bis zu die Hälfte; im Regen gibt es trotzdem etwa 1,7-mal so viele Überholungen wie im Trockenen, weil Autos auf verschiedenen Reifen fahren. Test über 6 Seeds: ohne Neutralisation 22 Unfälle (13 Aquaplaning), mit SC 8. Ohne Wetterdaten bitgleich, 77 Kern-Tests). M9.3 erledigt (Intermediate und Wet als Streckendaten mit Grip-Verlust bei Nässe (Slick 25 %, Inter 10 %, Wet 5 %) und Überhitzen auf trockener Linie (Inter altern dreimal, Wet fünfmal so schnell). Kreuzungspunkte etwa bei 40 % (Slick/Inter) und 80 % Nässe (Inter/Wet). Teams wählen den schnellsten Reifen für die Nässe, wie sie sie einschätzen (Poker: bis zu 15 % daneben), und holen das Auto nach bis zu 90 s Zögern herein; bei Regen am Start stehen Regenreifen schon in der Startaufstellung. Wer Regenreifen gefahren ist, ist von der Mischungspflicht befreit (B6.3.6). Zeitenturm zeigt I und W. Ohne Wetterdaten bitgleich. Der Test aus 9.2 erwartet jetzt nur noch mehr als 8 % langsamere Regenrennen, weil die Autos Regenreifen fahren. Auffällig: im Regen deutlich mehr Überholungen (Seed 1 Wolkenbruch: 91), weil Autos auf verschiedenen Reifen fahren. 72 Kern-Tests). M9.2 erledigt (Wetter im Kern: Regenverlauf als Daten in `data/weather` (Schauer, Wolkenbruch), mit Zufall aus dem Seed auf eigenem Zufallsstrom; Runner `--weather <Datei>`. Nässe neben und auf der Ideallinie: Bei Regen nass in etwa 2 Minuten, danach trocknet die Ideallinie in etwa 15 Minuten, der Rest in etwa 30 Minuten. Auf Slicks kostet eine nasse Ideallinie bis zu 25 % Grip, Fahrfehler werden bis zu viermal so häufig, und in der Gischt eines Vordermanns steigt das Risiko von Folgeunfällen. Regen und Nässe stehen im Snapshot. Ohne Wetterdaten ist das Rennen bitgleich (Runner auf beiden Strecken identisch). 67 Kern-Tests). M9.1 erledigt (Reglement geprüft: Mischungspflicht entfällt bei Intermediate/Wet (B6.3.6); Regenreifen-Pflicht bei Start oder Neustart hinter dem SC, Wechsel kostet Stop-and-Go (B6.3.7, B5.10.1 a, B5.15.2 a); Rennen verkürzt um SC-Runden minus eins (B5.10.7, B2.5.2 a); danach stehender oder fliegender Start (B5.10.8/9); Regenlicht mit Intermediate/Wet (B1.5.5 a); Rain Hazard ab 40 % Regenwahrscheinlichkeit (B1.5.11). Eine Erklärung "Strecke nass" gibt es nur für Training und Qualifying, deshalb sind 9.4 und 9.7 angepasst). M8 erledigt (8.8: Kamera Boxengasse als KAM 6, auf der anderen Seite der Geraden mit Blick auf Fahrspur und Boxen; Reviews von Speeding, Unsafe Release und "Boxengasse gesperrt" starten auf ihr (Review-Kameramodus 5). Boxengasse und Garagen im Bild geprüft; dabei behoben: beide waren seit 7.8 100-mal zu klein skaliert (Würfel der Engine = 1 m) und deshalb unsichtbar. Zeitenturm: DSQ statt Abstand, UNLAP für Überrundete beim Vorbeifahren, im Kopf "BOXENGASSE ZU" und "UEBERRUNDETE VORBEI". Skriptzeile `screenshot <Name>`. Prüfung: `pit_lane.txt` (Boxengasse live mit haltendem Auto, Review #10 Unsafe Release auf der Boxengassen-Kamera), `pit_closed.txt` (Vorfall "Boxengasse gesperrt" in der Liste, Kopfzeile); Regression auf beiden Strecken identisch zum Runner. Nicht im Bild geprüft: DSQ (bei Seed 1 kein Verstoß gegen die Mischungspflicht) und UNLAP (im Lauf standen die Überrundeten schon am Ende der Schlange). Aufgefallen: Valle Alta kreuzt sich in der Draufsicht selbst; ein ausgefallenes Auto kann im Zeitenturm noch BLUE zeigen). M8.7 erledigt (Vorfallliste anklickbar: Klick auf eine Zeile wählt den Vorfall und startet das Review. Im Review eine Zeitleiste -5..+5 s zum Klicken oder Ziehen (neuer Befehl `SetReviewOffset`, Skript `review_at`), Knöpfe -1 S, PAUSE/PLAY, +1 S, KAMERA, ENDE. Strafen-Dialog: Beschuldigten wählen, Strafe wählen (Erklärung in eigenen Worten mit Artikel: B1.9.3, B1.9.5 a–d, f, i, B1.9.6, B6.3.6), BESTAETIGEN. Die Karte ist während des Reviews ausgeblendet, weil das Review-Panel die rechte Seite braucht. Bewertung mit Seiten (14 Vorfälle pro Seite) und Knöpfen ZURUECK/WEITER. Skripte: Zeilen nach Rennende laufen einzeln pro Bild ab 0,5 s nach dem Ziel, `screenshot` speichert ein Bild, `-UISimQuitWhenDone` wartet auf das Skript; Klickziele werden neu geloggt, wenn sie sich verschieben. Prüfung: `incident_review_clicks.txt` und `incident_review_commands.txt` ergeben dieselbe Befehlsfolge, dasselbe Urteil (5 s DRA) und dasselbe Rennergebnis; danach blättert der Klick auf WEITER auf Seite 2/2 (Bild). Klicks brauchen gezeichnete Bilder dazwischen, deshalb läuft der Klicktest mit Zeitraffer 20 bis nach den Klicks. Regression unverändert). M8.6 erledigt (Steuerpult unten in der Mitte, `UIControlPanelWidget`: Knöpfe für VSC, SC, Rot/Neustart, Überrundete, Boxengasse, Kameras 1–5 und Zeitraffer, jeweils mit Tastenkürzel. Umschaltknöpfe stehen wie die Tasten für den passenden Befehl (VSC/VSC ENDE, SC/SC REIN, ROT/NEUSTART, BOX ZU/BOX AUF); aktive Zustände farbig, nicht erlaubte grau. Rot fragt nach (JA, ROT / NEIN, verfällt nach 5 s). Der Log nennt die Knopfpositionen. Prüfung: `unreal/TestScripts/control_panel_clicks.txt` (alle Knöpfe per Klick) und `control_panel_commands.txt` (dieselben Befehle direkt) ergeben dieselbe Befehlsfolge und dasselbe Rennergebnis; Überrundete ohne SC wird abgelehnt; Regression unverändert). M8.5 erledigt (Streckenkarte unten rechts, `UITrackMapWidget`: Marshal-Sektoren in Flaggenfarbe, gewählter Sektor breiter, Autos als Punkte (beschriftet nur Führender und verfolgtes Auto), SC und Unfallstellen, Start/Ziel. Klick auf einen Sektor öffnet ein Menü Grün/Gelb/Doppelt Gelb, Klick auf ein Auto setzt die TV-Kamera darauf (neuer Befehl `FollowCar`, Skript `follow <Kürzel>`). Der Log nennt beim ersten Zeichnen die Bildschirmpositionen der Sektoren, Menüzeilen und Autos für Prüfskripte. Prüfung mit `unreal/TestScripts/track_map_clicks.txt`: Klick aufs Auto → "TV ACH", Klick auf Sektor 8 und "Gelb" → Sektor 8 gelb, im Offscreen-Bild sichtbar; Karte auch auf Valle Alta geprüft; Regression unverändert. Aufgefallen: Valle Alta kreuzt sich in der Draufsicht selbst, ohne Brücke). M8.4 erledigt (Befehlsschicht `AUIRaceDirector::Execute` in `UIRaceCommands.cpp`: alle Aktionen (Flaggen, VSC, SC, Rot, Neustart, Überrundete, Boxengasse, Kamera, Zeitraffer, Vorfallauswahl, Review, Urteil) laufen darüber, werden geloggt und in unpassenden Zuständen abgelehnt. Die Tasten sind umgestellt, neu: L Überrundete, P Boxengasse, 6 Disqualifikation. `-UISimScript=<Datei>` führt Befehle zu Rennzeiten aus (Beispiel `unreal/TestScripts/sc_at_600.txt`), und zwar im festen Sim-Takt statt pro Bild: vorher hingen skriptierte Eingriffe (auch `-UISimSafetyCarAt`) vom Zeitraffer und der Bildrate ab. `click x y` schickt einen Klick als Slate-Ereignis eines eigenen virtuellen Nutzers, der echte Mauszeiger bleibt unberührt; der Log nennt das getroffene Widget. Prüfung: Skript-SC bei 600 s und Schalter liefern dasselbe Ergebnis, auch bei anderem Zeitraffer; ohne Eingriffe identisch zum Runner). M8.3 erledigt (*Mischungspflicht:* Mit 1 % Wahrscheinlichkeit pro Auto übersieht das Team B6.3.6 (eigener Zufallsstrom, das übrige Rennen bleibt gleich). Wer so ohne zweite Mischung ins Ziel kommt, erzeugt genau einen Vorfall "Mischungspflicht"; richtig ist die neue Strafe Disqualifikation, das Auto steht dann ganz unten (Runner: DSQ). Den Fall "Rennen unterbrochen und nicht neu gestartet" (+30 s) gibt es in der Sim nicht. *Boxengasse schließen:* Befehle `ClosePitEntry`/`OpenPitEntry`; geplante Stopps warten, Drive-Through und Stop-and-Go ruhen; mit 5 % pro fälligem Stopp fährt ein Auto trotzdem ein, das ist ein Vorfall mit Stop-and-Go nach B1.6.4 b. Unreal kennt die neuen Vorfallstypen und die Disqualifikation (Taste 6 im Review), die Bedienung der Boxengasse kommt mit 8.4. 62 Kern-Tests, Runner und Unreal ohne Eingriffe unverändert und identisch). M8.2 erledigt (neuer Befehl `LappedCarsMayOvertake` unter SC: Überrundete in der Schlange, also eine Runde oder mehr hinter dem Führenden oder zwischen Führendem und SC, fahren auf der Seite an Feld und SC vorbei und dann im Renntempo hinten an; das SC kommt nach der nächsten Überquerung der Linie in dieser Runde herein, ohne Befehl. Test über 8 Seeds: 18 freigegebene Autos, 10 rechtzeitig hinten, kein Auto der Führungsrunde hinter einem Freigegebenen, keine Verstöße. Ohne den Befehl ist das Rennen unverändert, Runner und Unreal identisch. Freigabe nur per Befehl, noch nicht automatisch; in Unreal bedienbar ab 8.4. Der alte SC-Test prüft die Schlange weiter nur für die Führungsrunde, weil er ohne Freigabe läuft). M8.1 erledigt (Reglement geprüft: Überrundete unter SC nach B5.13.4 c, SC eine Runde danach herein nach B5.13.5 b, geschlossene Boxeneinfahrt nach B1.6.4 b mit Stop-and-Go; die Disqualifikation bei Verstoß gegen B6.3.6 stand schon seit 7.1 in der Regeldatei. Das Abnahmekriterium von 8.2 ist entsprechend korrigiert: der Neustart wartet nicht auf die Überrundeten). M7 erledigt (7.8: Zeitenturm zeigt Mischung, Reifenalter, Stopps und SW-Flagge mit Zahl der Verstöße; jedes Team hat eine eigene Box, im Abstand von 12 m um die Linie verteilt, und seine Autos halten dort; Boxengasse und Garagen als Blockout. Offen: Boxengasse und Garagen sind noch nicht im Bild geprüft, keine Kamera zeigt die Boxengasse). M7.1 erledigt (Reglement geprüft). M7.2 erledigt: Jede Unfallstelle merkt sich die Vorbeifahrten (Risiko ×0,97 pro Auto) und löst höchstens 2 Folgeunfälle aus. Ohne Eingriffe fallen im Mittel ~1,9 statt 10,5 Autos aus (20 Seeds). M7.3/7.4 erledigt: Soft/Medium/Hard als Streckendaten (Tempo, Verschleiß, Klippe), Startreifen nach Startplatz, Teams planen Stopps vor der Klippe und stoppen unter SC/VSC früher, B6.3.6 durch die Reifenwahl erfüllt, 5 s/10 s werden beim nächsten Stopp abgesessen (B1.9.6 b), neue Reifen unter Rot. Überholungen ~48 pro Rennen. Offen: Ein Verstoß gegen B6.3.6 (Disqualifikation) kann bisher nicht entstehen und ist deshalb noch kein Vorfallstyp. M7.5 erledigt: Speeding in der Boxengasse (Limiter-Fehler 2 % pro Durchfahrt, 1–20 km/h zu schnell; Ground Truth nach Guidelines: unter 6 km/h 5 s, 6–15 km/h Drive-Through, darüber Stop-and-Go) und Unsafe Release (Auto auf der Fahrspur weniger als 30 m entfernt, das Team lässt in 25 % der Fälle trotzdem los, sonst wartet es; 5 s, bei weniger als 10 m Abstand 10 s). M7.6 erledigt: Track Limits in markierten Kurven (Streckendaten `track_limits`, im Testring T4 und die letzte Kurve). Die Neigung hängt stark von der Aggressivität ab (Aggressivität hoch 4), und abgefahrene Reifen erhöhen sie. Race Control zählt automatisch mit: beim 3. Verstoß die schwarz-weiße Flagge, ab dem 4. Verstoß jeweils ein Vorfall, richtig sind 5 s. Nebenbei behoben: Die Tests für VSC und SC berücksichtigen jetzt Boxenstopps; die Kontrolle auf Überholen unter SC zählt nur Autos in der Führungsrunde, weil das Vorbeiwinken Überrundeter unter SC noch fehlt. Überholchance auf 1,35 nachgestellt. M7.7 erledigt: zweite fiktive Strecke `data/tracks/valle-alta.json` (4,66 km, Bergstrecke mit mehr langsamen Kurven, kürzeren Geraden und stärkerem Reifenverschleiß; die Geradenlängen sind so berechnet, dass die Runde schließt). Die zentralen Eigenschaften (Streckendaten, Determinismus, Überholungen 20–50, Boxenstopps, Pflicht der zwei Mischungen) werden auf beiden Strecken getestet, die übrigen Kern-Tests laufen weiter nur auf dem Testring. Unreal wählt die Strecke mit `-UISimTrack=<Datei>`; Runner und Unreal liefern für beide Strecken identische Ergebnisse. M1–M6 erledigt (55 Kern-Tests, Unreal-Ergebnis identisch zum Konsolen-Runner). Unfälle ohne Kontakt sind eigene Vorfälle im Review (zählen in der Bewertung nur, wenn entschieden); der Zeitenturm zeigt den Overtake-Modus (OVT).

### 8.1 Plan M6: Unfälle & Sicherheit

**Ziel:** Bisher macht nichts Flaggen, VSC oder Safety Car nötig: Kollisionen kosten nur Sekunden. M6 bringt stehende Autos und Trümmer auf die Strecke. Der Spieler muss reagieren, sonst drohen Folgeunfälle. Die Bewertungskategorie "Sicherheit" wird damit messbar.

Reihenfolge = Abarbeitungsreihenfolge. Jedes Paket endet mit seinem Abnahmekriterium; Kern-Pakete werden mit Tests abgesichert, Unreal-Pakete mit unsichtbaren Prüfläufen (`-nullrhi` / `-RenderOffscreen`).

| # | Paket | Inhalt | Fertig, wenn … |
|---|---|---|---|
| 6.1 | **Git** | Repository anlegen, `.gitignore` für Kern-Build und Unreal-Ausgaben prüfen, Git LFS für künftige Binär-Assets vorbereiten, Stand M5 als erster Commit. | `git status` sauber, erster Commit enthält Kern, Daten, Unreal-Quellen und Doku, aber keine Build-Ausgaben. |
| 6.2 | **Reglement prüfen** | Aktuelle FIA-Fassung (Section B, Issue 07 oder neuer) gegen `data/rules/fia-f1-2026.json` abgleichen: Artikelnummern für VSC, SC, Unterbrechung, blaue Flaggen, Override-Modus. Werte bleiben Sim-Vereinfachungen. | Jeder Eintrag hat eine geprüfte Quelle (`verified: true`) oder einen begründeten Vermerk, warum nicht. |
| 6.3 | **Unfälle im Kern** | Neue Ereignisse: *Dreher* (Auto steht kurz, fährt weiter), *Abflug* (Auto steht neben der Strecke), *schwerer Unfall* (Auto steht auf der Strecke, Trümmer im Marshal-Sektor, Ausfall). Auslöser: eskalierende Fahrfehler und Kollisionen, abhängig von Kurventempo und Reifen. Stehende Autos und Trümmer sind Hindernisse. Bergung durch Marshals dauert je nach Lage 60–180 s und ist auf der Strecke nur unter doppelt Gelb, VSC oder SC erlaubt. | Tests: ein Unfall erzeugt ein stehendes Hindernis an einem festen Ort; die Bergung wartet unter Grün und läuft unter VSC/SC; ausgefallene Autos werden korrekt gewertet; Unfallrate pro Rennen liegt in einem festgelegten Zielbereich; Determinismus bleibt erhalten. |
| 6.4 | **Folgeunfälle** | Annähernde Autos an einer ungesicherten Gefahrenstelle (keine passende Flagge im Sektor) haben ein erhöhtes Unfallrisiko, abhängig von Tempo und Sichtweite. Trümmer erhöhen das Risiko eines Reifenschadens. Gelb senkt das Risiko, doppelt Gelb/VSC/SC fast auf null. | Test: Gleiche Unfallsituation mit und ohne Flagge; ohne Flagge entstehen über viele Seeds deutlich mehr Folgeunfälle. |
| 6.5 | **Marshal-Funk** | Funk-Log im Kern: Meldungen der Streckenposten mit kurzer Verzögerung ("Posten 7: Auto 22 steht im Kies, Fahrer ok"), Bergungsanfragen, Meldung "Strecke frei". In Unreal als Funk-Log-Panel. | Jeder Unfall erzeugt eine Meldung mit Ort und Lage; das Panel zeigt sie in Unreal (Screenshot-Prüfung). |
| 6.6 | **Bewertung Sicherheit** | Neue Kategorie: Reaktionszeit vom Unfall bis zur angemessenen Flagge, Folgeunfälle, Bergungen, die auf eine Neutralisation warten mussten. Übertriebene Neutralisation bleibt über die Rennqualität bestraft. | Tests: schnelle, passende Reaktion ergibt hohe Sicherheit; keine Reaktion ergibt niedrige Sicherheit; Gesamtbewertung gewichtet neu (z. B. 35 % Fairness, 15 % Konsistenz, 30 % Sicherheit, 20 % Rennqualität). |
| 6.7 | **Blaue Flaggen** | Automatisch, wenn ein Überrundender dicht dran ist; der Überrundete muss innerhalb von N Marshal-Sektoren Platz machen (N aus dem Reglement). Ignorieren wird zum Vorfall "Blaue Flaggen ignoriert" mit Ground Truth. | Tests: Überrundete machen in der Regel Platz; ein ignorierender Fahrer erzeugt einen Vorfall; Ergebnis bleibt ohne Ignorieren deterministisch. |
| 6.8 | **Override-Modus 2026** | Ersetzt den erhöhten Windschatten-Bonus aus M1 durch die echte 2026-Mechanik (Verfolger innerhalb der Regel-Schwelle am Messpunkt erhält Extra-Leistung), Werte aus 6.2. Danach Überholquote neu abstimmen. | Überholquote wieder im Zielbereich (20–50 pro Rennen); Test für den Messpunkt. |
| 6.9 | **Darstellung in Unreal** | Verunfallte Autos gedreht/abseits, Trümmer-Marker, Bergungsfahrzeug (Blockout), Funk-Log-Panel, Unfälle als Vorfälle im Review, Sicherheit im Bewertungsbildschirm. | Unsichtbarer Prüflauf mit Unfall zeigt stehendes Auto, Funk-Meldung und Flaggenposten; Regression ohne Eingriffe identisch zum Runner. |

**Bewusst nicht in M6:** Boxenstopps und Reifenstrategie (dann auch Reifenwechsel unter Rot), Bedienung per Maus, Persönlichkeiten im Funk, Wetter.

### 8.2 Plan M7: Boxenstopps & Strategie

**Ziel:** Bisher fährt jedes Auto das ganze Rennen auf einem Satz Reifen. Die Boxengasse wird nur für Strafen genutzt. M7 bringt Reifenverschleiß, echte Stopps und Strategie ins Rennen. Damit entstehen neue Verstöße, die der Spieler ahnden muss: Speeding in der Boxengasse, Unsafe Release, Track Limits. Außerdem wird die Kette der Folgeunfälle gedämpft. Ein vergessener Unfall soll nicht mehr das halbe Feld kosten: ohne Eingriffe fallen heute bei Seed 1 16 von 20 Autos aus.

Reihenfolge = Abarbeitungsreihenfolge. Kern-Pakete werden mit Tests abgesichert, Unreal-Pakete mit unsichtbaren Prüfläufen.

| # | Paket | Inhalt | Fertig, wenn … |
|---|---|---|---|
| 7.1 | **Reglement prüfen** | Aktuelle Fassung (Section B, Issue 09 oder neuer) für Reifen und Boxenstopps prüfen: Pflicht, zwei Trockenmischungen zu fahren; Reifenwechsel unter Rot; Tempolimit, Unsafe Release; Track Limits. Gelbe und blaue Flaggen gegen ISC Anhang H und die Race-Director-Notes prüfen (offen seit M6). Werte nach `data/rules/fia-f1-2026.json`, kein Regeltext. | Jeder neue Eintrag hat eine geprüfte Quelle oder einen begründeten Vermerk; Gelb und Blau sind `verified` oder haben einen Vermerk. |
| 7.2 | **Folgeunfälle entschärfen** | Fahrer reagieren nach einiger Zeit von selbst auf eine Unfallstelle. Wer sie schon passiert hat oder Trümmer sieht, fährt vorsichtiger. Das Risiko sinkt mit jeder Vorbeifahrt, und eine Stelle löst höchstens wenige Folgeunfälle aus. Flaggen bleiben deutlich besser als nichts zu tun. | Ohne Eingriffe fallen pro Rennen im Mittel höchstens 4 Autos aus (über viele Seeds). Der Test aus 6.4 (ohne Flagge deutlich mehr Folgeunfälle) gilt weiter. Die Sicherheitsbewertung bestraft fehlende Reaktionen weiterhin. |
| 7.3 | **Reifen im Kern** | Mischungen Soft, Medium und Hard (Werte als Daten): Grundtempo, Verschleiß pro Runde, Leistungsabfall gegen Ende. Alter und Mischung jedes Autos stehen im Snapshot. Verschleiß erhöht das Fehlerrisiko leicht. Startreifen pro Auto. | Tests: Ein Soft-Stint ist am Anfang schneller und baut schneller ab als Hard; ohne Stopp wird ein Auto gegen Rennende deutlich langsamer; der Determinismus bleibt erhalten. |
| 7.4 | **Boxenstopps und Strategie** | Echte Stopps über die vorhandene Boxengasse: Einfahrt, 80 km/h, Halt an der eigenen Box (Standzeit mit Streuung, seltene langsame Stopps), Ausfahrt. Jedes Team plant ein oder zwei Stopps nach Verschleiß. Unter SC oder VSC ist ein Stopp günstiger, darauf reagieren die Teams. Pflicht von zwei Mischungen nach 7.1; wer sie nicht erfüllt, wird automatisch zu einem Vorfall. Reifenwechsel unter Rot. Strafen werden beim nächsten Stopp vor dem Service abgesessen. | Tests: Jedes Auto im Ziel erfüllt die Mischungspflicht oder hat einen Vorfall; Stopps unter SC häufen sich; die Gesamtzeit eines Stopps liegt im Zielbereich; Überholungen bleiben im Zielbereich (20–50). |
| 7.5 | **Verstöße in der Boxengasse** | *Speeding:* seltener Fehler beim Limiter, gemessen über einen Abschnitt; das wird zum Vorfall mit Ground Truth (Höhe der Überschreitung). *Unsafe Release:* ein Auto wird vor ein anderes in der Fahrspur gelassen; Vorfall gegen das Team, Schuld nach Abstand. | Tests: Beide Vorfälle entstehen in vielen Rennen; die Ground Truth passt zur Situation; ohne diese Fehler bleibt das Rennen deterministisch. |
| 7.6 | **Track Limits** | Fahrfehler in bestimmten Kurven führen über die Linie (Kurven als Daten). Race Control zählt die Verstöße: Verwarnungen, schwarz-weiße Flagge, ab dem nächsten Verstoß ein Vorfall nach 7.1. | Tests: Die Zählung pro Fahrer stimmt; erst der Verstoß nach der schwarz-weißen Flagge erzeugt einen Vorfall; die Rate pro Rennen liegt im Zielbereich. |
| 7.7 | **Zweite Strecke** | Zweite fiktive Strecke als Daten (anderer Charakter: mehr langsame Kurven, kürzere Gerade, andere Overtake-Linien, eigene Boxengasse). Kern-Tests laufen auf beiden Strecken. Unreal wählt die Strecke per `-UISimTrack`. | Alle Kern-Tests sind auf beiden Strecken grün; der Runner und Unreal liefern für beide identische Ergebnisse. |
| 7.8 | **Darstellung in Unreal** | Zeitenturm: Reifen (S/M/H und Alter), Anzahl Stopps, Track-Limits-Verwarnungen. Boxengasse und Boxen als Blockout, Autos halten an ihrer Box. Neue Vorfallstypen in Liste und Review. Funk-Meldungen der Teams zu Stopps sind optional. | Ein unsichtbarer Prüflauf zeigt einen Stopp an der Box und Reifenangaben im Turm; die Regression ohne Eingriffe ist identisch zum Runner. |

**Bewusst nicht in M7:** Wetter (braucht erst Reifen und Stopps, daher M8 oder später), Bedienung per Maus, Persönlichkeiten im Funk, Boxengasse schließen.

### 8.3 Plan M8: Bedienung per Maus

**Ziel:** Bisher läuft alles über Tasten, und man muss die Belegung kennen (Q/E/Y für Sektoren, 0–5 für Strafen, Tab für den Beschuldigten). M8 macht das Spiel mit der Maus bedienbar. Dazu gehören eine Streckenkarte mit Sektoren und Autos, ein Steuerpult mit Knöpfen, eine anklickbare Vorfallliste und ein Strafen-Dialog. Die Tasten bleiben als Kürzel erhalten. Dazu kommen vier Reste aus M7: Überrundete unter SC, der Verstoß gegen die Mischungspflicht, Boxengasse schließen und eine Kamera auf die Boxengasse.

**Prüfung ohne echte Maus:** Prüfläufe dürfen den Mauszeiger nicht fangen und kein Fenster zeigen. Deshalb laufen alle Aktionen über eine Befehlsschicht, die Tasten, Knöpfe und Prüfskripte gemeinsam nutzen. Klicks werden in Prüfläufen als synthetische Slate-Ereignisse an eine Bildschirmposition geschickt; der echte Mauszeiger bleibt unberührt. Ergebnis per Log und Offscreen-Screenshot.

Reihenfolge = Abarbeitungsreihenfolge. Kern-Pakete werden mit Tests abgesichert, Unreal-Pakete mit unsichtbaren Prüfläufen.

| # | Paket | Inhalt | Fertig, wenn … |
|---|---|---|---|
| 8.1 | **Reglement prüfen** | Aktuelle Fassung (Section B) prüfen: Vorbeiwinken Überrundeter unter SC (wann, Mindestabstand bis zum Neustart); geschlossene Boxeneinfahrt (Ausnahmen, Strafe); Folge eines Verstoßes gegen die Mischungspflicht. Werte nach `data/rules/fia-f1-2026.json`, kein Regeltext. | Jeder neue Eintrag hat eine geprüfte Quelle oder einen begründeten Vermerk. |
| 8.2 | **Überrundete unter SC** | Race Control gibt nach 8.1 die Überrundeten frei. Sie fahren am Feld und am SC vorbei und schließen hinten an; das SC kommt am Ende der folgenden Runde herein (B5.13.5 b), ohne auf die Überrundeten zu warten. Freigabe automatisch oder per Befehl. | Tests: Nach der Freigabe überholen nur die Überrundeten; beim Neustart stehen alle Autos der Führungsrunde vor den Überrundeten, die rechtzeitig hinten angekommen sind; das SC kommt eine Runde nach der Freigabe herein; die SC-Kontrolle auf Überholen gilt wieder für das ganze Feld; der Determinismus bleibt erhalten. |
| 8.3 | **Mischungspflicht und Boxengasse schließen** | *Mischungspflicht:* Ein seltener Strategiefehler lässt ein Auto ohne zweite Mischung ins Ziel kommen. Das wird zum Vorfall mit Ground Truth nach 8.1 (dafür kommt die Disqualifikation als Strafe hinzu). *Boxengasse schließen:* Race Control sperrt die Einfahrt; geplante Stopps werden verschoben, Strafen ruhen. Selten fährt ein Auto trotzdem ein, das wird zum Vorfall. | Tests: Ohne Strategiefehler erfüllt jedes Auto die Pflicht; der Fehler erzeugt genau einen Vorfall pro betroffenem Auto; bei geschlossener Boxengasse stoppt niemand regulär; Einfahrt trotz Sperre ergibt einen Vorfall mit der Strafe nach 8.1. |
| 8.4 | **Befehlsschicht und Prüfskripte** | Alle Aktionen in Unreal (Flagge pro Sektor, VSC, SC, Rot, Neustart, Überrundete freigeben, Boxengasse, Kamera, Zeitraffer, Review, Urteil) laufen über eine Befehlsfunktion. Die Tasten rufen sie auf. Neu: `-UISimScript=<Datei>` mit Befehlen zu Rennzeiten und synthetischen Klicks an Bildschirmpositionen. Jeder Befehl wird geloggt. | Die Regression ohne Eingriffe bleibt identisch zum Runner. Ein Skript mit SC zu einer festen Zeit liefert dasselbe Ergebnis wie der bisherige Skript-Schalter für den SC. |
| 8.5 | **Streckenkarte** | 2D-Karte der Strecke aus den Streckendaten, Marshal-Sektoren in Flaggenfarbe, Autos als Punkte mit Kürzel, Unfallstellen und SC markiert. Klick auf einen Sektor öffnet die Auswahl Grün, Gelb, doppelt Gelb. Klick auf ein Auto setzt die Kamera auf dieses Auto. Für beide Strecken. | Offscreen-Screenshot zeigt die Karte mit Autos und Sektorfarben; ein synthetischer Klick auf einen Sektor setzt dort Gelb (Log und Bild); Klick auf ein Auto wechselt die Kamera. |
| 8.6 | **Steuerpult per Maus** | Knöpfe für VSC, SC, Rot/Neustart, Überrundete freigeben, Boxengasse schließen/öffnen, Zeitraffer und Kameras. Knöpfe zeigen den Zustand (aktiv, nicht erlaubt) und eine Kurzhilfe mit dem Tastenkürzel. Riskante Befehle (Rot) fragen nach. | Synthetische Klicks lösen jeden Befehl aus, das Ergebnis ist identisch zur Taste; nicht erlaubte Befehle sind gesperrt (z. B. Neustart ohne Rot). |
| 8.7 | **Vorfälle und Strafen-Dialog** | Vorfallliste anklickbar, im Review Zeitleiste zum Ziehen, Play/Pause und Kameramodus als Knöpfe. Strafen-Dialog: Beschuldigten wählen, Strafe wählen (mit Erklärung in eigenen Worten und Artikelnummer), bestätigen. Bewertungsbildschirm mit Knöpfen zum Durchblättern. | Ein Skript führt per Klick durch einen Vorfall bis zum Urteil; das Urteil ist dasselbe wie per Taste; Screenshot des Dialogs. |
| 8.8 | **Kamera Boxengasse und Darstellung** | Eigene Kamera auf die Boxengasse, im Review wählbar für Stopps, Speeding und Unsafe Release; Boxengasse und Garagen im Bild prüfen (offen seit 7.8). Zeitenturm: DSQ, Anzeige "Boxengasse geschlossen", Überrundete beim Vorbeifahren. Neue Vorfallstypen in Liste und Review. | Offscreen-Screenshot zeigt die Boxengasse mit haltendem Auto; Vorfälle aus 8.3 erscheinen in der Liste; die Regression ohne Eingriffe ist identisch zum Runner. |

**Bewusst nicht in M8:** Wetter, Persönlichkeiten im Funk, Steward-Anhörungen, Karriere, Gamepad-Steuerung, freie Tastenbelegung.

### 8.4 Plan M9: Wetter und echte Strecken

**Ziel:** Bisher ist es immer trocken. M9 bringt Regen: Die Strecke wird nass und trocknet wieder ab, dazu kommen Intermediate- und Regenreifen. Teams entscheiden, wann sie wechseln. Für den Spieler gibt es neue Entscheidungen: die Strecke nass erklären, bei starkem Regen SC oder Rot, Start hinter dem SC. Dazu kommen die echten Rennstrecken als private Daten, und die fiktive Valle Alta entfällt. Zwei Reste aus M8 werden erledigt: Überrundete werden unter SC automatisch freigegeben, und DSQ sowie UNLAP werden im Bild geprüft.

**Echte Strecken nur privat:** Die Streckenformen kommen aus dem freien Datensatz *f1-circuits* (GitHub bacinger/f1-circuits, MIT-Lizenz). Alle Strecken, die er enthält, werden übernommen, auch die nicht mehr gefahrenen. Die Namen werden leicht und erkennbar abgewandelt. Wie das Parodie-Feld bleiben sie privat. Rohdaten, Namensliste und erzeugte Strecken liegen in `data/tracks/private/`, und `.gitignore` hält den Ordner aus dem öffentlichen Repository. Der Konverter selbst ist allgemein und öffentlich. Öffentliche Tests laufen auf dem Testring. Tests auf echten Strecken laufen nur, wenn die privaten Dateien vorhanden sind.

Reihenfolge = Abarbeitungsreihenfolge. Kern-Pakete werden mit Tests abgesichert, Unreal-Pakete mit unsichtbaren Prüfläufen.

| # | Paket | Inhalt | Fertig, wenn … |
|---|---|---|---|
| 9.1 | **Reglement prüfen** | Aktuelle Fassung (Section B) prüfen: Intermediate- und Regenreifen im Rennen, Wegfall der Mischungspflicht bei Nässe (B6.3.6), Erklärung "nasses Rennen" (Ergebnis: gibt es im Rennen nicht), Start hinter dem SC und stehender Start danach, Unterbrechung bei Regen, Regenlicht. Werte nach `data/rules/fia-f1-2026.json`, kein Regeltext. | Jeder neue Eintrag hat eine geprüfte Quelle oder einen begründeten Vermerk. |
| 9.2 | **Wetter im Kern** | Wetterverlauf als Daten pro Rennen (Regenstärke über die Zeit, mit Zufall aus dem Seed). Die Nässe der Strecke steigt mit dem Regen und sinkt danach, zuerst auf der Ideallinie. Nässe senkt den Grip (Kurventempo) und erhöht das Fehlerrisiko. Gischt senkt die Sicht für Hinterherfahrende, das wirkt auf das Risiko von Folgeunfällen. Wetter und Nässe stehen im Snapshot. | Tests: Ohne Regen bleibt das Rennen unverändert (Regression identisch); bei Regen steigen Rundenzeiten und Unfallrate deutlich; nach dem Regen trocknet die Strecke in einem festgelegten Zeitraum; der Determinismus bleibt erhalten. |
| 9.3 | **Regenreifen und Strategie** | Intermediate und Wet als Daten, mit Tempo abhängig von der Nässe und Verschleiß auf trockener Strecke. Teams wechseln nach dem Kreuzungspunkt, mit Verzögerung und gelegentlichem Pokern (zu früh, zu spät). Die Mischungspflicht entfällt bei Nässe (nach 9.1). | Tests: Auf nasser Strecke ist Intermediate schneller als Slick und Wet schneller als Intermediate bei starkem Regen; beim Abtrocknen wechseln die Teams wieder auf Slicks; wer nass gefahren ist, bekommt keinen Mischungsvorfall. |
| 9.4 | **Race Control im Regen** | Neue Befehle (nach 9.1): Start hinter dem SC, wahlweise mit Regenreifen-Pflicht (B5.10.1 a, B6.3.7: Wechsel bis zum Ende der SC-Phase kostet Stop-and-Go), danach stehender oder fliegender Start (B5.10.8/9); das Rennen verkürzt sich um die SC-Runden minus eins (B5.10.7). Dasselbe für den Neustart nach Rot (B5.15.2 a). Eine Erklärung "Strecke nass" gibt es im Rennen nicht. Bei starkem Regen und stehendem Wasser steigt die Aquaplaning-Gefahr stark, SC oder Rot sind dann angemessen. Die Bewertung "Sicherheit" berücksichtigt das Rennen bei starkem Regen ohne Neutralisation, die Rennqualität bestraft übertriebene Neutralisation weiterhin. | Tests: Starker Regen ohne Eingriff ergibt deutlich mehr Unfälle und eine niedrige Sicherheitsbewertung; SC oder Rot senken das Risiko; der Start hinter dem SC läuft regelkonform ab. |
| 9.5 | **Überrundete automatisch** | Race Control gibt die Überrundeten unter SC von selbst frei, wenn die Schlange formiert ist und der Spieler nichts tut (Zeitpunkt als Sim-Vereinfachung, nach 8.1 und 9.1). Der Befehl aus 8.2 bleibt. | Tests: Ohne Befehl werden Überrundete in der Schlange freigegeben, danach kommt das SC herein; mit Befehl bleibt das Verhalten wie in 8.2; ohne SC ändert sich nichts. |
| 9.6 | **Echte Strecken** | Download der GeoJSON-Dateien aus f1-circuits (Lizenz in `docs/ASSETS.md`). Ein Konverter (öffentlich, `core/tools`) macht daraus unser Streckenformat: Abtasten, Krümmung zu Segmenten, Kurventempo aus dem Radius, Sektoren, Marshal-Sektoren, Track-Limits-Kurven, Overtake-Linien, Boxengasse an Start/Ziel. Abgewandelte Namen kommen aus einer privaten Liste, die Ausgabe nach `data/tracks/private/`. Valle Alta wird entfernt, ihre Tests wandern auf den Testring oder auf private Strecken. | Alle Strecken des Datensatzes werden erzeugt und schließen innerhalb der Toleranz; Rundenzeiten und Überholungen liegen für bekannte Strecken in einem plausiblen Bereich; `git status` zeigt keine privaten Dateien; Runner und Unreal liefern auf einer echten Strecke identische Ergebnisse. |
| 9.7 | **Darstellung in Unreal** | Regen sichtbar (dunklerer Himmel, nasse Fahrbahn, einfache Gischt), Wetter- und Radar-Anzeige mit Vorhersage, Reifen I und W im Zeitenturm, Knöpfe "Start hinter SC", "Regenreifen-Pflicht" und "Stehender Start" im Steuerpult. Strecken aus `data/tracks/private` per `-UISimTrack`. | Offscreen-Bilder zeigen Regen und eine echte Strecke mit Karte; ein Skript startet hinter dem SC mit Regenreifen-Pflicht und gibt den stehenden Start frei; die Regression ohne Regen ist identisch zum Runner. |
| 9.8 | **DSQ und UNLAP im Bild** | Gezielte Prüfläufe: ein erzwungener Verstoß gegen die Mischungspflicht mit Urteil Disqualifikation, und ein Überrundeter mitten in der SC-Schlange. | Offscreen-Bilder zeigen DSQ im Zeitenturm und UNLAP beim Vorbeifahren. |

**Bewusst nicht in M9:** Persönlichkeiten im Funk, Steward-Anhörungen, Karriere, Training und Qualifying, wechselnde Tageszeit.

### 8.5 Plan M10: Saison und Karriere

**Ziel:** Bisher steht jedes Rennen für sich. M10 macht daraus eine Saison: ein Kalender mit mehreren Rennen, WM-Stand für Fahrer und Teams, und eine Bewertung des Spielers über die ganze Saison. Entscheidungen wirken über das Rennen hinaus (Strafpunkte, Sperre). Der Spielstand wird zwischen den Rennen gespeichert. Dazu kommen die vier Reste aus M9: BLUE und OVT im Zeitenturm, zu viele Überholungen auf schnellen Strecken, der stehende Start und Gischt hinter den Autos.

**Annahmen:**
- Die Saison-Logik liegt im Kern (ohne Unreal), wie alles andere. Unreal zeigt sie nur an.
- Gespeichert wird nur zwischen den Rennen, nicht mitten im Rennen.
- Training und Qualifying gibt es weiter nicht. Die Startaufstellung entsteht aus dem Tempo der Autos mit Zufall aus dem Seed.
- Öffentlich gibt es einen Beispiel-Kalender nur mit dem Testring. Der Kalender mit den echten Strecken liegt privat in `data/seasons/private/` (gitignored).
- Der stehende Start ändert jedes Rennergebnis. Die bisherige Vergleichsbasis (Seed 1, 39 Überholungen) wird mit 10.3 einmal neu festgelegt; Runner und Unreal müssen danach wieder identisch sein.

Reihenfolge = Abarbeitungsreihenfolge. Kern-Pakete werden mit Tests abgesichert, Unreal-Pakete mit unsichtbaren Prüfläufen.

| # | Paket | Inhalt | Fertig, wenn … |
|---|---|---|---|
| 10.1 | **Reglement prüfen** | Aktuelle Fassung prüfen: WM-Punkte für Fahrer und Konstrukteure, Punkte bei verkürztem Rennen, Renndistanz (305 km, Ausnahme Monaco), Ablauf des stehenden Starts (Einführungsrunde, Ampel, Startabbruch), Frühstart und falsche Startposition mit Strafen, Strafpunkte und Sperre. Werte nach `data/rules`. | Alle Werte mit Artikel in der Regeldatei, Abweichungen vom Plan hier vermerkt. |
| 10.2 | **Reste im Kern** | BLUE nicht mehr für ausgefallene Autos, OVT nicht mehr für Autos im Ziel. Überholungen auf langen Vollgas-Passagen dämpfen (im Sim-Modell, nicht pro Strecke von Hand). | Tests für BLUE und OVT. Alle privaten Strecken liegen bei 15–70 Überholungen (Seed 1), der Testring bleibt bei 20–50. |
| 10.3 | **Stehender Start** | Startaufstellung in Reihen, Ampel, Reaktionszeiten, Positionswechsel bis zur ersten Kurve, Startunfälle. Neue Vorfälle: Frühstart und falsche Startposition (Strafen nach 10.1). Stehender Start auch nach dem Start hinter dem SC (B5.10.8) und beim Neustart nach Rot. Neue Vergleichsbasis. | Tests: Autos stehen bis zum Erlöschen der Ampel, Frühstart erzeugt genau einen Vorfall mit der richtigen Strafe, Positionswechsel in Runde 1 im plausiblen Bereich, deterministisch. Runner und Unreal identisch. |
| 10.4 | **Saison im Kern** | Kalender als Daten (Strecke, Runden aus der Renndistanz, Wetter). Punkte nach 10.1, WM-Stand für Fahrer und Teams mit Gleichstandsregel. Runner `--season <Datei>` fährt die ganze Saison. | Tests: Punkte und Stand stimmen für ein Beispiel von Hand, gleicher Seed ergibt dieselbe Saison. Der Runner gibt nach jedem Rennen den Stand aus. |
| 10.5 | **Karriere und Spielstand** | Saisonwertung des Spielers aus den Rennbewertungen. Strafpunkte pro Fahrer aus den Urteilen des Spielers, Sperre für das nächste Rennen bei Erreichen der Grenze (nach 10.1). Speichern und Laden als JSON zwischen den Rennen. | Tests: Speichern, Laden und Weiterfahren ergibt dieselbe Saison wie ohne Unterbrechung. Ein gesperrter Fahrer fehlt im nächsten Rennen. |
| 10.6 | **Saison in Unreal** | Saison-Bildschirm mit Maus: Kalender, nächstes Rennen, WM-Stand, Bewertungen der bisherigen Rennen, Strafpunkte. Knopf "Rennen starten", nach der Rennbewertung zurück zum Saison-Bildschirm. Spielstand wird automatisch gesichert. | Offscreen-Bilder zeigen Saison-Bildschirm vor und nach einem Rennen. Ein Prüfskript fährt zwei Rennen hintereinander, der Stand ist identisch zum Runner. |
| 10.7 | **Start und Gischt in Unreal** | Startaufstellung und Ampel im Bild, Knopf "Stehender Start" im Steuerpult (nach SC-Start und Rot), neue Vorfälle in Liste und Strafen-Dialog. Gischt hinter den Autos, abhängig von Nässe und Tempo. | Offscreen-Bilder zeigen Aufstellung mit Ampel und Gischt im Regen. Regression identisch zum Runner. |
| 10.8 | **Abschluss** | Eine ganze private Saison unsichtbar durchfahren, mit Speichern und Laden in der Mitte. Auffälligkeiten festhalten. | Lauf ohne Fehler, Endstand identisch zum Runner, offene Punkte hier vermerkt. |

**Bewusst nicht in M10:** Persönlichkeiten im Funk, Steward-Anhörungen, Training und Qualifying, Fahrerwechsel und Teamentwicklung über die Saison, Sprintrennen, wechselnde Tageszeit.

### 8.6 Plan M11: Steward-Anhörungen

**Ziel:** Bisher urteilt der Spieler nur nach Replay und Telemetrie. M11 macht das Urteilen zum Kern: Beide Seiten eines Vorfalls sagen aus, die Aussagen können stimmen, übertreiben oder falsch sein, und der Spieler prüft sie gegen die Beweise. Wer deutlich daneben liegt, bekommt einen Einspruch des Teams, und ein Urteil kann kassiert werden. Dazu kommen vier Reste aus M10: eine ausgeglichenere Saison, der vollständige Start, die vollständige Karriere und Feinschliff in Unreal.

**Annahmen:**
- Die Aussagen entstehen im Kern aus der Ground Truth und einer neuen Eigenschaft der Fahrer (Ehrlichkeit). Es gibt keine frei erzeugten Texte: Aussagen sind Bausteine mit Werten, Unreal setzt sie in Sätze um.
- Eine Anhörung kostet Zeit. Während des Rennens verzögert sie das Urteil, nach dem Rennen ist sie ohne Zeitdruck möglich; die Bewertung kennt beides schon (Urteile nach dem Rennen kosten Rennqualität).
- Die Tagesform gilt nur in Saisonrennen. Einzelrennen bleiben unverändert, damit die Vergleichsbasis mit Seed 1 hält.
- Der Spielstand bekommt ein neues Format; alte Spielstände werden beim Laden abgelehnt, nicht umgewandelt.

Reihenfolge = Abarbeitungsreihenfolge. Kern-Pakete werden mit Tests abgesichert, Unreal-Pakete mit unsichtbaren Prüfläufen.

| # | Paket | Inhalt | Fertig, wenn … |
|---|---|---|---|
| 11.1 | **Reglement prüfen** | Aktuelle Fassung prüfen: Untersuchung und Anhörung durch die Stewards (Vorladung, Fristen, Entscheidung nach dem Rennen), Protest und Right of Review, Einführungsrunde, Startabbruch und zusätzliche Einführungsrunde im Detail, Fahrerwechsel und Ersatzfahrer. Werte nach `data/rules`. | Alle Werte mit Artikel in der Regeldatei, Abweichungen vom Plan hier vermerkt. |
| 11.2 | **Ausgeglichenere Saison** | Tagesform je Saisonrennen für Fahrer und Team aus dem Seed, dazu eine Stärke je Streckentyp (schnell, langsam, nass). Kein Eingriff in Einzelrennen. | Tests: Private Saison mit den Seeds 1 bis 3: mindestens 6 verschiedene Sieger, kein Fahrer gewinnt mehr als die Hälfte, mindestens 14 Fahrer punkten. Gleicher Seed, gleiche Saison. Einzelrennen bitgleich. |
| 11.3 | **Start vervollständigen** | Einführungsrunde vor dem stehenden Start, abgewürgtes Auto in der Startaufstellung, Befehle für Startabbruch und zusätzliche Einführungsrunde; das Rennen wird jeweils um eine Runde kürzer (B5.8, B5.9), der Verursacher startet aus der Boxengasse. | Tests: Nach der Einführungsrunde steht jedes Auto in seiner Box. Ein abgewürgtes Auto ohne Startabbruch erhöht das Unfallrisiko am Start, mit Abbruch startet es aus der Boxengasse und das Rennen hat eine Runde weniger. Neue Vergleichsbasis, Runner und Unreal identisch. |
| 11.4 | **Aussagen und Beweise** | Pro Vorfall Beweise aus der Aufzeichnung (Bremspunkt, Tempounterschied, Linie, Abstand) und je eine Aussage beider Seiten. Die Ehrlichkeit des Fahrers entscheidet, ob die Aussage zur Aufzeichnung passt. | Tests: Ein ehrlicher Fahrer sagt nie etwas, das den Beweisen widerspricht. Ein unehrlicher Fahrer widerspricht ihnen in einem festgelegten Anteil der Fälle. Jeder Widerspruch ist an einem Beweis prüfbar. Deterministisch. |
| 11.5 | **Anhörung und Right of Review** | Befehl "Anhörung" (verzögert das Urteil im Rennen), Urteil wie bisher. Nach 11.1 gibt es gegen Strafen aus dem Rennen keine Berufung, nur das Right of Review, und das braucht ein neues Element, das bei der Entscheidung nicht vorlag. In der Sim sind das die Beweise, die der Spieler nicht gesehen hat, weil er ohne Anhörung geurteilt hat: Liegt ein solches Urteil deutlich neben der Ground Truth, beantragt das Team die Überprüfung, und das Urteil wird vor dem nächsten Rennen kassiert. Nach einer Anhörung ist kein Review möglich; ein falsches Urteil kostet dann nur Bewertung. Kassierte Urteile gehen in Bewertung und Karriere ein. | Tests: Richtiges Urteil ergibt keinen Antrag. Ein deutlich falsches Urteil ohne Anhörung wird kassiert und die Strafe zurückgenommen, soweit das noch geht. Dasselbe Urteil nach einer Anhörung bleibt bestehen. Die Bewertung sinkt mit jedem kassierten Urteil. |
| 11.6 | **Karriere vervollständigen** | Ersatzfahrer für gesperrte Fahrer (nach 11.1), Verfall der Strafpunkte, Einzelwerte der Bewertung je Rennen im Spielstand zugänglich, neues Spielstand-Format. | Tests: Das Feld bleibt bei einer Sperre vollständig, der Ersatzfahrer punktet für das Team. Strafpunkte verfallen nach der festgelegten Frist. Speichern, Laden und Weiterfahren ergibt dieselbe Saison. |
| 11.7 | **Anhörung in Unreal** | Anhörungs-Dialog mit beiden Aussagen, den Beweisen und dem Replay; Widersprüche lassen sich markieren. Meldung bei Einspruch mit Ergebnis. Knöpfe für Startabbruch und zusätzliche Einführungsrunde. | Offscreen-Bilder zeigen den Dialog mit Aussagen und Beweisen und die Einspruch-Meldung. Klicks und Befehle ergeben dasselbe Rennergebnis. |
| 11.8 | **Unreal-Feinschliff** | Größere Schrift und Einzelwerte auf dem Saison-Bildschirm, Rückmeldung am Schalter "STEHEND", durchscheinende Gischt, die vier seit M10.3 nicht gefahrenen Prüfskripte neu abstimmen. | Offscreen-Bilder zeigen die Änderungen. Alle Prüfskripte in `unreal/TestScripts` laufen mit dem aktuellen Rennverlauf. |
| 11.9 | **Abschluss** | Eine ganze private Saison unsichtbar durchfahren, mit Anhörungen durch einen Ersatzspieler und Speichern und Laden in der Mitte. Auffälligkeiten festhalten. | Lauf ohne Fehler, Endstand identisch zum Runner, offene Punkte hier vermerkt. |

**Bewusst nicht in M11:** Persönlichkeiten im Funk, Training und Qualifying, Berufung vor höheren Instanzen, Geldstrafen, Sprintrennen, Teamentwicklung über die Saison.

---

## 9. Technik-Entscheidungen

| Entscheidung | Gewählt | Begründung |
|---|---|---|
| Engine | Unreal Engine 5.8 (installiert unter `F:\Program Files\Epic Games\UE_5.8`) | 3D-Autos, Kameras, Replays gewünscht; Engine ist bekannt |
| Sim-Sprache | C++20, engine-unabhängig | Performance, Determinismus, UE6-Portierbarkeit |
| Build Kern | CMake | unabhängig von UnrealBuildTool testbar |
| Tests | Catch2 oder doctest | leichtgewichtig |
| Daten | JSON | lesbar, leicht editierbar, später Modding |
| Physik | nur bei Unfällen | Kontrollierbarkeit, Performance |
| Versionskontrolle | Git + Git LFS für UE-Assets | Binär-Assets |

---

## 10. Daten und Recht

- **Namen:** keine Verwendung von "F1", "Formula 1", "FIA", echten Team- und Fahrernamen oder Logos in veröffentlichten Versionen. Für den privaten Gebrauch gibt es zwei Starterfelder als Daten:
  - `data/entries/fictional-grid.json`: frei erfundene Teams und Fahrer. **Standard und einzige Option für Veröffentlichungen.**
  - `data/entries/grid-2026-parody.json`: das reale Feld 2026 mit leicht abgewandelten Namen (z. B. "Max Verstoppen", "Ferraro"). Erkennbar gewollt, deshalb **nur privat**: Auch abgewandelte, absichtlich erkennbare Namen können Namens- und Persönlichkeitsrechte der Fahrer und Markenrechte der Teams verletzen. Vor einer Veröffentlichung aus Builds und öffentlichem Repository entfernen.
- **Reglement:** Die Regeln selbst (Abläufe, Flaggenbedeutung, Strafenarten) lassen sich als Spielmechanik nachbauen. Der **Wortlaut** der FIA-Dokumente ist aber urheberrechtlich geschützt, und "FIA" und "Formula 1" sind Marken. Deshalb:
  - Im Spiel Regeln mit eigenen Worten erklären und nur auf Artikelnummern verweisen. Keine Reglement-Texte ins Repository oder ins Spiel kopieren.
  - Privat kann die Regeldatei "FIA F1 2026" heißen. Für eine Veröffentlichung wird sie umbenannt, z. B. in ein generisches "Formel-Reglement". Die Mechanik bleibt gleich.
- **Streckendaten:** Koordinaten aus FastF1/OpenF1 nur für den privaten Gebrauch. Für eine Veröffentlichung braucht es Fantasiestrecken oder eine Prüfung der Nutzungsbedingungen. Streckenlayouts selbst können geschützt sein.
- **Assets:** Lizenz jedes Fremd-Assets in `docs/ASSETS.md` notieren.
- **Spielname:** "Under Investigation" ist Arbeitstitel. Vor einer Veröffentlichung Steam, App-Stores, DPMA und EUIPO prüfen.

---

## 11. Offene Fragen

1. **Spielmodus zum Start:** einzelnes Rennen, oder gleich ein Wochenende (Training, Qualifying, Rennen)?
2. **Rollentrennung:** Spieler ist Race Director *und* Steward, oder später zwei getrennte Rollen mit KI-Kollegen?
3. **Realismus-Stufen:** soll es einen "Arcade"-Modus mit Hilfen geben (Vorfall-Hinweise, Strafvorschläge)?
4. **Zeitmaßstab:** Echtzeit-Rennen oder verkürzte Renndistanz als Standard?
5. **Erste Strecke:** Fantasiestrecke von Anfang an, oder privat eine reale Strecke als Referenz?
6. **Sprache:** UI auf Deutsch, Englisch oder beides (Lokalisierung von Anfang an einplanen)?
