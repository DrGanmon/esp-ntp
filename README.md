# GPS-NTP mit Wemos D1 mini oder WT32-ETH01

ESP8266-Firmware für einen lokalen NTP-Server (UDP 123), der UTC vom u-blox NEO-6M-0-001 bezieht. Kein Internet-Zeitserver erforderlich. Deutsche Statusseite mit Satelliten im Fix, sichtbaren Satelliten, HDOP, Alter der Zeitübernahme, geschätzter Zeitunsicherheit und WLAN-Status.

Zusätzlich wird das **WT32-ETH01 (ESP32 mit LAN8720-Ethernet)** unterstützt. Beide Boards verwenden dieselben Webfunktionen einschließlich Captive Portal, NTP-Kalibrierung, Satellitenansicht und Web-OTA. Ethernet hat beim WT32 Vorrang vor einer vorhandenen WLAN-Verbindung.

## Lizenz

Der eigene Projektcode steht wie bei ESPEasy unter **GNU GPL Version 3 oder
jeder späteren Version (`GPL-3.0-or-later`)**. Kommerzielle Nutzung ist erlaubt;
bei Weitergabe gelten die Copyleft- und Quellcodepflichten der GPL.

Es gelten [LICENSE](LICENSE) und der vollständige GPLv3-Text in
[COPYING](COPYING). Fremdkomponenten behalten ihre eigenen Lizenzen.
Hinweise zu Abhängigkeiten und Firmware-Weitergabe stehen in
[THIRD_PARTY.md](THIRD_PARTY.md). Eine eigene SDK-Linking-Ausnahme wird nicht erteilt.

Die Projektlizenztexte werden den GitHub-Build-Artefakten und Releases beigelegt.
Sie ersetzen kein vollständiges Paket der erforderlichen Fremdlizenzen und Quellen.

## WT32-ETH01

| GPS | WT32-ETH01 |
| --- | --- |
| TX | GPIO14 (Hardware-UART2 RX) |
| GND | GND |
| PPS / TIMEPULSE | optional GPIO33 / `485_EN` |
| RX | nicht benötigt |
| VCC | gemäß GPS-Trägerplatine |

