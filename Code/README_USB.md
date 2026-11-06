# USB-Befehle fuer die UI

In `Core/Src/main.c` ordnet `ExecuteUsbCommand()` jede empfangene Zahl
`Button1_Action()` bis `Button10_Action()` zu. Knopf 1 misst die Distanz mit
dem VL53L4CD und sendet das Ergebnis zur UI. Knopf 2 setzt wie bisher LED_B1
auf LOW. Die weiteren Funktionen enthalten TODO-Platzhalter. Alle Aktionen
laufen in der Hauptschleife, ausserhalb des USB-Interrupts.

## Sensor auslesen und USB-Aktivitaet

Knopf 1 ruft `SensorApp_Measure()` aus `Core/Src/sensor_app.c` auf.
Beim ersten Aufruf wird XSHUT ueber den vorhandenen `SHUT_TOF`-GPIO kurz auf
LOW, dann auf HIGH gesetzt. `VL53L4CD_RegisterBusIO()` verbindet den
ST-Sensortreiber mit `BSP_I2C1_Init/Send/Recv/GetTick` aus `custom_bus.c`.
Der Bus verwendet damit die vorhandene CubeMX-Konfiguration (I2C1,
PB8/SCL, PB7/SDA). Die Sensoradresse ist der ST-Standardwert 0x52
(die fuer HAL bereits verschobene 7-Bit-Adresse 0x29).

Nach erfolgreicher ID-Pruefung und Initialisierung startet jeder Knopfdruck
eine neue Messung mit dem ST-Treiber im Modus ASYNC_ONESHOT. Die Hauptschleife
fragt die Datenbereitschaft ab, liest den Wert und stoppt die Messung.
Ein altes Datenbereit-Flag wird vor jeder neuen Messung geloescht.
Wird innerhalb von 500 ms kein neuer Wert bereit, folgt ein Timeout.
Die erste Sensorinitialisierung und einzelne I2C-Transfers sind durch den
vorhandenen ST-/HAL-Treiber blockierend; die I2C-Timeouts des BSP bleiben erhalten.

Die UI zeigt die UTF-8/ASCII-Antwort direkt im vorhandenen Ausgabefeld:

```text
DISTANCE_MM=123
```

Bei Fehlern erscheint stattdessen beispielsweise `ERROR TOF NOT_FOUND`,
`ERROR TOF TIMEOUT` oder `ERROR TOF RANGE_STATUS=4`. Jede Antwort endet mit
CR/LF. Nur Sensorstatus 0 wird als gueltige Distanz ausgegeben. Nach einem
Initialisierungs- oder Busfehler versucht der naechste Knopfdruck die
Sensorinitialisierung erneut.

Die Antwort liegt in einem statischen Puffer, bis die asynchrone USB-Sendung
fertig ist. Bei belegter Sendeschnittstelle wird erneut versucht zu senden;
nach 1000 ms ohne Annahme wird die ausstehende Antwort verworfen. Wurde eine
Sendung angenommen, bleibt ihr Puffer bis zum Abschluss oder USB-Abbruch
reserviert. Weitere Befehle warten waehrend Messung/Antwort in der
Befehlswarteschlange.

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
Knopf 1 sendet die oben beschriebene Sensorantwort an die UI.

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
Knopf 1 druecken und das Objekt vor dem Sensor bewegen. Die angezeigte Distanz
in Millimetern muss sich entsprechend aendern; LED_USB soll beim Senden und
Empfangen aufleuchten. Knopf 2 setzt weiterhin LED_B1 auf LOW.
