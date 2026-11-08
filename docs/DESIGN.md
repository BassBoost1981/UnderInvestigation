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
- **Prüfstand (2026-11-08):** Section B, Issue 09 vom 2026-10-01. Relevante Artikel: B1.6.3 (Boxengasse, 80 km/h), B1.9.6 (Strafen absitzen; +20 s / +30 s, wenn Drive-Through bzw. Stop-and-Go nicht mehr absitzbar), B5.12 VSC, B5.13 Safety Car, B5.14 Unterbrechung, B5.15 Wiederaufnahme, B7.2 Overtake-Modus. Flaggen (Gelb, Blau) stehen im ISC Anhang H und in den Race-Director-Notes; beides noch nicht geprüft.
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
| M7+ | Ausbau | Boxenstopps/Strategie, Bedienung per Maus (Streckenkarte, Strafen-Dialog), Funk mit Persönlichkeiten, Wetter, Steward-Anhörungen, mehrere Strecken, Karriere |

**Stand 2026-11-08:** M1–M6 erledigt (46 Kern-Tests, Unreal-Ergebnis identisch zum Konsolen-Runner). Unfälle ohne Kontakt sind eigene Vorfälle im Review (zählen in der Bewertung nur, wenn entschieden); der Zeitenturm zeigt den Overtake-Modus (OVT).

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
