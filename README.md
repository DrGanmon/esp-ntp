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

## Notfall-NTP

In der Weboberfläche unter **Notfall-NTP** einen Vorschlag auswählen oder einen eigenen Hostnamen bzw. eine IPv4-Adresse eintragen. Keine URL und keinen Port angeben. **Notfall-NTP speichern** speichert die Auswahl dauerhaft im EEPROM-Bereich des Flashs. Ein leeres Feld deaktiviert den Ersatzserver; im Auslieferungszustand ist er deaktiviert. Die Vorschläge entsprechen den Zonen des [NTP Pool Project](https://www.ntppool.org/en/use.html).

GPS hat immer Vorrang. Sobald seit fünf Sekunden keine neue gültige GPS-Zeit übernommen wurde (oder beim Start noch keine vorliegt), fragt der ESP den eingestellten Ersatzserver über das verbundene WLAN ab. Die Abfrage wiederholt sich alle 30 Sekunden. UDP-Antworten werden ohne Warteschleife verarbeitet; die DNS-Auflösung kann pro Versuch bis zu einer Sekunde dauern. Ein externer Server benötigt Internetzugang, ein lokaler Server nur Erreichbarkeit im LAN.

Die Statusseite zeigt aktive Zeitquelle, Stratum, Ersatzserver und dessen Abfragestatus. Bei Ersatzzeit meldet der ESP das Stratum des vorgelagerten Servers plus eins; Stratum 0 (einschließlich Kiss-o'-Death), 15 und 16 werden nicht übernommen. Absender, Port, Anfragekennung, Version, Modus, Synchronisationsstatus, Datum und Root-Distanz werden geprüft. Diese Prüfungen sind keine kryptografische Authentifizierung. Kommt GPS zurück, wird sofort wieder GPS-Zeit verwendet.

Die NTP-Unsicherheit schätzt sich aus vorgelagerter Root-Dispersion, halber Root-Verzögerung, halber lokaler Paketlaufzeit, 10 ms Reserve und 100 ppm seit der Übernahme. Sie ist keine gemessene Genauigkeit. Nach 60 Sekunden ohne gültige Übernahme von einer der beiden Quellen ist der Server unsynchronisiert. Beim Deaktivieren oder Ändern des Ersatzservers wird eine bisherige Ersatzzeit bis zur nächsten gültigen Übernahme verworfen.

## Zeitqualität und Grenzen

Für GPS verwendet diese Version **serielle NMEA-Zeit ohne PPS**. UART-Ausgabezeit, Satzreihenfolge und Verarbeitung erzeugen einen unbekannten Versatz. Als technische Planungsannahme werden bei GPS-Zeit **1000 ms plus 100 ppm seit der letzten Übernahme** angezeigt und in der NTP-Root-Dispersion gemeldet. Das ist weder eine Messung noch eine garantierte Fehlergrenze. HDOP beschreibt die Positionsgeometrie und ist kein Maß für die Zeitgenauigkeit.

Die Firmware übernimmt GPS-Zeit nur mit aktueller gültiger GPS-Position und aktuellem Datum/Zeit. Nach 60 Sekunden ohne gültige Übernahme von GPS oder Notfall-NTP antwortet NTP mit Leap Indicator 3 und Stratum 16 (unsynchronisiert). Bei GPS-Zeit meldet der direkt GPS-gebundene Server Stratum 1; das bedeutet hier keine Mikrosekundengenauigkeit. UTC wird unverändert ausgegeben, Zeitzone und Sommerzeit sind Aufgabe der Clients.

Für Millisekunden- oder bessere Genauigkeit muss das TIMEPULSE/PPS-Signal herausgeführt und die Firmware um eine eindeutige Zuordnung von UTC-Sekunde zu Puls sowie eine geregelte Uhr erweitert werden. Die Zeitimpuls-Spezifikation des GPS-Chips ist nicht die erreichbare Genauigkeit eines WLAN-NTP-Clients. Bei alten GPS-Empfängern mögliche Datumsprobleme durch Firmware/GPS-Wochen-Rollover mit einer unabhängigen Referenz prüfen.

## Prüfung mit Hardware

- Erststart ohne gespeichertes WLAN: AP, DNS-Umleitung und WLAN-Speicherung prüfen.
- Setup zeigt ausschließlich WLAN-Einrichtung: Netzsuche, erneute Suche, Auswahl inklusive Sonderzeichen in SSIDs, manuelle Eingabe und Passwortanzeige prüfen. Nach erfolgreicher Anmeldung muss die Statusseite über die WLAN-IP erreichbar sein; über den AP bleibt auch `/dashboard` beim Setup.
- Während der 60 Sekunden nach erfolgreicher WLAN-Anmeldung über den AP `/`, `/dashboard` und eine Captive-Portal-Prüfadresse öffnen: alle müssen weiterhin das Setup zeigen bzw. dorthin umleiten. Gleichzeitig muss über die WLAN-IP die normale Statusoberfläche erreichbar sein.
- Falsches Passwort und WLAN-Ausfall: Portal bleibt bzw. wird erneut erreichbar.
- Unter freiem Himmel Satelliten und plausibles UTC-Datum prüfen.
- Mit einem NTP-Client Synchronisationsstatus und Abweichung gegen eine unabhängige Quelle messen.
- GPS-TX trennen, Notfall-NTP deaktiviert: nach spätestens 60 Sekunden muss NTP unsynchronisiert melden; Satellitenwerte werden nach 5 Sekunden ausgeblendet.
- Erreichbaren Ersatzserver eintragen, GPS-TX trennen: nach ungefähr 5 Sekunden muss die Zeitquelle auf Notfall-NTP wechseln. GPS wieder anschließen: Rückkehr zu GPS prüfen.
- Ersatzserver speichern und ESP neu starten: gespeicherte Auswahl muss erhalten bleiben. Ungültigen bzw. unerreichbaren Server testen: keine Zeitübernahme, nach Ablauf der letzten gültigen Zeit unsynchronisiert.
- WLAN-Reset bestätigen: gespeichertes WLAN darf nach dem Neustart nicht mehr verbunden werden; Einrichtung über AP muss erneut funktionieren. Abbruch der Bestätigung darf nichts ändern.

Die Verarbeitung von NTP-Antworten lässt sich zusätzlich ohne Hardware prüfen:

```sh
g++ -std=c++11 -Wall -Wextra -Iinclude tests/fallback_test.cpp -o /tmp/esp-ntp-fallback-test
/tmp/esp-ntp-fallback-test
```

Grundlagen: [u-blox NEO-6 Datenblatt](https://content.u-blox.com/sites/default/files/products/documents/NEO-6_DataSheet_%28GPS.G6-HW-09005%29.pdf), [TinyGPSPlus](https://github.com/mikalhart/TinyGPSPlus), [ESP8266 Arduino Core](https://github.com/esp8266/Arduino).
