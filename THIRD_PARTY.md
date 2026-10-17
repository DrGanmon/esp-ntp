# Fremdkomponenten und Weitergabe

Die Projektlizenz gilt für den eigenen Code. Fremdkomponenten behalten ihre
Lizenzen. Die Projektlizenz erteilt keine zusätzlichen Rechte an Fremdcode.

## Geprüfte Build-Konfiguration

| Komponente | Verwendete Version | Lizenz / Besonderheit |
| --- | --- | --- |
| TinyGPSPlus | 1.1.0 | LGPL-2.1-or-later; Hinweise in `src/TinyGPS++.h` |
| Arduino ESP8266 Core | 3.1.2, PlatformIO espressif8266 4.2.1 | Core überwiegend LGPL; Einzeldateien und Bibliotheken beachten |
| EspSoftwareSerial | 8.0.1, im ESP8266-Framework | LGPL-2.1-or-later |
| ESP8266 NONOS SDK | im genannten Core | Vorcompilierte Bibliotheken; SDK-Lizenz beschränkt Nutzung auf ESP8266 |
| Arduino ESP32 Core | 2.0.17, PlatformIO espressif32 6.10.0 | Core überwiegend LGPL-2.1-or-later; Einzeldateien und Bibliotheken beachten |
| ESP-IDF | 4.4.7, im ESP32-Framework | Überwiegend Apache-2.0; weitere Lizenzen laut Komponentenhinweisen |
| ESP32 Wi-Fi / PHY | im genannten ESP-IDF | Vorcompilierte Bibliotheken unter Apache-2.0; nicht vollständig im Quellcode geliefert |
| lwIP, FreeRTOS, Newlib und weitere Laufzeitkomponenten | aus den Frameworks / Toolchains | Unter anderem BSD, MIT und weitere dateispezifische Bedingungen |
| GCC-Laufzeitbibliotheken | aus den Toolchains | Unter anderem GPL mit GCC Runtime Library Exception; jeweilige Hinweise beachten |

PlatformIO, Compiler und esptool sind Build-Werkzeuge. Ihre Programmlizenzen
werden durch die bloße Ausführung nicht zur Lizenz der erzeugten Anwendung.
Eingebundene Laufzeitbibliotheken sind davon getrennt zu betrachten.

## Firmware weitergeben

- `LICENSE` und `COPYING` zusammen mit den Lizenzhinweisen
  der tatsächlich eingebundenen Fremdkomponenten erhalten und mitgeben.
- Den zur jeweiligen Firmware gehörenden vollständigen Corresponding Source
  nach GPLv3 bereitstellen: einschließlich eigener Quellen, erforderlicher
  offener Abhängigkeiten, Änderungen, Build-Skripte und Konfiguration.
  Ein Link auf einen beweglichen Branch oder nur `platformio.ini` ersetzt das
  nicht automatisch. Für binäre SDK-Komponenten wird durch dieses Projekt
  keine zusätzliche Ausnahme von den GPL-Pflichten erteilt.
- LGPL-Pflichten erfüllen, insbesondere Quellen und Änderungen der betroffenen
  Bibliotheken und gegebenenfalls Materialien zum Wiederverlinken bereitstellen.
- Bei Weitergabe als Benutzerprodukt prüfen, ob GPLv3-Installationsinformationen
  erforderlich sind. Vorhandene Download-, Änderungs- und Installationsrechte
  dürfen nicht durch zusätzliche Bedingungen eingeschränkt werden.

Der GitHub-Workflow legt die Projektlizenztexte den Firmware-Dateien bei. Das
ist noch kein vollständiges Fremdlizenz- oder Quellcodepaket und allein kein
Nachweis, dass sämtliche Weitergabepflichten erfüllt sind. Vor Firmware-
Veröffentlichungen die jeweils tatsächlich verwendeten Pakete prüfen.

## Quellen

- [Arduino ESP8266 Core 3.1.2](https://github.com/esp8266/Arduino/tree/3.1.2)
- [ESP8266 SDK-Lizenz](https://github.com/esp8266/Arduino/blob/3.1.2/tools/sdk/License)
- [Arduino ESP32 Core 2.0.17](https://github.com/espressif/arduino-esp32/tree/2.0.17)
- [ESP-IDF 4.4.7 Lizenzübersicht](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/COPYRIGHT.html)
- [ESP32 Wi-Fi-Lizenz für ESP-IDF 4.4.7](https://github.com/espressif/esp32-wifi-lib/blob/f2aae4d44ec7908013066e69d29b9948846c335c/LICENSE)
- [ESP32 PHY-Lizenz für ESP-IDF 4.4.7](https://github.com/espressif/esp-phy-lib/blob/dcfdccf6cc2fc02d0886624b7998c890d1a19b28/LICENSE)
- [TinyGPSPlus](https://github.com/mikalhart/TinyGPSPlus)

## Einordnung der Lizenzwahl

Das Projekt verwendet wie ESPEasy GPL-3.0-or-later für den eigenen Code und
separate Hinweise auf Fremdlizenzen. Daraus folgt keine rechtliche Bestätigung
für jede Kombination mit vorcompilierten SDK-Bibliotheken. Insbesondere deren
Einordnung und die Quellcodepflichten für die gesamte Firmware bleiben vor
Weitergabe zu prüfen.

Referenz: [ESPEasy-Lizenz](https://github.com/letscontrolit/ESPEasy/blob/680517fa4c8061db7a7a06c1272b8015207766b4/License.txt).
Fremde Copyright-Hinweise wurden nicht als eigene übernommen.
