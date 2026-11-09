# Fremd-Assets und ihre Lizenzen

Jedes fremde Asset, das das Projekt nutzt, steht hier mit Quelle, Lizenz und Verwendung (siehe DESIGN.md Kapitel 10).

| Asset | Quelle | Lizenz | Verwendung |
|---|---|---|---|
| Streckenformen echter Rennstrecken (GeoJSON) | [bacinger/f1-circuits](https://github.com/bacinger/f1-circuits), © 2019–2025 Tomislav Bacinger | MIT | Nur privat: Rohdaten in `data/tracks/private/source/` (gitignored), daraus erzeugt `core/tools/trackgen/convert_circuits.py` die privaten Strecken in `data/tracks/private/`. Nicht im öffentlichen Repository, nicht in veröffentlichten Builds (Streckenlayouts und Namen können geschützt sein). |
| Würfel, Ebene, Grundmaterial | Unreal Engine 5.8 (`/Engine/BasicShapes`) | Unreal Engine EULA | Blockout-Darstellung in Unreal. |
