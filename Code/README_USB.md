# USB-Befehle fuer die UI

In `Core/Src/main.c` sind `Button1_Action()` bis `Button10_Action()`
vorbereitet. Die jeweiligen TODO-Kommentare durch die gewuenschte Funktion
ersetzen. `ExecuteUsbCommand()` ordnet jede empfangene Zahl dem passenden
Platzhalter zu. Die Funktionen laufen in der Hauptschleife, ausserhalb des
USB-Interrupts. Lange blockierende Funktionen verzoegern weitere Befehle.

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
Es gibt aktuell keine Rueckmeldung an die UI.

## Bauen und pruefen

Das Projekt `Code` in STM32CubeIDE bauen und auf den STM32F042 flashen.
Die neuen Dateien liegen im bereits konfigurierten Quellordner `Core`.
Aenderungen in generierten Dateien stehen in USER-CODE-Bloecken.

Der Parser laesst sich ohne Hardware mit GCC testen (aus dem Repo-Stamm):

```sh
mkdir -p work
gcc -std=c11 -Wall -Wextra -Werror -ICode/Tests/stubs -ICode/Core/Inc \
  Code/Core/Src/usb_commands.c Code/Tests/test_usb_commands.c \
  -o work/test_usb_commands
./work/test_usb_commands
```

Zum Hardwaretest einen Breakpoint in der gewuenschten `ButtonN_Action()`
setzen und den entsprechenden UI-Knopf druecken. Die leeren Platzhalter
aendern bisher keine Ausgaenge.
