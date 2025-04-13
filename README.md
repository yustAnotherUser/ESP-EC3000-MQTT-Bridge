# ESP-EC3000-MQTT-Bridge
Empfängt EC3000-Pakete und dekodiert sie über einen RFM69, der mit einem ESP8266/ESP32 verbunden ist, und sendet sie nach einigen Plausibilitätsprüfungen an MQTT.
---
Receives EC3000 packets and decodes them via a RFM69 connected to an ESP8266/ESP32 and sends them to MQTT after some sanity checks.

Volle Unterstützung des ESP32-C3 mit verbautem 0,42" OLED, der blauen LED und des "BO0"-Tasters mit zwei Funktionen.<br>
- Kurz drücken: Wechselt zur nächsten ID
- Länger als halbe Sekunde drücken: Wechselt die Schriftart der Anzeige! (aktuell 37 mehr oder minder zufällig ausgewählte aus den hunderten verfügbaren, kann einfach im Array angepasst werden)

Die LED leuchtet minimal für einen sehr kurzen Zeitraum wenn ein Paket in Ordnung ist und länger und stärker wenn ein Paket nicht in Ordnung ist.

Dass OLED zeigt in drei Zeilen:<br>
1234 ID<br>
Ein 1 Pixel breiter horizontaler "Ladebalken" der fünf Sekunden braucht (dass nicht veränderbare Intervall eines Senders zwischen den Paketen) und "neustartet" sobald dass nächste Paket da ist. (Wenn er am Ende stehenbleibt bedeutet das, dass er das nächste Paket komplett verpasst hat und wird daher solange am Ende stehen bis wieder ein Paket eintrifft)<br>
123,4 W<br>
12,345 KWH<br><br>

!! Ihr müsst eure WLAN und MQTT Daten im Quellcode eingeben !!<br><br>

MQTT Struktur:<br>
EC3000/debug		-> Removing stale ID (Durch fehlerhafte Pakete werden evtl. IDs erkannt die es nicht gibt welche aber sofort einen Platz im internen Tracker einnehmen.
                                     Wenn innerhalb von 66 Sekunden die ID _nicht_ nochmal empfangen wurde wird sie wieder von der internen Trackerliste gelöscht)<br>
EC3000/debug		-> Discarded (gefolgt von mindestens einer der folgenden Möglichkeiten)<br><br>
														OnSeconds > TotalSeconds;<br>
														IsOn=No but Power>0;<br>
														Power > 3600;<br>
	      													Power > MaxPower;<br>
														Resets not +1 (TODO: or +254) (last=1234);<br>
														Consumption invalid (last=1.234, delta=0.234)<br><br>
							also z.B.<br>
EC3000/debug		-> IsOn=No but Power>0; Power too high; Resets not +1 or +254 (last=1234); Consumption invalid (last=1.234, delta=0.234);<br>
<br>
EC3000/[ID]<br>
EC3000/1234			-> {"TotalSeconds":693790,"OnSeconds":85,"Consumption":0.000,"Power":0.0,"MaximumPower":11.3,"NumberOfResets":3,"IsOn":0,"CRC":"0x7514","RSSI":-72.00}<br>
<br>
Jedes Paket wird einer Plausibilitätsüberprüfung unterzogen bevor es an MQTT geleitet wird.
Die Pakete werden verworfen wenn eine der folgenden Bedingungen zutrifft:<br><br>

1. AKTIV: OnSeconds größer TotalSeconds
2. AKTIV: "IsOn" gleich "No" aber Power über 0
3. AKTIV: Stromverbrauchswert zu hoch (aktuell 3600 Watt)
4. AKTIV: Stromverbrauchswert höher als maximaler Stromverbrauchswert<br>
5. DEAKTIVIERT / TODO: wenn "Resets" nicht gleich geblieben ist oder es sich anders geändert hat als "+1" oder "+256"<br>
   (TODO: oder auch mal unkontrolliert 256 drauflegt aber nur bis ca. 3900 danach gehts wieder rückwärts in sovielen 256er Schritten
   wie möglich bis der Wert wieder unter 256 ist)<br>
   (5 von 6 Steckdosen von mir haben dieses Verhalten, wobei eine ca. alle 17 Std. +256 macht, vier davon machen +256 in größeren Abständen<br>
    und eine gar nicht, diese hat aber bereits einen sehr hohen Wert der bei einem weiteren +256 über 3900 kommen würde?!?).<br>
   (ein Reset ist wenn die Steckdose selbst vom Strom getrennt und wieder damit verbunden wurde z.B. durch ein vorgeschaltetes Funkrelay)<br>
   (wenn Ihr die Steckdose manuell zurückstellt (5 Sekunden auf die rote LED an der Steckdose drücken bis die LED dauerhaft leuchtet)<br>
   müsst ihr sogesehen die Bridge auch kurz neustarten da damit alle internen Werte der Steckdose auf 0 gesetzt werden und somit dass Tracking für diese ID verwirren würde)<br>
6. AKTIV: Wenn "Consumption" stärker gestiegen ist als in fünf Sekunden theoretisch maximal möglich ist (0.025 bei 3600 Watt)<br><br>

Diese aktuell fünf aktiven Checks filtern soweit ich dass aber beurteilen kann bereits alles raus was fehlerhafte Daten hat.<br><br>

NOTES/TODO:<br>
// Der RSSI Wert ist immer gleich... - Scheint sich doch mal zu ändern?!<br><br>

QUELLEN:<br>
EC3000 Paketverarbeitung und dazugehörige RFM69 Initialisierung<br>
https://github.com/Rainerlan/LaCrosseGatewayMQTT<br>
Display Code<br>
https://github.com/AlexYeryomin/ESP32C3-OLED-72x40<br>

