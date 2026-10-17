# GPS-NTP mit Wemos D1 mini

ESP8266-Firmware für einen lokalen NTP-Server (UDP 123), der UTC vom u-blox NEO-6M-0-001 bezieht. Kein Internet-Zeitserver erforderlich. Deutsche Statusseite mit Satelliten im Fix, sichtbaren Satelliten, HDOP, Alter der Zeitübernahme, geschätzter Zeitunsicherheit und WLAN-Status.

## Verdrahtung

| GPS | D1 mini |
| --- | --- |
| TX | D5 / GPIO14 |
| GND | GND |
| VCC | gemäß der Versorgungsspezifikation des konkreten GPS-Breakouts |
| RX | nicht benötigt |
| PPS / TIMEPULSE | in dieser Version nicht verwendet |

GPIO am ESP8266 vertragen nur 3,3 V. Das nackte NEO-6M-Modul benötigt 2,7–3,6 V; manche Breakout-Platinen haben einen Regler und erlauben eine andere Eingangsspannung. Vor Anschluss die eigene Platine prüfen. Gemeinsame Masse verwenden. GPS-Antenne mit freier Sicht zum Himmel platzieren. UART-Vorgabe: 9600 Baud, 8N1, NMEA RMC/GGA/GSV aktiviert. Bereits umkonfigurierte Module gegebenenfalls zurücksetzen.

## Bauen und starten

PlatformIO installieren, dann im Projektverzeichnis:

