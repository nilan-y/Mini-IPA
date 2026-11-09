# USB-Befehle fuer die UI

In `Core/Src/main.c` ordnet `ExecuteUsbCommand()` jede empfangene Zahl
`Button1_Action()` bis `Button10_Action()` zu. Alle Aktionen laufen in der
Hauptschleife, ausserhalb des USB-Interrupts.

| Befehl | Aktion |
| --- | --- |
| `1\n` | Laufende Messung starten, jeden ausgelesenen Wert ueber USB senden |
| `2\n` | Sensor stoppen und noch nicht gesendete Ergebnisse/Einzeltrigger verwerfen |
| `3\n` | Genau eine frische Messung durchfuehren und senden |

Die Funktionen fuer 4 bis 10 enthalten weiterhin TODO-Platzhalter. Eine
erneute 1 waehrend der laufenden Messung startet den Sensor nicht neu.
Eine 3 waehrend der laufenden Messung stoppt diese und wechselt zur
Einzelmessung. Weitere Einzeltrigger warten auf den Abschluss der vorherigen
Messung/Antwort (bis zu 15 ausstehende Einzeltrigger; weitere werden verworfen).
Eine 1 wechselt nach einer noch laufenden Einzelmessung/Antwort in den
kontinuierlichen Modus und verwirft weitere wartende Einzeltrigger.
Stoppen im Leerlauf ist ebenfalls erlaubt.

## Sensor auslesen und USB-Aktivitaet

Knopf 1 ruft `SensorApp_StartContinuous()`, Knopf 2 `SensorApp_Stop()` und
Knopf 3 `SensorApp_Measure()` aus `Core/Src/sensor_app.c` auf.
Beim ersten Aufruf wird XSHUT ueber den vorhandenen `SHUT_TOF`-GPIO kurz auf
LOW, dann auf HIGH gesetzt. `VL53L4CD_RegisterBusIO()` verbindet den
ST-Sensortreiber mit `BSP_I2C1_Init/Send/Recv` und `BSP_GetTick` aus `custom_bus.c`.
Der Bus verwendet damit die vorhandene CubeMX-Konfiguration (I2C1,
PB8/SCL, PB7/SDA). Die Sensoradresse ist der ST-Standardwert 0x52
(die fuer HAL bereits verschobene 7-Bit-Adresse 0x29).

Nach erfolgreicher ID-Pruefung und Initialisierung startet Befehl 1 den
ST-Treiber im Modus ASYNC_CONTINUOUS. Die Hauptschleife fragt die
Datenbereitschaft ab und liest jeweils einen neuen Wert. Der ST-Treiber
quittiert jeden Wert; nach Abschluss seiner USB-Sendung wird der naechste
ausgelesen. Die vorhandene ST-Initialisierung verwendet ein Timingbudget
von 50 ms (ungefaehr 20 Messungen/s; die USB-Uebertragung kann die Ausgaberate
begrenzen). Befehl 3 verwendet ASYNC_ONESHOT und stoppt nach einer Messung.
Vor dem Start eines neuen Messlaufs wird ein altes Datenbereit-Flag geloescht.
Wird innerhalb von 500 ms kein neuer Wert bereit, folgt ein Timeout.
Die erste Sensorinitialisierung und einzelne I2C-Transfers sind durch den
vorhandenen ST-/HAL-Treiber blockierend; die I2C-Timeouts des BSP bleiben erhalten.

Die UI zeigt die UTF-8/ASCII-Antwort direkt im vorhandenen Ausgabefeld:

```text
D: 123mm
```

Bei Fehlern erscheint stattdessen beispielsweise `ERROR TOF NOT_FOUND`,
`ERROR TOF TIMEOUT` oder `Out of Range=4`. Jede Antwort endet mit
CR/LF. Nur Sensorstatus 0 wird als gueltige Distanz ausgegeben. Nach einem
Initialisierungs- oder Busfehler versucht der naechste Knopfdruck die
Sensorinitialisierung erneut. Bus-/Lesefehler und Timeouts beenden auch eine
laufende Messung; ungueltige Range-Statuswerte werden gemeldet, waehrend der
kontinuierliche Modus weiterlaeuft.

### Fester Kalibrier-Offset

Der bekannte Offset ist in `Core/Src/sensor_app.c` als
`SENSOR_OFFSET_MM` auf **+6 mm** eingestellt. Nach jeder erfolgreichen
`VL53L4CD_Init()` wird er mit der vorhandenen ST-Funktion
`VL53L4CD_SetOffset()` in den Sensor geschrieben. Das geschieht auch nach
einem Neustart oder einer erneuten Initialisierung nach einem Fehler.
Der Sensor beruecksichtigt die Korrektur selbst; die USB-Ausgabe addiert
die 6 mm nicht nochmals. +6 mm bedeutet gemaess ST eine positive
Distanzkorrektur (z. B. 94 mm unkorrigiert entsprechen etwa 100 mm korrigiert).

Schlaegt das Setzen fehl, wird keine Messung gestartet. Die UI erhaelt
`ERROR TOF OFFSET`; beim naechsten Knopfdruck wird erneut initialisiert
und der Offset erneut gesetzt.

Hier wird ein bekannter Kalibrierwert uebernommen. Eine Kalibrierung zum
Ermitteln eines neuen Offsets (`VL53L4CD_OffsetCalibration()`) benoetigt ein
Referenzziel in bekanntem Abstand und ist kein automatischer Startschritt.