Nur 3,3-V-GPIO-Signale verwenden. GPIO33 darf nicht gleichzeitig für andere Funktionen verwendet werden. Ethernet-Pins einschließlich GPIO0 (50-MHz-Eingang), GPIO16 (PHY-Power), GPIO18 (MDIO) und GPIO23 (MDC) bleiben dem Board vorbehalten. Der PHY wird mit Adresse 1 als LAN8720 initialisiert. Grundlage ist die [WT32-ETH01-Variante des ESP32 Arduino Core](https://github.com/espressif/arduino-esp32/blob/2.0.17/variants/wt32-eth01/pins_arduino.h).

```sh
pio run -e wt32_eth01
pio run -e wt32_eth01 -t upload
pio device monitor -e wt32_eth01
```

Das WT32 benötigt für den ersten Upload einen externen USB-UART-Adapter mit 3,3-V-Logik und eine geeignete Versorgung. TX0/RX0 gekreuzt anschließen, gemeinsame Masse verwenden und GPIO0 für den seriellen Download beim Reset auf GND legen. Anschließend GPIO0 wieder freigeben und neu starten; im Normalbetrieb wird dieser Anschluss für Ethernet benötigt. Die konkrete Boardrevision und deren Anschlussbezeichnungen prüfen.

Im Normalbetrieb Netzwerkkabel anschließen: standardmäßig wird die Ethernet-IP per DHCP bezogen. Die IP steht im seriellen Monitor und auf der Statusseite. Beim Start wird für die Wiederherstellung zusätzlich der WLAN-AP geöffnet. Besteht eine LAN- oder WLAN-Verbindung, schließt er nach ungefähr 60 Sekunden. Ohne Verbindung bleibt das Setup erreichbar. Über die AP-IP bleibt das WLAN-Setup sichtbar; dort führt **Ethernet / IP-Konfiguration** zu den Netzwerkeinstellungen. WLAN-Zugangsdaten sind für den Ethernet-Betrieb nicht erforderlich.

### Nutzung beider ESP32-Kerne

Auf **Core 1** läuft ein eigener Task mit höherer Priorität für GPS-Auswertung, PPS-Zuordnung und NTP-Antworten. **Core 0** verarbeitet Weboberfläche, Konfiguration und NTP-Upstream-Abfragen. DNS läuft auf Core 0 in einem separaten Worker und blockiert die GPS-/NTP-Verarbeitung nicht. Gemeinsame Zeitdaten werden über einen rekursiven Mutex geschützt; PPS-ISR-Daten über einen kurzen Spinlock. Webantworten werden nach dem Freigeben des Zeitdaten-Mutex gesendet.

Der Hardware-UART verwendet einen 2048-Byte-Empfangspuffer und einen niedrigen FIFO-Schwellwert, damit NMEA-Sätze zeitnah verarbeitet werden. Beide Kerne werden für getrennte Aufgaben genutzt; sie berechnen keine konkurrierenden Zeitwerte. Ethernet und die Aufteilung können Latenzschwankungen reduzieren, garantieren aber keine konkrete Genauigkeit. Flash-Schreibvorgänge und OTA können weiterhin beide Kerne bzw. Interrupts beeinflussen. Die tatsächliche Genauigkeit muss unter Last mit Hardware gemessen werden.

## DHCP oder statische IP

Auf Status- und Setup-Seite führt **IP-Konfiguration** bzw. **Ethernet / IP-Konfiguration** zu `/network`. Dort zwischen DHCP und statischer IPv4 wählen. Bei statischer Konfiguration IP-Adresse, Subnetzmaske, Gateway und DNS-Server eintragen. Zum Speichern ist das aktuelle OTA-Passwort erforderlich (Standard `gpsntp-update`). Das Gerät speichert die Einstellungen dauerhaft und startet neu.

Die Einstellung gilt beim **WT32 für Ethernet**, beim **D1 mini für WLAN**. Das optionale WLAN am WT32 bleibt bei DHCP, damit Ethernet und WLAN nicht dieselbe feste IP erhalten. Ungültige Masken, Netz-/Broadcastadressen, ein Gateway außerhalb des Subnetzes und eine Überschneidung mit dem Setup-Netz `192.168.4.0/24` werden abgelehnt. Der Server prüft nicht, ob eine gewählte Adresse bereits von einem anderen Gerät verwendet wird.

Bei falscher statischer IP: Gerät neu starten, innerhalb des Wiederherstellungsfensters mit `GPS-NTP-…` verbinden und `http://192.168.4.1/network` öffnen. Dort die Adresse korrigieren oder DHCP wählen. Bei statischer Konfiguration wird der AP auf beiden Boards bei jedem Neustart geöffnet und bleibt bei bestehender Verbindung mindestens ungefähr 60 Sekunden verfügbar. Ein WLAN-Reset löscht ausschließlich WLAN-Zugangsdaten; die IP-Konfiguration bleibt gespeichert.

## Verdrahtung D1 mini

| GPS | D1 mini |
| --- | --- |
| TX | D5 / GPIO14 |
| GND | GND |
| VCC | gemäß der Versorgungsspezifikation des konkreten GPS-Breakouts |
| RX | nicht benötigt |
| PPS / TIMEPULSE | optional D6 / GPIO12 (3,3-V-Signal) |

GPIO am ESP8266 vertragen nur 3,3 V. Das nackte NEO-6M-Modul benötigt 2,7–3,6 V; manche Breakout-Platinen haben einen Regler und erlauben eine andere Eingangsspannung. Vor Anschluss die eigene Platine prüfen. Gemeinsame Masse verwenden. GPS-Antenne mit freier Sicht zum Himmel platzieren. UART-Vorgabe: 9600 Baud, 8N1, NMEA RMC/GGA/GSV aktiviert. Bereits umkonfigurierte Module gegebenenfalls zurücksetzen.

## Bauen und starten

PlatformIO installieren, dann im Projektverzeichnis:

```sh
pio run -e d1_mini
pio run -e d1_mini -t upload
pio device monitor -e d1_mini
```

1. Ohne gespeicherte Zugangsdaten öffnet der ESP sofort den AP `GPS-NTP-<Chip-ID>` mit Passwort `gpssetup8266`. Mit gespeicherten Daten startet der AP nach 20 Sekunden ohne WLAN-Verbindung.
2. Mit diesem WLAN verbinden. Das Captive Portal öffnet die eigenständige **WLAN-Setup-Seite**; alternativ **http://192.168.4.1/** oder **http://192.168.4.1/setup** öffnen. HTTPS-Aufrufe lassen sich nicht transparent umleiten.
3. Verfügbare Netzwerke werden automatisch im Hintergrund gesucht und mit Signalstärke und Verschlüsselungsstatus angezeigt. Netzwerk auswählen oder die SSID eines versteckten Netzes manuell eingeben. Passwort eingeben und **Speichern und verbinden** drücken. **Erneut suchen** aktualisiert die Liste. Die Zugangsdaten bleiben im Flash gespeichert. Bei falschen Daten bleibt das Setup erreichbar und zeigt einen Fehler; alle 30 Sekunden versucht der ESP die Verbindung erneut.
4. Nach erfolgreicher Verbindung zeigt das Setup die WLAN-IP und einen Link zur Statusseite. Das Captive Portal und Zugriffe über `192.168.4.1` bleiben immer bei der WLAN-Einrichtung, auch wenn der ESP bereits verbunden ist. Mit dem eigenen WLAN verbinden und die angezeigte IP oder **http://gps-ntp.local/** öffnen, sofern das Endgerät mDNS unterstützt. Dort erscheinen GPS-/NTP-Status, Notfall-NTP und WLAN-Reset. Der AP bleibt noch ungefähr 60 Sekunden ab erfolgreicher Verbindung offen. Bei Zugriff über die WLAN-IP öffnet die Startseite direkt die Statusoberfläche.
5. Auf NTP-Clients die WLAN-IP als Zeitserver eintragen. Eine DHCP-Reservierung im Router sorgt für eine feste Adresse. Beispiel für chrony: `server 192.168.1.50 iburst` (durch tatsächliche IP ersetzen).

Bei WLAN-Ausfall wird das Portal erneut geöffnet. Über **WLAN löschen und neu starten** in der Weboberfläche lassen sich die gespeicherten WLAN-Zugangsdaten nach einer Bestätigung löschen. Der ESP startet anschließend neu und öffnet sofort den Einrichtungs-AP. Die Notfall-NTP-Einstellung bleibt erhalten. Das Setup-Passwort ist öffentlich dokumentiert; bei Bedarf in `src/main.cpp` ändern. Zugangsdaten werden lokal per HTTP übertragen; Einrichtung nur in einer vertrauenswürdigen Umgebung durchführen. UDP 123 nicht ins Internet weiterleiten.

## Wenn kein Einrichtungs-WLAN sichtbar ist

`Leaving... Hard resetting via RTS pin...` ist die Ausgabe des Upload-Werkzeugs. Der danach geöffnete Monitor allein bestätigt noch keinen erfolgreichen Firmware-Start. Die Firmware schreibt beim Start `[BOOT]`-Meldungen, beim AP-Start dessen Namen und alle zehn Sekunden eine `[STATUS]`-Zeile. DTR/RTS sind für den Monitor deaktiviert, damit dessen Steuerleitungen das Board nicht im Reset halten.

Nach dem Upload den Monitor mit 115200 Baud öffnen und einmal die Reset-Taste am D1 mini drücken. Nach spätestens 20 Sekunden sollte der AP sichtbar sein, wenn keine WLAN-Verbindung besteht. Falls der ESP bereits verbunden ist, zeigt das Terminal stattdessen die WLAN-IP. Ohne GPS-Modul muss die WLAN-Einrichtung ebenfalls funktionieren. Bleibt der Monitor völlig leer, Port und USB-Verbindung prüfen und testweise das GPS-Modul bei ausgeschalteter Versorgung abziehen. Das Einrichtungs-WLAN nutzt 2,4 GHz; die Setup-Adresse lautet `http://192.168.4.1/`.

## Optionales PPS-Signal

Der NEO-6M besitzt an Modul-Pin 3 den Ausgang TIMEPULSE. Falls dieser an deiner Trägerplatine zugänglich ist, zusätzlich beim **D1 mini mit D6 / GPIO12**, beim **WT32 mit GPIO33 / 485_EN** verbinden. GPS-TX bleibt beim D1 mini an D5 / GPIO14 und beim WT32 an GPIO14, die Masse muss gemeinsam sein. Nur ein 3,3-V-kompatibles Signal verwenden. Ohne PPS-Leitung bleibt der jeweilige Eingang frei und die bisherige NMEA-/NTP-Zeitübernahme funktioniert weiter. Die optionale Erkennung ist standardmäßig aktiv; mit `-D GPS_PPS_ENABLED=0` in den PlatformIO-Build-Flags lässt sie sich vollständig abschalten.

Vorausgesetzt wird die Werkskonfiguration: **1 Hz, steigende Flanke auf der UTC-Sekundengrenze, nachfolgender RMC-Satz für dieselbe Sekunde**. Anders konfigurierte Frequenz, Polarität oder eine um ganze Sekunden verschobene Ausgabe müssen zuerst korrigiert werden. Die Firmware ordnet einen zeitnah verarbeiteten ganzsekündigen RMC-Satz dem vorausgehenden Puls zu. Der Satzanfang muss 10–800 ms nach dem Puls liegen. Drei aufeinanderfolgende Zuordnungen, Pulsabstände von 1 s ±2 ms und eine stabile RMC-Verzögerung innerhalb 20 ms sind für die Aktivierung nötig. Regelmäßige Pulse allein beweisen keine richtige UTC-Zuordnung.

Die Statusseite zeigt **PPS (optional)** und bei erfolgreicher Zuordnung **GPS + PPS** als Zeitquelle. PPS hat Vorrang vor der NTP-Kalibrierung; die serielle Übertragungsverzögerung geht dann nicht in den Zeitanker ein. NTP dient weiterhin als Plausibilitätskontrolle. Widerspricht eine ausreichend genaue NTP-Antwort der PPS-Zeit um mehr als 0,5 s, wird die Zuordnung verworfen und PPS für 60 Sekunden gesperrt. Ohne unabhängige Referenz kann eine ganze Sekunde falsche Zuordnung nicht sicher ausgeschlossen werden.

### Hardware-Capture und Frequenzregelung

Beim WT32 erfasst **MCPWM0 Capture-Kanal 0** die steigende PPS-Flanke direkt in Hardware. **Kanal 1** ist für Software-Ablesungen desselben frei laufenden 80-MHz-Capture-Timers reserviert. GPS-Zeitanker, NTP-Vergleiche und NTP-Paketzeitstempel verwenden diese gemeinsame Zeitbasis. Der Interrupt übergibt nur den bereits gespeicherten Flankenwert; eine verzögerte Ausführung verschiebt dadurch nicht die gespeicherte Flanke. Der 32-Bit-Hardwarezähler wird regelmäßig in Software erweitert, bevor er nach ungefähr 53,7 Sekunden überläuft. Deep Sleep und Änderungen der Capture-Konfiguration während des Betriebs werden nicht unterstützt. Bei aktivierter dynamischer Energieverwaltung wird die maximale APB-Frequenz gesperrt. Die beiden Capture-Kanäle dürfen nicht parallel von anderen Funktionen verwendet werden.

Falls Hardware-Capture nicht initialisiert werden kann, verwendet das WT32 automatisch wieder den GPIO-Interrupt mit `micros()`. Der D1 mini verwendet grundsätzlich diesen Interruptpfad. **PPS-Zeiterfassung** zeigt die tatsächlich verwendete Methode an.

Nach bestätigter PPS-/UTC-Zuordnung lernt die Firmware den lokalen Gangfehler über **16 aufeinanderfolgende Einsekundenintervalle**. Ein Intervall muss innerhalb ±500 µs um eine Sekunde liegen; die Differenz zwischen längstem und kürzestem Intervall darf im Lernfenster höchstens 100 µs betragen. Das entspricht nach der anfänglichen PPS-Zuordnung etwa weiteren 16 Sekunden Lernzeit. Folgende Fenster aktualisieren den geschätzten Fehler mit einem Viertel ihres Unterschieds zum bisherigen Wert. Positive ppm bedeuten: Der lokale Timer läuft zu schnell; die verstrichene lokale Zeit wird bei der NTP-Berechnung entsprechend verkürzt.

Die Frequenzkorrektur wird nur für aktive PPS-Zeit angewendet und bei fehlender UTC-Zuordnung, unplausiblen Intervallen, großen Schwankungen oder nach spätestens 60 Sekunden ohne neues gültiges Lernfenster verworfen. Sie ersetzt keine sichere Zuordnung des Pulses zur UTC-Sekunde und setzt nach PPS-Ausfall die Uhr nicht eigenständig fort. **Frequenzkorrektur** und **PPS-Intervallstreuung** zeigen Lernzustand, ppm und gemessene Intervallstreuung; diese Streuung ist keine absolute UTC-Fehlermessung.

### NTP-Zeitstempel im Netzwerkstack

Auf dem WT32 verwendet der NTP-Server einen eigenen lwIP-UDP-Empfangspfad. Der Empfangszeitpunkt wird beim Aufruf des UDP-Empfangs-Callbacks erfasst, bevor das Paket zur Bearbeitung an den Zeit-Task übergeben wird. Eine feste Warteschlange fasst zwölf Anfragen. Für die Antwort wird ein unveränderlicher Zeitanker einschließlich Frequenzkorrektur an den lwIP-Sendepfad übergeben; der Sendezeitstempel wird dort nach der Pufferreservierung unmittelbar vor der UDP-Übergabe gesetzt. Die lwIP-Callbacks nehmen keinen Anwendungs-Zeitdaten-Mutex, um gegenseitiges Blockieren der Kerne zu vermeiden. Um alte Antworten zu vermeiden, werden Anfragen und Sendeaufträge mit mehr als 20 ms interner Wartezeit verworfen.

Der D1 mini bleibt bei der Arduino-UDP-API: Der Empfang wird innerhalb des Abholaufrufs angenähert, der Sendezeitstempel nach Vorbereitung der UDP-Verbindung gesetzt. Die gemeinsame Zeitrechnung berücksichtigt auch Anfragen, deren Empfang vor dem jüngsten PPS-Zeitanker liegt, sowie positive und negative Frequenzfehler und Timerüberläufe.

**NTP-Zeitstempel** zeigt den Pfad an; **NTP verworfen / Sendefehler** zählt beim WT32 Warteschlangenüberläufe, veraltete Aufträge, Pufferfehler und fehlgeschlagene Sendeübergaben. Der normale Antwortzähler zählt beim WT32 erfolgreich eingereihte Antworten, nicht nachweislich beim Client eingetroffene Pakete. Diese Softwarezeitstempel liegen näher am Netzwerkstack, sind aber keine Hardware-Zeitstempel am Ethernet-MAC/PHY. Ethernet-Warteschlangen, Übertragungsdauer und der Netzwerkweg bleiben zusätzliche Fehlerquellen.

Die bisherige PPS-Unsicherheitsannahme von 2 ms wird trotz dieser Verbesserungen beibehalten, bis unabhängige Messungen vorliegen. Ein kleinerer angezeigter Zahlenwert allein wäre keine Genauigkeitsverbesserung.

Fehlen passende RMC-Sätze, fallen Pulse aus oder wird die Impulsfolge unplausibel, erfolgt automatisch der Rückfall auf NMEA mit gegebenenfalls vorhandener NTP-Kalibrierung bzw. bei GPS-Ausfall auf Notfall-NTP. Spätestens 1,5 Sekunden nach dem letzten zugeordneten Puls wird PPS nicht mehr als aktive Zeitquelle verwendet. Es werden keine Sekunden allein aus unbestätigten Pulsen fortgeschrieben. Für PPS werden vorläufig **2 ms plus 100 ppm seit der letzten Übernahme** als technische Unsicherheit angesetzt. Das ist keine gemessene Genauigkeit; WLAN- und Interruptlatenz bleiben zu prüfen. Die Firmware stellt die PPS-Konfiguration des GPS-Moduls nicht selbst um.

## Position und Satellitenhimmel

Die normale Statusseite zeigt Breitengrad und Längengrad in WGS84 mit sechs Nachkommastellen, die Höhe über dem mittleren Meeresspiegel und das Alter des letzten Positionswerts. Position und Höhe werden nach fünf Sekunden ohne aktuelle Daten ausgeblendet. Viele Nachkommastellen bedeuten keine entsprechend hohe Ortsgenauigkeit; Empfangsbedingungen und Satellitengeometrie begrenzen die Genauigkeit. HDOP bleibt als Maß für die Positionsgeometrie sichtbar.

Das Himmelsdiagramm zeigt die relativen Satellitenrichtungen vom Empfänger aus: Norden oben, Osten rechts, Horizont am Außenkreis und Zenit in der Mitte. Die Tabelle enthält PRN-Kennung, Azimut (0–359°), Elevation (0–90°) und den gemeldeten Signalwert C/N₀ in dB-Hz. Punkte mit positivem Signalwert sind grün, ohne Signalwert oder mit Wert null grau. Ein gemeldeter Signalwert bedeutet nicht automatisch, dass der Satellit für den Positionsfix verwendet wird. Satelliten ohne Winkelangaben erscheinen nur in der Tabelle. Entfernungen und absolute Umlaufpositionen werden nicht angezeigt.

Die Firmware wertet die standardmäßig ausgegebenen `GPGSV`-Nachrichten des NEO-6M aus; alternativ wird auch `GNGSV` unterstützt. Nur vollständige, geordnete Nachrichtenfolgen mit gültigen Prüfsummen werden übernommen (bis zu 40 Satelliten). Die letzte vollständige Folge wird angezeigt; getrennte Konstellations-/Signalfolgen anderer Empfänger werden nicht zusammengeführt. Nach zehn Sekunden ohne vollständige Folge werden Diagramm und Tabelle geleert. Falls keine Satelliten erscheinen, prüfen, ob GSV-Ausgabe am GPS-Modul aktiviert ist. Die Ansicht benötigt keine externen Karten, Skripte oder Internetverbindung und aktualisiert sich alle zwei Sekunden.

## Notfall-NTP und GPS-Kalibrierung

In der Weboberfläche unter **Notfall-NTP** einen Vorschlag auswählen oder einen eigenen Hostnamen bzw. eine IPv4-Adresse eintragen. Keine URL und keinen Port angeben. **Notfall-NTP speichern** speichert die Auswahl dauerhaft im EEPROM-Bereich des Flashs. Ein leeres Feld deaktiviert den Ersatzserver; im Auslieferungszustand ist er deaktiviert. Die Vorschläge entsprechen den Zonen des [NTP Pool Project](https://www.ntppool.org/en/use.html).

Der ESP fragt den eingestellten Server alle 30 Sekunden ab, auch bei GPS-Empfang. Bei gültigem GPS ohne PPS dient NTP zur Kalibrierung der seriellen Zeit; mit PPS als Plausibilitätsvergleich. Sobald seit fünf Sekunden keine neue gültige GPS-Zeit übernommen wurde (oder beim Start noch keine vorliegt), wird NTP direkt als Ersatzzeit verwendet. UDP-Antworten werden ohne Warteschleife verarbeitet. Beim ESP8266 kann DNS pro Versuch bis zu einer Sekunde blockieren; beim ESP32 läuft DNS im separaten Worker, dessen interner Timeout bis etwa 15 Sekunden dauern kann. Ein externer Server benötigt Internetzugang, ein lokaler Server nur Erreichbarkeit im LAN.

Die Statusseite zeigt aktive Zeitquelle, Stratum, Ersatzserver, Abfragestatus, GPS-Korrektur, Netzlaufzeit und Streuung der Vergleichswerte. Bei Ersatzzeit meldet der ESP das Stratum des vorgelagerten Servers plus eins; Stratum 0 (einschließlich Kiss-o'-Death), 15 und 16 werden nicht übernommen. Absender, Port, Anfragekennung, Version, Modus, Synchronisationsstatus, Datum und Root-Distanz werden geprüft. Diese Prüfungen sind keine kryptografische Authentifizierung. Kommt GPS zurück, wird wieder GPS-Zeit verwendet.

### Kalibrierung ohne PPS oder DS3231

Die NTP-Berechnung zieht die Verarbeitungsdauer des Servers (`T3 − T2`) von der gesamten Anfrage-/Antwortdauer ab. Aus der verbleibenden Netzlaufzeit wird unter Annahme symmetrischer Wege die Zeit bei Antwortempfang geschätzt. Das entspricht der Laufzeitkorrektur des [NTP-Verfahrens nach RFC 5905](https://www.rfc-editor.org/rfc/rfc5905.html#section-8). Diese Zeit wird mit der noch unkorrigierten GPS-Zeit am selben Empfangszeitpunkt verglichen.

Die Firmware hält bis zu fünf Vergleichsmessungen im RAM. Mindestens drei gültige Messungen sind nötig, bei störungsfreiem Betrieb also etwa eine Minute. Verwendet wird der Median der drei Messungen mit der kürzesten Netzlaufzeit. Eine Messung darf höchstens 100 ms Netzlaufzeit, 100 ms geschätzte NTP-Unsicherheit und einen GPS-Versatz von maximal ±1 Sekunde haben. Die drei ausgewählten Versatzwerte dürfen höchstens 20 ms auseinanderliegen. Andernfalls wird keine neue Kalibrierung aktiviert.

Bei aktiver Kalibrierung lautet die Zeitquelle **GPS + NTP-Kalibrierung**. Der ermittelte Versatz korrigiert jeden neuen GPS-Zeitanker. Stratum und Referenzkennung kennzeichnen dann die Abhängigkeit vom vorgelagerten NTP-Server; es wird kein unabhängiges GPS-Stratum 1 behauptet. Die Unsicherheit enthält die größte NTP-Unsicherheit der drei ausgewählten Vergleiche, deren Streuung, 8 ms UART-Reserve sowie einen Alterungszuschlag von 100 ppm.

Die Kalibrierung läuft ab, sobald die älteste der drei verwendeten Messungen 180 Sekunden alt ist. Sie wird außerdem bei Serverwechsel, Neustart, längerer GPS-Unterbrechung oder einer Änderung der GPS-Phasenlage um mehr als 20 ms verworfen. Danach gilt wieder die konservative GPS-Unsicherheit, bis neue stabile Vergleiche vorliegen. Ein leeres Serverfeld deaktiviert NTP-Ersatzzeit und Kalibrierung.

Damit lässt sich ein stabiler serieller GPS-Versatz ausgleichen. **Eine tatsächliche Genauigkeit von 1 ms wird nicht garantiert.** WLAN-Latenz, asymmetrische Netzwerkwege, ein falscher NTP-Server und wechselnde GPS-Ausgabeverzögerungen können unentdeckt bleiben. Ein genauer, unabhängig synchronisierter Server im lokalen Netz bietet bessere Voraussetzungen als ein entfernter Internetserver. Die angezeigte Unsicherheit ist eine Schätzung; die erreichte Genauigkeit muss gegen eine weitere unabhängige Zeitquelle gemessen werden.

Die NTP-Unsicherheit schätzt sich aus vorgelagerter Root-Dispersion, halber Root-Verzögerung, halber lokaler Paketlaufzeit, 10 ms Reserve und 100 ppm seit der Übernahme. Sie ist keine gemessene Genauigkeit. Nach 60 Sekunden ohne gültige Übernahme von einer der beiden Quellen ist der Server unsynchronisiert. Beim Deaktivieren oder Ändern des Ersatzservers wird eine bisherige Ersatzzeit bis zur nächsten gültigen Übernahme verworfen.

## GitHub-Builds und Versionen

Der Workflow `.github/workflows/firmware.yml` führt bei Pushes, Pull Requests und manuell über **Actions → Firmware bauen und veröffentlichen → Run workflow** die nativen Tests aus und baut beide Boards. Unter den Artefakten des erfolgreichen Laufs steht das Paket **firmware** für 30 Tage zum Download bereit.

Für jede Version einen Tag mit dem Präfix `v` auf den gewünschten Commit setzen und zu GitHub übertragen, beispielsweise nach dem Commit der Änderungen:

```sh
git tag -a v1.0.0 -m "Version 1.0.0"
git push origin v1.0.0
```

Nach erfolgreichen Tests und beiden Builds erstellt der Workflow automatisch einen GitHub Release mit generierten Versionshinweisen und diesen Dateien:

- `esp-ntp-d1-mini.bin`: Anwendungsfirmware für den Wemos D1 mini.
- `esp-ntp-wt32-eth01.bin`: Anwendungsfirmware für den WT32-ETH01.
- `SHA256SUMS`: SHA-256-Prüfsummen beider Dateien; nach dem Download mit `sha256sum --check SHA256SUMS` prüfen.

Tags mit Bindestrich, etwa `v1.0.0-rc1`, erzeugen einen Vorabrelease. Ein erneut gestarteter Workflow aktualisiert die Dateien eines bereits vorhandenen Releases. Die Release-Dateien bleiben unabhängig von der 30-tägigen Artefaktaufbewahrung verfügbar. GitHub Actions muss im Repository aktiviert sein; der Release-Job verwendet das automatische `GITHUB_TOKEN` mit Schreibrecht für Repository-Inhalte, zusätzliche Secrets sind nicht nötig.

Die `.bin` für das eigene Board kann direkt über die OTA-Webseite installiert werden. Die Dateien enthalten nur die Anwendung; für die erste serielle Installation einschließlich Bootloader und Partitionstabelle weiterhin PlatformIO verwenden.

## Firmware-Update im Browser (OTA)

Diese Version einmal seriell installieren. Danach können weitere Firmware-Versionen über **Firmware aktualisieren** auf der normalen Statusseite hochgeladen werden. Die Funktion ist über die LAN-/WLAN-IP bzw. `http://gps-ntp.local/` erreichbar; über den Einrichtungs-AP werden keine Updates angenommen.

1. Das feste **OTA-Standardpasswort lautet `gpsntp-update`**. Unter **OTA-Passwort ändern** auf der Statusseite kann es mit dem aktuellen Passwort geändert werden. Das neue Passwort zweimal eingeben (8–64 druckbare ASCII-Zeichen ohne Leerzeichen) und speichern. Die Änderung gilt sofort und bleibt nach Neustarts, OTA-Updates und WLAN-Reset erhalten. Das Passwort wird weder über die Status-API noch im seriellen Monitor ausgegeben. Ein vollständiges Löschen des Flashs stellt den Standard wieder her. Beim erstmaligen Wechsel von der früheren Firmware mit zufälligem Passwort wird dieses einmal durch `gpsntp-update` ersetzt; anschließend bleiben selbst gesetzte Passwörter erhalten. Wird diese Version noch mit der früheren OTA-Firmware hochgeladen, gilt für diesen einen Upload zunächst deren bisheriges Passwort.
2. Die neue Firmware für das tatsächliche Board bauen: `pio run -e d1_mini` oder `pio run -e wt32_eth01`. Die Upload-Datei liegt unter **`.pio/build/d1_mini/firmware.bin`** bzw. **`.pio/build/wt32_eth01/firmware.bin`**. Niemals Firmware zwischen den Boards austauschen. Keine `.elf`-, Dateisystem-, Bootloader- oder Partitionierungsdatei hochladen. Beim WT32 wird eine OTA-fähige Partitionstabelle verwendet; der erste serielle Upload muss deshalb auch Bootloader und Partitionstabelle installieren.
3. In der Statusseite die `.bin`-Datei auswählen, das OTA-Passwort eingeben und **Firmware hochladen und neu starten** bestätigen.
4. Die Fortschrittsanzeige zeigt die Übertragung. Erst nach vollständig empfangener Datei und erfolgreicher Prüfung durch den ESP8266-Updater wird das Update für den nächsten Start aktiviert. Dateigröße, verfügbarer Speicher und ESP-Firmware-Kopf werden geprüft; dies ist keine Signaturprüfung und keine Garantie, dass die hochgeladene Anwendung funktioniert. Nur eigene, vertrauenswürdige Builds verwenden.
5. Nach der Erfolgsmeldung startet der ESP automatisch neu. Kurz warten und die Seite neu laden. WLAN-Zugangsdaten, Notfall-NTP und OTA-Passwort bleiben bei Updates mit dieser Projektkonfiguration erhalten.

Ein abgebrochener oder abgelehnter Upload löst keinen Neustart aus. Bei einem Verbindungsabbruch ohne Erfolgsmeldung zunächst die Seite neu laden und den Gerätestatus prüfen. Während des Uploads können GPS-Verarbeitung und NTP-Antworten pausieren. Das Gerät verwendet HTTP; OTA daher nur im vertrauenswürdigen LAN nutzen und keine Portweiterleitung für die Weboberfläche einrichten. Falls das Gerät nach einem USB-Upload einen Bootstrap-Fehler für OTA meldet, einmal die Reset-Taste drücken oder die Versorgung kurz trennen und erneut versuchen.

## Zeitqualität und Grenzen

Ohne angeschlossenes und zugeordnetes PPS verwendet diese Version **serielle NMEA-Zeit**. Die Firmware kompensiert die berechenbare Übertragungszeit des gültigen RMC-Satzes: bei 9600 Baud und 8N1 dauert jedes Zeichen ungefähr 1,042 ms. Der Zeitanker wird um die Übertragungsdauer vom `$` bis zum abschließenden `CR` zurückgerechnet. Die tatsächliche Korrektur steht auf der Statusseite unter **UART-Zeitkorrektur**. Bei GPS + PPS bestimmt stattdessen die Impulsflanke den Zeitanker.

Nur zeitnah verarbeitete RMC-Sätze werden als Zeitreferenz übernommen. Pausen von mehr als 10 ms in der GPS-Verarbeitung, Pufferüberläufe oder eine um mehr als 8 ms von der erwarteten Übertragungsdauer abweichende Satzaufnahme verwerfen den Satz für die Zeitübernahme. Satelliten- und Positionsdaten werden weiterhin ausgewertet. Die Statusseite zählt verworfene Sätze und Pufferüberläufe. Bei langen HTTP-/OTA-Vorgängen kann dieser Zähler steigen; mit dem nächsten rechtzeitig empfangenen RMC-Satz setzt die Zeitübernahme wieder ein.

NTP-Anfragen werden vor und nach HTTP-Verarbeitung bedient. WLAN-Schlafmodus ist ausgeschaltet, um zusätzliche Empfangslatenz zu vermeiden; dadurch kann der Stromverbrauch steigen. Diese Änderungen benötigen weder einen DS3231 noch eine zusätzliche GPS-Leitung.

Ohne aktive NTP-Kalibrierung bleiben die interne Verzögerung des GPS-Moduls und die Reihenfolge seiner ausgegebenen Sätze unbekannt. Die UART-Korrektur entfernt nur einen berechenbaren Anteil des Versatzes. Ohne aktive NTP-Kalibrierung werden als technische Planungsannahme **1000 ms plus 100 ppm seit der letzten Übernahme** angezeigt und in der NTP-Root-Dispersion gemeldet. Das ist weder eine Messung noch eine garantierte Fehlergrenze. Die tatsächliche Verbesserung muss gegen eine unabhängige Referenz gemessen werden. HDOP beschreibt die Positionsgeometrie und ist kein Maß für die Zeitgenauigkeit.

Die Firmware übernimmt GPS-Zeit nur mit aktueller gültiger GPS-Position und aktuellem Datum/Zeit. Nach 60 Sekunden ohne gültige Übernahme von GPS oder Notfall-NTP antwortet NTP mit Leap Indicator 3 und Stratum 16 (unsynchronisiert). Bei GPS-Zeit ohne NTP-Kalibrierung meldet der direkt GPS-gebundene Server Stratum 1; das bedeutet hier keine Mikrosekundengenauigkeit. Kalibrierte GPS-Zeit übernimmt die NTP-Abhängigkeit wie oben beschrieben. UTC wird unverändert ausgegeben, Zeitzone und Sommerzeit sind Aufgabe der Clients.

Für höhere GPS-Genauigkeit unabhängig von der seriellen Ausgabeverzögerung unterstützt die Firmware optional TIMEPULSE/PPS wie oben beschrieben. Bei Platinen mit ausschließlich TX/RX/VCC/GND ist dafür eine zusätzliche Verbindung zum Modul nötig. Die Zeitimpuls-Spezifikation des GPS-Chips ist nicht die erreichbare Genauigkeit eines WLAN-NTP-Clients. Bei alten GPS-Empfängern mögliche Datumsprobleme durch Firmware/GPS-Wochen-Rollover mit einer unabhängigen Referenz prüfen.

## Prüfung mit Hardware

- WT32 mit Ethernet und ohne WLAN-Zugangsdaten starten: DHCP-IP, Weboberfläche und NTP auf der LAN-IP prüfen. Kabel abziehen und wieder verbinden; Wiederherstellung des AP bzw. Ethernet-Betriebs prüfen. Optional gespeichertes WLAN als Reserve testen.
- Statische IP speichern, neu starten und alle vier gespeicherten Werte prüfen. Danach zurück auf DHCP wechseln. Eine falsche Adresse über den AP innerhalb des Wiederherstellungsfensters korrigieren. Änderungen müssen OTA-Passwort und Notfall-NTP beibehalten.
- Auf dem WT32 unter wiederholten Webzugriffen GPS-/PPS-Zustand, NTP-Abweichung und Pufferüberläufe prüfen. Ebenso OTA auf jedem Board separat testen; keine konkrete Verbesserung allein aus der Kernaufteilung ableiten.

- Erststart ohne gespeichertes WLAN: AP, DNS-Umleitung und WLAN-Speicherung prüfen.
- Setup zeigt ausschließlich WLAN-Einrichtung: Netzsuche, erneute Suche, Auswahl inklusive Sonderzeichen in SSIDs, manuelle Eingabe und Passwortanzeige prüfen. Nach erfolgreicher Anmeldung muss die Statusseite über die WLAN-IP erreichbar sein; über den AP bleibt auch `/dashboard` beim Setup.
- Während der 60 Sekunden nach erfolgreicher WLAN-Anmeldung über den AP `/`, `/dashboard` und eine Captive-Portal-Prüfadresse öffnen: alle müssen weiterhin das Setup zeigen bzw. dorthin umleiten. Gleichzeitig muss über die WLAN-IP die normale Statusoberfläche erreichbar sein.
- Falsches Passwort und WLAN-Ausfall: Portal bleibt bzw. wird erneut erreichbar.
- Unter freiem Himmel Satelliten und plausibles UTC-Datum prüfen.
- Optional PPS mit D6/GPIO12 (D1 mini) bzw. GPIO33 (WT32) verbinden: nach drei stabilen Zuordnungen muss GPS + PPS erscheinen. PPS abziehen oder GPS-TX trennen: automatische Rückkehr zur bisherigen Zeitquelle prüfen. Korrekte UTC-Sekunde gegen eine unabhängige Referenz kontrollieren, insbesondere nach Änderungen der GPS-Konfiguration.
- Auf dem WT32 muss PPS-Zeiterfassung MCPWM-Hardware melden; nach etwa 16 weiteren stabilen Intervallen muss eine Frequenzkorrektur in ppm erscheinen. Lauf über mehrere Minuten prüfen, damit Capture-Zählerüberläufe erfasst werden. PPS trennen: die Frequenzkorrektur darf nach dem Rückfall nicht weiter angewendet werden.
- NTP-Offset, Streuung und maximale Ausreißer gegen eine unabhängige Referenz vor/nach dem Update vergleichen, sowohl ohne Last als auch mit wiederholten HTTP-Zugriffen. Beim WT32 Zähler für verworfene NTP-Aufträge unter Last kontrollieren. OTA separat prüfen; Flash-Schreiben kann weiterhin die Zeitverarbeitung beeinflussen.
- Position und Höhe gegen einen bekannten Standort vergleichen. Im Satellitenhimmel müssen 0° Azimut oben und 90° rechts liegen; Satelliten bei 90° Elevation erscheinen in der Mitte. GPS-TX trennen: Position muss nach fünf Sekunden verschwinden, die Himmelsansicht nach zehn Sekunden.
- Mit einem NTP-Client Synchronisationsstatus und Abweichung gegen eine unabhängige Quelle messen.
- Bei laufendem GPS einen genauen NTP-Server eintragen: nach mindestens drei stabilen Vergleichen muss die Quelle auf GPS + NTP-Kalibrierung wechseln. Korrektur, Laufzeit, Streuung und NTP-Stratum prüfen. Den NTP-Server anschließend unerreichbar machen: spätestens nach Ablauf der Vergleichswerte muss die konservative GPS-Unsicherheit zurückkehren. Serverfeld leeren: Kalibrierung muss sofort verschwinden.
- UART-Zeitkorrektur prüfen und die NTP-Abweichung gegen dieselbe unabhängige Referenz vor/nach dem Update vergleichen. Beim Laden der Webseite oder einer Netzsuche auf den Zähler verworfener GPS-Sätze achten; anschließend muss die Zeitübernahme wieder einsetzen.
- GPS-TX trennen, Notfall-NTP deaktiviert: nach spätestens 60 Sekunden muss NTP unsynchronisiert melden; Satellitenwerte werden nach 5 Sekunden ausgeblendet.
- Erreichbaren Ersatzserver eintragen, GPS-TX trennen: nach ungefähr 5 Sekunden muss die Zeitquelle auf Notfall-NTP wechseln. GPS wieder anschließen: Rückkehr zu GPS prüfen.
- Ersatzserver speichern und ESP neu starten: gespeicherte Auswahl muss erhalten bleiben. Ungültigen bzw. unerreichbaren Server testen: keine Zeitübernahme, nach Ablauf der letzten gültigen Zeit unsynchronisiert.
- WLAN-Reset bestätigen: gespeichertes WLAN darf nach dem Neustart nicht mehr verbunden werden; Einrichtung über AP muss erneut funktionieren. Abbruch der Bestätigung darf nichts ändern.
- OTA mit korrektem Passwort und eigener Firmware testen: Fortschritt, Erfolgsmeldung, Neustart und Erhalt der Einstellungen prüfen. Falsches Passwort, Textdatei als `.bin`, zu große Datei und abgebrochenen Upload testen: kein erfolgreicher Update-Neustart. OTA-Aufruf über die AP-IP muss abgelehnt werden.
- OTA-Passwort ändern: falsches aktuelles Passwort, unterschiedliche Bestätigungen und unzulässige Längen müssen abgelehnt werden. Nach erfolgreicher Änderung und Neustart muss nur das neue Passwort Updates und weitere Passwortänderungen erlauben. Notfall-NTP muss beim Speichern des Passworts erhalten bleiben und umgekehrt.

Die Verarbeitung von NTP-Antworten lässt sich zusätzlich ohne Hardware prüfen:

```sh
g++ -std=c++11 -Wall -Wextra -Iinclude tests/fallback_test.cpp -o /tmp/esp-ntp-fallback-test
/tmp/esp-ntp-fallback-test
g++ -std=c++11 -Wall -Wextra -Iinclude tests/nmea_timing_test.cpp -o /tmp/esp-ntp-nmea-test
/tmp/esp-ntp-nmea-test
g++ -std=c++11 -Wall -Wextra -Iinclude tests/gps_calibration_test.cpp -o /tmp/esp-ntp-calibration-test
/tmp/esp-ntp-calibration-test
g++ -std=c++11 -Wall -Wextra -Iinclude tests/satellite_view_test.cpp -o /tmp/esp-ntp-satellite-test
/tmp/esp-ntp-satellite-test
g++ -std=c++11 -Wall -Wextra -Iinclude tests/pps_clock_test.cpp -o /tmp/esp-ntp-pps-test
/tmp/esp-ntp-pps-test
g++ -std=c++11 -Wall -Wextra -Iinclude tests/ip_config_test.cpp -o /tmp/esp-ntp-ip-test
/tmp/esp-ntp-ip-test
g++ -std=c++11 -Wall -Wextra -Iinclude tests/precision_clock_test.cpp -o /tmp/esp-ntp-precision-test
/tmp/esp-ntp-precision-test
```

Grundlagen: [u-blox NEO-6 Datenblatt](https://content.u-blox.com/sites/default/files/products/documents/NEO-6_DataSheet_%28GPS.G6-HW-09005%29.pdf), [TinyGPSPlus](https://github.com/mikalhart/TinyGPSPlus), [ESP8266 Arduino Core](https://github.com/esp8266/Arduino).
