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
| M9+ | Ausbau | Wetter, Funk mit Persönlichkeiten, Steward-Anhörungen, Karriere |

**Stand 2026-11-08:** M8.4 erledigt (Befehlsschicht `AUIRaceDirector::Execute` in `UIRaceCommands.cpp`: alle Aktionen (Flaggen, VSC, SC, Rot, Neustart, Überrundete, Boxengasse, Kamera, Zeitraffer, Vorfallauswahl, Review, Urteil) laufen darüber, werden geloggt und in unpassenden Zuständen abgelehnt. Die Tasten sind umgestellt, neu: L Überrundete, P Boxengasse, 6 Disqualifikation. `-UISimScript=<Datei>` führt Befehle zu Rennzeiten aus (Beispiel `unreal/TestScripts/sc_at_600.txt`), und zwar im festen Sim-Takt statt pro Bild: vorher hingen skriptierte Eingriffe (auch `-UISimSafetyCarAt`) vom Zeitraffer und der Bildrate ab. `click x y` schickt einen Klick als Slate-Ereignis eines eigenen virtuellen Nutzers, der echte Mauszeiger bleibt unberührt; der Log nennt das getroffene Widget. Prüfung: Skript-SC bei 600 s und Schalter liefern dasselbe Ergebnis, auch bei anderem Zeitraffer; ohne Eingriffe identisch zum Runner). M8.3 erledigt (*Mischungspflicht:* Mit 1 % Wahrscheinlichkeit pro Auto übersieht das Team B6.3.6 (eigener Zufallsstrom, das übrige Rennen bleibt gleich). Wer so ohne zweite Mischung ins Ziel kommt, erzeugt genau einen Vorfall "Mischungspflicht"; richtig ist die neue Strafe Disqualifikation, das Auto steht dann ganz unten (Runner: DSQ). Den Fall "Rennen unterbrochen und nicht neu gestartet" (+30 s) gibt es in der Sim nicht. *Boxengasse schließen:* Befehle `ClosePitEntry`/`OpenPitEntry`; geplante Stopps warten, Drive-Through und Stop-and-Go ruhen; mit 5 % pro fälligem Stopp fährt ein Auto trotzdem ein, das ist ein Vorfall mit Stop-and-Go nach B1.6.4 b. Unreal kennt die neuen Vorfallstypen und die Disqualifikation (Taste 6 im Review), die Bedienung der Boxengasse kommt mit 8.4. 62 Kern-Tests, Runner und Unreal ohne Eingriffe unverändert und identisch). M8.2 erledigt (neuer Befehl `LappedCarsMayOvertake` unter SC: Überrundete in der Schlange, also eine Runde oder mehr hinter dem Führenden oder zwischen Führendem und SC, fahren auf der Seite an Feld und SC vorbei und dann im Renntempo hinten an; das SC kommt nach der nächsten Überquerung der Linie in dieser Runde herein, ohne Befehl. Test über 8 Seeds: 18 freigegebene Autos, 10 rechtzeitig hinten, kein Auto der Führungsrunde hinter einem Freigegebenen, keine Verstöße. Ohne den Befehl ist das Rennen unverändert, Runner und Unreal identisch. Freigabe nur per Befehl, noch nicht automatisch; in Unreal bedienbar ab 8.4. Der alte SC-Test prüft die Schlange weiter nur für die Führungsrunde, weil er ohne Freigabe läuft). M8.1 erledigt (Reglement geprüft: Überrundete unter SC nach B5.13.4 c, SC eine Runde danach herein nach B5.13.5 b, geschlossene Boxeneinfahrt nach B1.6.4 b mit Stop-and-Go; die Disqualifikation bei Verstoß gegen B6.3.6 stand schon seit 7.1 in der Regeldatei. Das Abnahmekriterium von 8.2 ist entsprechend korrigiert: der Neustart wartet nicht auf die Überrundeten). M7 erledigt (7.8: Zeitenturm zeigt Mischung, Reifenalter, Stopps und SW-Flagge mit Zahl der Verstöße; jedes Team hat eine eigene Box, im Abstand von 12 m um die Linie verteilt, und seine Autos halten dort; Boxengasse und Garagen als Blockout. Offen: Boxengasse und Garagen sind noch nicht im Bild geprüft, keine Kamera zeigt die Boxengasse). M7.1 erledigt (Reglement geprüft). M7.2 erledigt: Jede Unfallstelle merkt sich die Vorbeifahrten (Risiko ×0,97 pro Auto) und löst höchstens 2 Folgeunfälle aus. Ohne Eingriffe fallen im Mittel ~1,9 statt 10,5 Autos aus (20 Seeds). M7.3/7.4 erledigt: Soft/Medium/Hard als Streckendaten (Tempo, Verschleiß, Klippe), Startreifen nach Startplatz, Teams planen Stopps vor der Klippe und stoppen unter SC/VSC früher, B6.3.6 durch die Reifenwahl erfüllt, 5 s/10 s werden beim nächsten Stopp abgesessen (B1.9.6 b), neue Reifen unter Rot. Überholungen ~48 pro Rennen. Offen: Ein Verstoß gegen B6.3.6 (Disqualifikation) kann bisher nicht entstehen und ist deshalb noch kein Vorfallstyp. M7.5 erledigt: Speeding in der Boxengasse (Limiter-Fehler 2 % pro Durchfahrt, 1–20 km/h zu schnell; Ground Truth nach Guidelines: unter 6 km/h 5 s, 6–15 km/h Drive-Through, darüber Stop-and-Go) und Unsafe Release (Auto auf der Fahrspur weniger als 30 m entfernt, das Team lässt in 25 % der Fälle trotzdem los, sonst wartet es; 5 s, bei weniger als 10 m Abstand 10 s). M7.6 erledigt: Track Limits in markierten Kurven (Streckendaten `track_limits`, im Testring T4 und die letzte Kurve). Die Neigung hängt stark von der Aggressivität ab (Aggressivität hoch 4), und abgefahrene Reifen erhöhen sie. Race Control zählt automatisch mit: beim 3. Verstoß die schwarz-weiße Flagge, ab dem 4. Verstoß jeweils ein Vorfall, richtig sind 5 s. Nebenbei behoben: Die Tests für VSC und SC berücksichtigen jetzt Boxenstopps; die Kontrolle auf Überholen unter SC zählt nur Autos in der Führungsrunde, weil das Vorbeiwinken Überrundeter unter SC noch fehlt. Überholchance auf 1,35 nachgestellt. M7.7 erledigt: zweite fiktive Strecke `data/tracks/valle-alta.json` (4,66 km, Bergstrecke mit mehr langsamen Kurven, kürzeren Geraden und stärkerem Reifenverschleiß; die Geradenlängen sind so berechnet, dass die Runde schließt). Die zentralen Eigenschaften (Streckendaten, Determinismus, Überholungen 20–50, Boxenstopps, Pflicht der zwei Mischungen) werden auf beiden Strecken getestet, die übrigen Kern-Tests laufen weiter nur auf dem Testring. Unreal wählt die Strecke mit `-UISimTrack=<Datei>`; Runner und Unreal liefern für beide Strecken identische Ergebnisse. M1–M6 erledigt (55 Kern-Tests, Unreal-Ergebnis identisch zum Konsolen-Runner). Unfälle ohne Kontakt sind eigene Vorfälle im Review (zählen in der Bewertung nur, wenn entschieden); der Zeitenturm zeigt den Overtake-Modus (OVT).

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