Die Antwort liegt in einem statischen Puffer, bis die asynchrone USB-Sendung
fertig ist. Bei belegter Sendeschnittstelle wird erneut versucht zu senden;
nach 1000 ms ohne Annahme wird die ausstehende Antwort verworfen und eine
laufende Messung gestoppt. Wurde eine
Sendung angenommen, bleibt ihr Puffer bis zum Abschluss oder USB-Abbruch
reserviert. Befehle werden auch waehrend Messung und Sendung verarbeitet:
Insbesondere stoppt 2 den Sensor, ohne auf die USB-Schnittstelle zu warten.
Eine schon angenommene USB-Sendung kann noch eintreffen, ihr Puffer wird
bis zum Abschluss nicht ueberschrieben. Ein fehlerhafter Stop wird als
`ERROR TOF STOP` gemeldet, sobald der Sendepuffer frei ist.

LED_USB leuchtet bei empfangenen Nutzdaten und angenommenen Sendungen
mindestens fuer einen sichtbaren 100-ms-Impuls. Weitere Aktivitaet verlaengert
den Impuls. `CDC_ProcessActivity_FS()` aktualisiert die LED in der Hauptschleife;
waehrend blockierender Initialisierung/I2C-Zugriffe kann sie laenger leuchten.
Die LED zeigt Datenverkehr an, nicht nur eine bestehende USB-Verbindung.

## HAL und CubeMX

Die vorhandene CubeMX-Konfiguration fuer Pins, Clock und Peripherie bleibt
erhalten. `Code.ioc`, `SystemClock_Config()`, `MX_GPIO_Init()` und die
anderen generierten Initialisierungsfunktionen wurden nicht geaendert.

Der USB-Empfang verwendet den vorhandenen ST-Stack:
`HAL_PCD_IRQHandler()` verarbeitet den USB-Interrupt. Die USB-Device-
Middleware ruft anschliessend `CDC_Receive_FS()` auf. Dort werden die Bytes
ausgewertet und der naechste Empfang mit `USBD_CDC_SetRxBuffer()` und
`USBD_CDC_ReceivePacket()` aktiviert. Diese CDC-Funktionen gehoeren zur
ST-USB-Device-Middleware und verwenden darunter die HAL-PCD-Treiber.

Die HAL kennt die Bedeutung der UI-Zahlen nicht. Deshalb sind die
Zeilenauswertung und die Zuordnung zu `ButtonN_Action()` Anwendungscode.
Der kleine Puffer uebergibt die Befehle an die Hauptschleife, damit spaetere
Funktionen den USB-Interrupt nicht blockieren. Er benoetigt keine globale
Interrupt-Sperre: Nur der USB-Interrupt schreibt den Schreibindex, nur die
Hauptschleife den Leseindex. Die gemeinsamen Zugriffe sind volatile und
byteweise; sie sind auf diesem STM32F042 atomar.

## Sendeformat der UI

Das Format wurde anhand des bereitgestellten Python-Codes bestaetigt:
Die Zahlenknoepfe rufen `self.send_command(str(value))` auf.
`send_command()` stellt mit `self._commands.put(command + "\n")` eine
abgeschlossene Zeile bereit. Der serielle Thread sendet diese mit
`connection.write(command.encode("utf-8"))`.

- Die UI sendet die Dezimalzahl `1` bis `10` als UTF-8-Text mit `\n`,
  beispielsweise `b"1\n"` oder `b"10\n"`. Fuer diese Ziffern entspricht
  UTF-8 der ASCII-Darstellung.
- Die Firmware akzeptiert zusaetzlich `\r` und `\r\n` als Zeilenende.
- Zahlen ohne Abschluss werden bis zum Zeilenende gesammelt. USB-Paketgrenzen
  spielen keine Rolle; auch aufgeteilte `10` und mehrere Befehle pro Paket
  werden verarbeitet.
- Leere Zeilen und ungueltige Zeilen (Text, Zahlen ausserhalb 1–10,
  mehr als zwei Ziffern) werden ignoriert. Binaere Zahlenbytes werden nicht
  als Befehle interpretiert.

`USB_DEVICE/App/usbd_cdc_if.c` uebergibt empfangene Bytes an
`Core/Src/usb_commands.c` und aktiviert danach den naechsten USB-Empfang.
Die Warteschlange speichert maximal 15 ausstehende Befehle in Reihenfolge.
Bei voller Warteschlange wird der neu eintreffende Befehl verworfen.
Die Messwerte von Befehl 1 und 3 verwenden dasselbe Antwortformat.

## Bauen und pruefen

Das Projekt `Code` in STM32CubeIDE bauen und auf den STM32F042 flashen.
Die neuen Dateien liegen im bereits konfigurierten Quellordner `Core`.
Aenderungen in generierten Dateien stehen in USER-CODE-Bloecken.

Der Parser laesst sich ohne Hardware mit GCC testen (aus dem Repo-Stamm):

```sh
mkdir -p work
gcc -std=c11 -Wall -Wextra -Werror -ICode/Core/Inc \
  Code/Core/Src/usb_commands.c Code/Tests/test_usb_commands.c \
  -o work/test_usb_commands
./work/test_usb_commands
```

Parser- und Sensor/USB-Integrationstests gemeinsam:

```sh
python3 Code/Tests/run_host_tests.py
```

Der Integrationstest verwendet den echten Anwendungscode und die echten
CDC-Callbacks mit simulierten HAL-/Sensor-/USB-Grenzen. Er ersetzt keinen
Hardwaretest. Zum Testen auf dem Board: Firmware bauen/flashen, UI verbinden,
Im Auto-Modus START (1) druecken und das Objekt vor dem Sensor bewegen. Die angezeigte Distanz
in Millimetern muss sich entsprechend aendern; LED_USB soll beim Senden und
Empfangen aufleuchten. STOP (2) muss die laufende Messung beenden. Im
Single-Modus sendet START/TRIGGER eine 3 und es darf genau eine neue
Messantwort erscheinen. Deine bestehende UI sendet diese Befehle bereits.