```sh
pio run
pio run -t upload
pio device monitor
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

## Position und Satellitenhimmel

Die normale Statusseite zeigt Breitengrad und Längengrad in WGS84 mit sechs Nachkommastellen, die Höhe über dem mittleren Meeresspiegel und das Alter des letzten Positionswerts. Position und Höhe werden nach fünf Sekunden ohne aktuelle Daten ausgeblendet. Viele Nachkommastellen bedeuten keine entsprechend hohe Ortsgenauigkeit; Empfangsbedingungen und Satellitengeometrie begrenzen die Genauigkeit. HDOP bleibt als Maß für die Positionsgeometrie sichtbar.

Das Himmelsdiagramm zeigt die relativen Satellitenrichtungen vom Empfänger aus: Norden oben, Osten rechts, Horizont am Außenkreis und Zenit in der Mitte. Die Tabelle enthält PRN-Kennung, Azimut (0–359°), Elevation (0–90°) und den gemeldeten Signalwert C/N₀ in dB-Hz. Punkte mit positivem Signalwert sind grün, ohne Signalwert oder mit Wert null grau. Ein gemeldeter Signalwert bedeutet nicht automatisch, dass der Satellit für den Positionsfix verwendet wird. Satelliten ohne Winkelangaben erscheinen nur in der Tabelle. Entfernungen und absolute Umlaufpositionen werden nicht angezeigt.

Die Firmware wertet die standardmäßig ausgegebenen `GPGSV`-Nachrichten des NEO-6M aus; alternativ wird auch `GNGSV` unterstützt. Nur vollständige, geordnete Nachrichtenfolgen mit gültigen Prüfsummen werden übernommen (bis zu 40 Satelliten). Die letzte vollständige Folge wird angezeigt; getrennte Konstellations-/Signalfolgen anderer Empfänger werden nicht zusammengeführt. Nach zehn Sekunden ohne vollständige Folge werden Diagramm und Tabelle geleert. Falls keine Satelliten erscheinen, prüfen, ob GSV-Ausgabe am GPS-Modul aktiviert ist. Die Ansicht benötigt keine externen Karten, Skripte oder Internetverbindung und aktualisiert sich alle zwei Sekunden.

## Notfall-NTP und GPS-Kalibrierung

In der Weboberfläche unter **Notfall-NTP** einen Vorschlag auswählen oder einen eigenen Hostnamen bzw. eine IPv4-Adresse eintragen. Keine URL und keinen Port angeben. **Notfall-NTP speichern** speichert die Auswahl dauerhaft im EEPROM-Bereich des Flashs. Ein leeres Feld deaktiviert den Ersatzserver; im Auslieferungszustand ist er deaktiviert. Die Vorschläge entsprechen den Zonen des [NTP Pool Project](https://www.ntppool.org/en/use.html).

Der ESP fragt den eingestellten Server alle 30 Sekunden ab, auch bei GPS-Empfang. Bei gültigem GPS dient NTP zur Kalibrierung der seriellen Zeit. Sobald seit fünf Sekunden keine neue gültige GPS-Zeit übernommen wurde (oder beim Start noch keine vorliegt), wird NTP direkt als Ersatzzeit verwendet. UDP-Antworten werden ohne Warteschleife verarbeitet; die DNS-Auflösung kann pro Versuch bis zu einer Sekunde dauern. Ein externer Server benötigt Internetzugang, ein lokaler Server nur Erreichbarkeit im LAN.

Die Statusseite zeigt aktive Zeitquelle, Stratum, Ersatzserver, Abfragestatus, GPS-Korrektur, Netzlaufzeit und Streuung der Vergleichswerte. Bei Ersatzzeit meldet der ESP das Stratum des vorgelagerten Servers plus eins; Stratum 0 (einschließlich Kiss-o'-Death), 15 und 16 werden nicht übernommen. Absender, Port, Anfragekennung, Version, Modus, Synchronisationsstatus, Datum und Root-Distanz werden geprüft. Diese Prüfungen sind keine kryptografische Authentifizierung. Kommt GPS zurück, wird wieder GPS-Zeit verwendet.

### Kalibrierung ohne PPS oder DS3231

Die NTP-Berechnung zieht die Verarbeitungsdauer des Servers (`T3 − T2`) von der gesamten Anfrage-/Antwortdauer ab. Aus der verbleibenden Netzlaufzeit wird unter Annahme symmetrischer Wege die Zeit bei Antwortempfang geschätzt. Das entspricht der Laufzeitkorrektur des [NTP-Verfahrens nach RFC 5905](https://www.rfc-editor.org/rfc/rfc5905.html#section-8). Diese Zeit wird mit der noch unkorrigierten GPS-Zeit am selben Empfangszeitpunkt verglichen.

Die Firmware hält bis zu fünf Vergleichsmessungen im RAM. Mindestens drei gültige Messungen sind nötig, bei störungsfreiem Betrieb also etwa eine Minute. Verwendet wird der Median der drei Messungen mit der kürzesten Netzlaufzeit. Eine Messung darf höchstens 100 ms Netzlaufzeit, 100 ms geschätzte NTP-Unsicherheit und einen GPS-Versatz von maximal ±1 Sekunde haben. Die drei ausgewählten Versatzwerte dürfen höchstens 20 ms auseinanderliegen. Andernfalls wird keine neue Kalibrierung aktiviert.

Bei aktiver Kalibrierung lautet die Zeitquelle **GPS + NTP-Kalibrierung**. Der ermittelte Versatz korrigiert jeden neuen GPS-Zeitanker. Stratum und Referenzkennung kennzeichnen dann die Abhängigkeit vom vorgelagerten NTP-Server; es wird kein unabhängiges GPS-Stratum 1 behauptet. Die Unsicherheit enthält die größte NTP-Unsicherheit der drei ausgewählten Vergleiche, deren Streuung, 8 ms UART-Reserve sowie einen Alterungszuschlag von 100 ppm.

Die Kalibrierung läuft ab, sobald die älteste der drei verwendeten Messungen 180 Sekunden alt ist. Sie wird außerdem bei Serverwechsel, Neustart, längerer GPS-Unterbrechung oder einer Änderung der GPS-Phasenlage um mehr als 20 ms verworfen. Danach gilt wieder die konservative GPS-Unsicherheit, bis neue stabile Vergleiche vorliegen. Ein leeres Serverfeld deaktiviert NTP-Ersatzzeit und Kalibrierung.

Damit lässt sich ein stabiler serieller GPS-Versatz ausgleichen. **Eine tatsächliche Genauigkeit von 1 ms wird nicht garantiert.** WLAN-Latenz, asymmetrische Netzwerkwege, ein falscher NTP-Server und wechselnde GPS-Ausgabeverzögerungen können unentdeckt bleiben. Ein genauer, unabhängig synchronisierter Server im lokalen Netz bietet bessere Voraussetzungen als ein entfernter Internetserver. Die angezeigte Unsicherheit ist eine Schätzung; die erreichte Genauigkeit muss gegen eine weitere unabhängige Zeitquelle gemessen werden.

Die NTP-Unsicherheit schätzt sich aus vorgelagerter Root-Dispersion, halber Root-Verzögerung, halber lokaler Paketlaufzeit, 10 ms Reserve und 100 ppm seit der Übernahme. Sie ist keine gemessene Genauigkeit. Nach 60 Sekunden ohne gültige Übernahme von einer der beiden Quellen ist der Server unsynchronisiert. Beim Deaktivieren oder Ändern des Ersatzservers wird eine bisherige Ersatzzeit bis zur nächsten gültigen Übernahme verworfen.

## Firmware-Update im Browser (OTA)

Diese Version einmal per USB installieren. Danach können weitere Firmware-Versionen über **Firmware aktualisieren** auf der normalen Statusseite hochgeladen werden. Die Funktion ist nur über die WLAN-IP bzw. `http://gps-ntp.local/` erreichbar; über den Einrichtungs-AP werden keine Updates angenommen.

