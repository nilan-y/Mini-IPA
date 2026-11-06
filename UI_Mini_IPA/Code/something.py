"""A small serial monitor for reading STM32 data over a USB serial port."""

from __future__ import annotations

import codecs
import queue
import threading
import tkinter as tk
from tkinter import messagebox, ttk


class SerialMonitor:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title("STM32 USB Serial Monitor")
        self.root.minsize(700, 460)

        self._events: queue.Queue[tuple[str, str]] = queue.Queue()
        self._commands: queue.Queue[str] = queue.Queue()
        self._stop_event: threading.Event | None = None
        self._closing = False
        self._connecting = False
        self._connected = False

        self.port_var = tk.StringVar()
        self.baud_var = tk.StringVar(value="115200")
        self.command_var = tk.StringVar()
        self.status_var = tk.StringVar(value="Disconnected")

        self._build_ui()
        self.refresh_ports()
        self.root.protocol("WM_DELETE_WINDOW", self.close)
        self.root.after(100, self._process_events)

    def _build_ui(self) -> None:
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(1, weight=1)

        controls = ttk.Frame(self.root, padding=12)
        controls.grid(row=0, column=0, sticky="ew")
        controls.columnconfigure(1, weight=1)

        ttk.Label(controls, text="USB serial port").grid(
            row=0, column=0, padx=(0, 8), sticky="w"
        )
        self.port_combo = ttk.Combobox(
            controls, textvariable=self.port_var, width=18
        )
        self.port_combo.grid(row=0, column=1, padx=(0, 8), sticky="ew")
        self.refresh_button = ttk.Button(
            controls, text="Refresh", command=self.refresh_ports
        )
        self.refresh_button.grid(row=0, column=2, padx=(0, 16))

        ttk.Label(controls, text="Baud rate").grid(
            row=0, column=3, padx=(0, 8), sticky="w"
        )
        self.baud_combo = ttk.Combobox(
            controls,
            textvariable=self.baud_var,
            values=("9600", "19200", "38400", "57600", "115200", "230400"),
            width=10,
        )
        self.baud_combo.grid(row=0, column=4, padx=(0, 12))

        self.connect_button = ttk.Button(
            controls, text="Connect", command=self.toggle_connection
        )
        self.connect_button.grid(row=0, column=5)

        ttk.Label(controls, text="Command").grid(
            row=1, column=0, padx=(0, 8), pady=(10, 0), sticky="w"
        )
        self.command_entry = ttk.Entry(
            controls, textvariable=self.command_var, state="disabled"
        )
        self.command_entry.grid(
            row=1, column=1, columnspan=4, padx=(0, 8), pady=(10, 0), sticky="ew"
        )
        self.command_entry.bind("<Return>", self._send_command_from_entry)
        self.send_button = ttk.Button(
            controls,
            text="Send",
            command=self.send_command,
            state="disabled",
        )
        self.send_button.grid(row=1, column=5, pady=(10, 0))
        ttk.Label(
            controls,
            text="Sends UTF-8 text followed by a newline. The STM32 firmware must handle the command and send a reply.",
        ).grid(row=2, column=0, columnspan=6, pady=(8, 0), sticky="w")

        content_frame = ttk.Frame(self.root)
        content_frame.grid(row=1, column=0, sticky="nsew")
        content_frame.columnconfigure(1, weight=1)
        content_frame.rowconfigure(0, weight=1)

        number_buttons = ttk.Frame(content_frame, padding=(12, 0, 0, 12))
        number_buttons.grid(row=0, column=0, sticky="ns")
        self.number_buttons = []
        for number in range(1, 11):
            button = ttk.Button(
                number_buttons,
                text=str(number),
                command=lambda value=number: self.send_command(str(value)),
                state="disabled",
                width=4,
            )
            button.grid(row=number - 1, column=0, pady=(0, 4), sticky="ew")
            self.number_buttons.append(button)

        output_frame = ttk.Frame(content_frame, padding=(12, 0, 12, 12))
        output_frame.grid(row=0, column=1, sticky="nsew")
        output_frame.columnconfigure(0, weight=1)
        output_frame.rowconfigure(0, weight=1)

        self.output = tk.Text(
            output_frame,
            wrap="none",
            state="disabled",
            font=("Consolas", 10),
            undo=False,
        )
        self.output.grid(row=0, column=0, sticky="nsew")

        y_scroll = ttk.Scrollbar(
            output_frame, orient="vertical", command=self.output.yview
        )
        y_scroll.grid(row=0, column=1, sticky="ns")
        x_scroll = ttk.Scrollbar(
            output_frame, orient="horizontal", command=self.output.xview
        )
        x_scroll.grid(row=1, column=0, sticky="ew")
        self.output.configure(yscrollcommand=y_scroll.set, xscrollcommand=x_scroll.set)

        footer = ttk.Frame(self.root, padding=(12, 0, 12, 10))
        footer.grid(row=2, column=0, sticky="ew")
        footer.columnconfigure(0, weight=1)
        ttk.Label(footer, textvariable=self.status_var).grid(
            row=0, column=0, sticky="w"
        )
        ttk.Button(footer, text="Clear output", command=self.clear_output).grid(
            row=0, column=1, sticky="e"
        )

    def refresh_ports(self) -> None:
        try:
            from serial.tools import list_ports
        except ImportError:
            self.status_var.set("Install pyserial to scan USB serial ports.")
            return

        ports = sorted(port.device for port in list_ports.comports())
        self.port_combo["values"] = ports

        current_port = self.port_var.get()
        if current_port not in ports:
            self.port_var.set(ports[0] if ports else "")

        if not self._connecting and not self._connected:
            if ports:
                self.status_var.set(f"{len(ports)} serial port(s) found.")
            else:
                self.status_var.set(
                    "No serial ports found. Connect the USB module or enter a COM port."
                )

    def toggle_connection(self) -> None:
        if self._connecting or self._connected:
            self._request_disconnect()
        else:
            self._start_connection()

    def _start_connection(self) -> None:
        port = self.port_var.get().strip()
        if not port:
            messagebox.showwarning(
                "Select a port",
                "Choose a USB serial port or enter its COM port name, such as COM3.",
                parent=self.root,
            )
            return

        try:
            baud_rate = int(self.baud_var.get().strip())
            if baud_rate <= 0:
                raise ValueError
        except ValueError:
            messagebox.showwarning(
                "Invalid baud rate",
                "Enter a positive integer baud rate.",
                parent=self.root,
            )
            return

        self._stop_event = threading.Event()
        while True:
            try:
                self._commands.get_nowait()
            except queue.Empty:
                break
        self._connecting = True
        self.connect_button.configure(text="Disconnect", state="normal")
        self.command_entry.configure(state="disabled")
        self.send_button.configure(state="disabled")
        self.status_var.set(f"Connecting to {port}...")

        worker = threading.Thread(
            target=self._read_serial,
            args=(port, baud_rate, self._stop_event),
            daemon=True,
        )
        worker.start()

    def _read_serial(
        self, port: str, baud_rate: int, stop_event: threading.Event
    ) -> None:
        try:
            import serial
        except ImportError:
            self._events.put(
                (
                    "error",
                    "pyserial is not installed. Install it with: "
                    "pip install -r Code\\requirements.txt",
                )
            )
            self._events.put(("disconnected", ""))
            return

        try:
            decoder = codecs.getincrementaldecoder("utf-8")(errors="replace")
            with serial.Serial(port, baudrate=baud_rate, timeout=0.2) as connection:
                self._events.put(("connected", port))
                while not stop_event.is_set():
                    try:
                        command = self._commands.get_nowait()
                    except queue.Empty:
                        pass
                    else:
                        connection.write(command.encode("utf-8"))
                        self._events.put(("sent", command[:-1]))

                    data = connection.read(256)
                    if data:
                        self._events.put(("data", decoder.decode(data)))
                remaining_text = decoder.decode(b"", final=True)
                if remaining_text:
                    self._events.put(("data", remaining_text))
        except (serial.SerialException, OSError) as exc:
            self._events.put(("error", str(exc)))
        finally:
            self._events.put(("disconnected", ""))

    def _request_disconnect(self) -> None:
        if self._stop_event is not None:
            self._stop_event.set()
        self.connect_button.configure(state="disabled")
        self.command_entry.configure(state="disabled")
        self.send_button.configure(state="disabled")
        self.status_var.set("Disconnecting...")

    def _send_command_from_entry(self, event: tk.Event[tk.Misc]) -> str:
        if event.widget is self.command_entry:
            return self.send_command()
        return "break"

    def send_command(self, command: str | None = None) -> str:
        from_entry = command is None
        if from_entry:
            command = self.command_var.get()
        command = command.rstrip("\r\n")
        if not command.strip():
            return "break"
        if "\n" in command or "\r" in command:
            messagebox.showwarning(
                "Invalid command",
                "Send one command at a time; embedded line breaks are not allowed.",
                parent=self.root,
            )
            return "break"
        if not self._connected:
            self.status_var.set("Connect to the STM32 before sending a command.")
            return "break"

        self._commands.put(command + "\n")
        if from_entry:
            self.command_var.set("")
        return "break"

    def _process_events(self) -> None:
        if self._closing:
            return

        while True:
            try:
                event, value = self._events.get_nowait()
            except queue.Empty:
                break

            if event == "connected":
                self._connecting = False
                self._connected = True
                self.connect_button.configure(text="Disconnect", state="normal")
                self.command_entry.configure(state="normal")
                self.send_button.configure(state="normal")
                for button in self.number_buttons:
                    button.configure(state="normal")
                self.status_var.set(f"Connected to {value}")
            elif event == "data":
                self._append_output(value)
            elif event == "sent":
                self._append_output(f"[TX] {value}\n")
            elif event == "error":
                self.status_var.set(f"Serial error: {value}")
                messagebox.showerror(
                    "Serial connection error", value, parent=self.root
                )
            elif event == "disconnected":
                self._connecting = False
                self._connected = False
                self._stop_event = None
                self.connect_button.configure(text="Connect", state="normal")
                self.command_entry.configure(state="disabled")
                self.send_button.configure(state="disabled")
                for button in self.number_buttons:
                    button.configure(state="disabled")
                if not self.status_var.get().startswith("Serial error:"):
                    self.status_var.set("Disconnected")

        self.root.after(100, self._process_events)

    def _append_output(self, text: str) -> None:
        self.output.configure(state="normal")
        self.output.insert("end", text)

        max_characters = 200_000
        character_count = len(self.output.get("1.0", "end-1c"))
        if character_count > max_characters:
            self.output.delete(
                "1.0", f"1.0 + {character_count - max_characters} chars"
            )

        self.output.see("end")
        self.output.configure(state="disabled")

    def clear_output(self) -> None:
        self.output.configure(state="normal")
        self.output.delete("1.0", "end")
        self.output.configure(state="disabled")

    def close(self) -> None:
        self._closing = True
        if self._stop_event is not None:
            self._stop_event.set()
        self.root.destroy()


def main() -> None:
    root = tk.Tk()
    SerialMonitor(root)
    root.mainloop()


if __name__ == "__main__":
    main()