1. Das feste **OTA-Standardpasswort lautet `gpsntp-update`**. Unter **OTA-Passwort ändern** auf der Statusseite kann es mit dem aktuellen Passwort geändert werden. Das neue Passwort zweimal eingeben (8–64 druckbare ASCII-Zeichen ohne Leerzeichen) und speichern. Die Änderung gilt sofort und bleibt nach Neustarts, OTA-Updates und WLAN-Reset erhalten. Das Passwort wird weder über die Status-API noch im seriellen Monitor ausgegeben. Ein vollständiges Löschen des Flashs stellt den Standard wieder her. Beim erstmaligen Wechsel von der früheren Firmware mit zufälligem Passwort wird dieses einmal durch `gpsntp-update` ersetzt; anschließend bleiben selbst gesetzte Passwörter erhalten. Wird diese Version noch mit der früheren OTA-Firmware hochgeladen, gilt für diesen einen Upload zunächst deren bisheriges Passwort.
2. Die neue Firmware mit `pio run` für **d1_mini** bauen. Die Upload-Datei liegt unter **`.pio/build/d1_mini/firmware.bin`**. Keine `.elf`-, Dateisystem- oder Firmware-Datei für ein anderes Board verwenden.
3. In der Statusseite die `.bin`-Datei auswählen, das OTA-Passwort eingeben und **Firmware hochladen und neu starten** bestätigen.
4. Die Fortschrittsanzeige zeigt die Übertragung. Erst nach vollständig empfangener Datei und erfolgreicher Prüfung durch den ESP8266-Updater wird das Update für den nächsten Start aktiviert. Dateigröße, verfügbarer Speicher und ESP-Firmware-Kopf werden geprüft; dies ist keine Signaturprüfung und keine Garantie, dass die hochgeladene Anwendung funktioniert. Nur eigene, vertrauenswürdige Builds verwenden.
5. Nach der Erfolgsmeldung startet der ESP automatisch neu. Kurz warten und die Seite neu laden. WLAN-Zugangsdaten, Notfall-NTP und OTA-Passwort bleiben bei Updates mit dieser Projektkonfiguration erhalten.

Ein abgebrochener oder abgelehnter Upload löst keinen Neustart aus. Bei einem Verbindungsabbruch ohne Erfolgsmeldung zunächst die Seite neu laden und den Gerätestatus prüfen. Während des Uploads können GPS-Verarbeitung und NTP-Antworten pausieren. Das Gerät verwendet HTTP; OTA daher nur im vertrauenswürdigen LAN nutzen und keine Portweiterleitung für die Weboberfläche einrichten. Falls das Gerät nach einem USB-Upload einen Bootstrap-Fehler für OTA meldet, einmal die Reset-Taste drücken oder die Versorgung kurz trennen und erneut versuchen.

## Zeitqualität und Grenzen

Für GPS verwendet diese Version **serielle NMEA-Zeit ohne PPS**. Die Firmware kompensiert die berechenbare Übertragungszeit des gültigen RMC-Satzes: bei 9600 Baud und 8N1 dauert jedes Zeichen ungefähr 1,042 ms. Der Zeitanker wird um die Übertragungsdauer vom `$` bis zum abschließenden `CR` zurückgerechnet. Die tatsächliche Korrektur steht auf der Statusseite unter **UART-Zeitkorrektur**.

Nur zeitnah verarbeitete RMC-Sätze werden als Zeitreferenz übernommen. Pausen von mehr als 10 ms in der GPS-Verarbeitung, Pufferüberläufe oder eine um mehr als 8 ms von der erwarteten Übertragungsdauer abweichende Satzaufnahme verwerfen den Satz für die Zeitübernahme. Satelliten- und Positionsdaten werden weiterhin ausgewertet. Die Statusseite zählt verworfene Sätze und Pufferüberläufe. Bei langen HTTP-/OTA-Vorgängen kann dieser Zähler steigen; mit dem nächsten rechtzeitig empfangenen RMC-Satz setzt die Zeitübernahme wieder ein.

NTP-Anfragen werden vor und nach HTTP-Verarbeitung bedient. WLAN-Schlafmodus ist ausgeschaltet, um zusätzliche Empfangslatenz zu vermeiden; dadurch kann der Stromverbrauch steigen. Diese Änderungen benötigen weder einen DS3231 noch eine zusätzliche GPS-Leitung.

Ohne aktive NTP-Kalibrierung bleiben die interne Verzögerung des GPS-Moduls und die Reihenfolge seiner ausgegebenen Sätze unbekannt. Die UART-Korrektur entfernt nur einen berechenbaren Anteil des Versatzes. Ohne aktive NTP-Kalibrierung werden als technische Planungsannahme **1000 ms plus 100 ppm seit der letzten Übernahme** angezeigt und in der NTP-Root-Dispersion gemeldet. Das ist weder eine Messung noch eine garantierte Fehlergrenze. Die tatsächliche Verbesserung muss gegen eine unabhängige Referenz gemessen werden. HDOP beschreibt die Positionsgeometrie und ist kein Maß für die Zeitgenauigkeit.

Die Firmware übernimmt GPS-Zeit nur mit aktueller gültiger GPS-Position und aktuellem Datum/Zeit. Nach 60 Sekunden ohne gültige Übernahme von GPS oder Notfall-NTP antwortet NTP mit Leap Indicator 3 und Stratum 16 (unsynchronisiert). Bei GPS-Zeit ohne NTP-Kalibrierung meldet der direkt GPS-gebundene Server Stratum 1; das bedeutet hier keine Mikrosekundengenauigkeit. Kalibrierte GPS-Zeit übernimmt die NTP-Abhängigkeit wie oben beschrieben. UTC wird unverändert ausgegeben, Zeitzone und Sommerzeit sind Aufgabe der Clients.

Für verlässlich höhere GPS-Genauigkeit unabhängig von NTP ist ein zugängliches TIMEPULSE/PPS-Signal mit eindeutiger Zuordnung von UTC-Sekunde zu Puls sowie eine entsprechend erweiterte Firmware nötig. Bei Platinen mit ausschließlich TX/RX/VCC/GND steht diese Option ohne Hardwareänderung nicht zur Verfügung. Die Zeitimpuls-Spezifikation des GPS-Chips ist nicht die erreichbare Genauigkeit eines WLAN-NTP-Clients. Bei alten GPS-Empfängern mögliche Datumsprobleme durch Firmware/GPS-Wochen-Rollover mit einer unabhängigen Referenz prüfen.

## Prüfung mit Hardware

- Erststart ohne gespeichertes WLAN: AP, DNS-Umleitung und WLAN-Speicherung prüfen.
- Setup zeigt ausschließlich WLAN-Einrichtung: Netzsuche, erneute Suche, Auswahl inklusive Sonderzeichen in SSIDs, manuelle Eingabe und Passwortanzeige prüfen. Nach erfolgreicher Anmeldung muss die Statusseite über die WLAN-IP erreichbar sein; über den AP bleibt auch `/dashboard` beim Setup.
- Während der 60 Sekunden nach erfolgreicher WLAN-Anmeldung über den AP `/`, `/dashboard` und eine Captive-Portal-Prüfadresse öffnen: alle müssen weiterhin das Setup zeigen bzw. dorthin umleiten. Gleichzeitig muss über die WLAN-IP die normale Statusoberfläche erreichbar sein.
- Falsches Passwort und WLAN-Ausfall: Portal bleibt bzw. wird erneut erreichbar.
- Unter freiem Himmel Satelliten und plausibles UTC-Datum prüfen.
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
```

Grundlagen: [u-blox NEO-6 Datenblatt](https://content.u-blox.com/sites/default/files/products/documents/NEO-6_DataSheet_%28GPS.G6-HW-09005%29.pdf), [TinyGPSPlus](https://github.com/mikalhart/TinyGPSPlus), [ESP8266 Arduino Core](https://github.com/esp8266/Arduino).
