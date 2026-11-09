"""A small serial monitor for reading STM32 data over a USB serial port."""

from __future__ import annotations

import codecs
import math
import queue
import re
import threading
import time
import tkinter as tk
from tkinter import messagebox, ttk


class SerialMonitor:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title("STM32 USB Serial Monitor")
        self.root.geometry("1000x700")
        self.root.minsize(1000, 460)
        self.root.resizable(False, True)

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
        self.page_var = tk.StringVar(value="Main")
        self.mode_var = tk.StringVar(value="Single")
        self.logging_var = tk.StringVar(value="Logging")
        self.graph_logging_status_var = tk.StringVar(value="Graph: Not logging")
        self.distance_var = tk.StringVar(value="Waiting...")
        self.laser_state_var = tk.StringVar(value="OFF")
        self.laser_power_var = tk.DoubleVar(value=0)
        self.laser_power_percent_var = tk.StringVar(value="0%")
        self.laser_power_consumption_var = tk.StringVar()
        self.system_power_consumption_var = tk.StringVar()
        self._last_laser_state = "OFF"
        self._last_laser_power_value = 0
        self._laser_power_after_id: str | None = None
        self._pending_laser_power_value: int | None = None
        self._last_distance_mm: float | None = None
        self._distance_line_buffer = ""
        self._measurement_started_at: float | None = None
        self._measurement_stopped_at: float | None = None
        self._active_measurement_mode: str | None = None
        self._graph_logging_active = False
        self._graph_samples: list[tuple[float, float]] = []
        self.x_range = (0.0, 10.0)
        self._continuous_window_seconds = 10.0
        self.y_range = (0.0, 1100.0)
        self._graph_plot_bounds: tuple[float, float, float, float] | None = None
        self._graph_axes: tuple[float, float] | None = None
        self._graph_pan_start: (
            tuple[int, int, tuple[float, float], tuple[float, float]] | None
        ) = None

        self._build_ui()
        self._update_laser_controls()
        self.refresh_ports()
        self.root.protocol("WM_DELETE_WINDOW", self.close)
        self.root.after(100, self._process_events)

    def _build_ui(self) -> None:
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(1, weight=1)

        page_tabs = ttk.Frame(self.root, padding=(12, 12, 12, 0))
        page_tabs.grid(row=0, column=0, sticky="ew")
        for column, page_name in enumerate(
            ("Main", "Measurements", "Display", "Log", "Settings")
        ):
            tab = ttk.Radiobutton(
                page_tabs,
                text=page_name,
                value=page_name,
                variable=self.page_var,
                command=self._show_page,
                style="Toolbutton",
            )
            tab.grid(row=0, column=column, padx=(0, 4))

        page_container = ttk.Frame(self.root)
        page_container.grid(row=1, column=0, sticky="nsew")
        page_container.columnconfigure(0, weight=1)
        page_container.rowconfigure(0, weight=1)
        self.pages: dict[str, ttk.Frame] = {}
        for page_name in ("Main", "Measurements", "Display", "Log", "Settings"):
            self.pages[page_name] = ttk.Frame(page_container)

        measurements_page = self.pages["Measurements"]
        measurements_page.columnconfigure(0, weight=1)
        measurements_page.rowconfigure(0, weight=1)
        measurements_panel = ttk.Frame(measurements_page, padding=24)
        measurements_panel.grid(row=0, column=0, sticky="nw")

        ttk.Label(
            measurements_panel,
            text="Laser",
            font=("Segoe UI", 14, "bold"),
        ).grid(row=0, column=0, columnspan=2, sticky="w")
        laser_switch = ttk.Frame(measurements_panel)
        laser_switch.grid(row=1, column=0, columnspan=2, pady=(8, 8), sticky="w")
        self.laser_switch_buttons = []
        for column, state in enumerate(("ON", "OFF")):
            button = ttk.Radiobutton(
                laser_switch,
                text=state,
                value=state,
                variable=self.laser_state_var,
                command=self._on_laser_state_change,
                style="Toolbutton",
            )
            button.grid(row=0, column=column)
            self.laser_switch_buttons.append(button)

        self.laser_power_scale = ttk.Scale(
            measurements_panel,
            from_=0,
            to=100,
            variable=self.laser_power_var,
            length=330,
            orient="horizontal",
            command=self._on_laser_power_change,
        )
        self.laser_power_scale.grid(
            row=2, column=0, columnspan=2, sticky="w"
        )
        ttk.Entry(
            measurements_panel,
            textvariable=self.laser_power_percent_var,
            state="readonly",
            justify="right",
            width=8,
        ).grid(row=3, column=0, columnspan=2, pady=(4, 0), sticky="w")

        ttk.Label(
            measurements_panel,
            text="Power consumption: Laser",
        ).grid(row=4, column=0, padx=(0, 8), pady=(8, 0), sticky="w")
        ttk.Entry(
            measurements_panel,
            textvariable=self.laser_power_consumption_var,
            state="readonly",
            width=14,
        ).grid(row=4, column=1, pady=(8, 0), sticky="e")

        ttk.Label(
            measurements_panel,
            text="System",
            font=("Segoe UI", 14, "bold"),
        ).grid(
            row=5,
            column=0,
            columnspan=2,
            pady=(32, 0),
            sticky="w",
        )
        ttk.Label(
            measurements_panel,
            text="Power consumption: System",
        ).grid(row=6, column=0, padx=(0, 8), pady=(8, 0), sticky="w")
        ttk.Entry(
            measurements_panel,
            textvariable=self.system_power_consumption_var,
            state="readonly",
            width=14,
        ).grid(row=6, column=1, pady=(8, 0), sticky="e")

        settings_page = self.pages["Settings"]
        settings_page.columnconfigure(0, weight=1)
        controls = ttk.Frame(settings_page, padding=12)
        controls.grid(row=0, column=0, sticky="nw")

        ttk.Label(controls, text="USB serial port").grid(
            row=0, column=0, padx=(0, 8), sticky="w"
        )
        self.port_combo = ttk.Combobox(
            controls, textvariable=self.port_var, width=18
        )
        self.port_combo.grid(row=0, column=1, padx=(0, 8), sticky="w")
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
            row=1, column=1, columnspan=4, padx=(0, 8), pady=(10, 0), sticky="w"
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
        self.connection_status_label = ttk.Label(
            controls, textvariable=self.status_var
        )
        self.connection_status_label.grid(
            row=3, column=0, columnspan=6, pady=(8, 0), sticky="w"
        )

        content_frame = self.pages["Main"]
        content_frame.grid(row=0, column=0, sticky="nsew")
        content_frame.columnconfigure(0, weight=1)
        content_frame.rowconfigure(0, weight=1)
        content_frame.rowconfigure(1, weight=0)

        self.graph_canvas = tk.Canvas(
            content_frame,
            width=920,
            height=500,
            background="white",
            highlightthickness=0,
        )
        self.graph_canvas.grid(row=0, column=0, padx=12, pady=(12, 0), sticky="nsew")
        self.graph_canvas.bind("<Configure>", self._draw_graph)
        self.graph_canvas.bind("<ButtonPress-1>", self._start_graph_pan)
        self.graph_canvas.bind("<B1-Motion>", self._pan_graph)
        self.graph_canvas.bind("<ButtonRelease-1>", self._stop_graph_pan)
        self.graph_canvas.bind("<MouseWheel>", self._on_graph_mousewheel)
        self.graph_canvas.bind("<Button-4>", self._on_graph_mousewheel)
        self.graph_canvas.bind("<Button-5>", self._on_graph_mousewheel)

        main_controls = ttk.Frame(content_frame, padding=(12, 0, 12, 12))
        main_controls.grid(row=1, column=0, sticky="ew")
        main_controls.columnconfigure(3, weight=1)

        mode_switch = ttk.Frame(main_controls)
        mode_switch.grid(row=0, column=0, padx=(0, 8), sticky="sw")
        ttk.Radiobutton(
            mode_switch,
            text="Single",
            value="Single",
            variable=self.mode_var,
            command=self._on_measurement_mode_change,
            style="Toolbutton",
        ).grid(row=0, column=0)
        ttk.Radiobutton(
            mode_switch,
            text="Auto",
            value="Auto",
            variable=self.mode_var,
            command=self._on_measurement_mode_change,
            style="Toolbutton",
        ).grid(row=0, column=1)

        logging_switch = ttk.Frame(main_controls)
        logging_switch.grid(row=0, column=1, padx=(0, 8), sticky="sw")
        self.logging_switch_buttons = []
        for column, option in enumerate(("Logging", "Continuous")):
            button = ttk.Radiobutton(
                logging_switch,
                text=option,
                value=option,
                variable=self.logging_var,
                command=self._on_logging_mode_change,
                style="Toolbutton",
            )
            button.grid(row=0, column=column)
            self.logging_switch_buttons.append(button)
        self._update_logging_switch()

        number_buttons = ttk.Frame(main_controls)
        number_buttons.grid(row=0, column=2, sticky="sw")
        self.number_buttons = []
        for column, (label, command) in enumerate(
            (("START", self._start_graph_logging), ("STOP", self._stop_graph_logging))
        ):
            button = ttk.Button(
                number_buttons,
                text=label,
                command=command,
                state="disabled",
            )
            button.grid(row=0, column=column, padx=(0, 4))
            self.number_buttons.append(button)
        self.trigger_button = ttk.Button(
            number_buttons,
            text="TRIGGER",
            state="disabled",
            command=self._trigger_graph,
        )
        self.trigger_button.grid(row=0, column=2, padx=(0, 4))
        self._update_trigger_button()
        ttk.Button(
            number_buttons,
            text="Clear Graph",
            command=self.clear_graph,
        ).grid(row=0, column=3, padx=(4, 0))

        distance_readout = ttk.Frame(main_controls)
        distance_readout.grid(row=0, column=3, sticky="e")
        ttk.Label(distance_readout, text="Distance (mm)").grid(
            row=0, column=0, padx=(0, 8)
        )
        ttk.Entry(
            distance_readout,
            textvariable=self.distance_var,
            state="readonly",
            justify="right",
            width=12,
        ).grid(row=0, column=1)
        ttk.Label(
            distance_readout,
            textvariable=self.graph_logging_status_var,
        ).grid(row=0, column=2, padx=(16, 0))

        log_page = self.pages["Log"]
        log_page.columnconfigure(0, weight=1)
        log_page.rowconfigure(0, weight=1)
        output_frame = ttk.Frame(log_page, padding=12)
        output_frame.grid(row=0, column=0, sticky="nsew")
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
        ttk.Button(log_page, text="Clear log", command=self.clear_output).grid(
            row=1, column=0, padx=12, pady=(0, 12), sticky="e"
        )

    @staticmethod
    def _tick_step(value_range: float, target_ticks: int = 10) -> float:
        raw_step = value_range / target_ticks
        magnitude = 10 ** math.floor(math.log10(raw_step))
        normalized_step = raw_step / magnitude
        if normalized_step <= 1:
            nice_step = 1
        elif normalized_step <= 2:
            nice_step = 2
        elif normalized_step <= 5:
            nice_step = 5
        else:
            nice_step = 10
        return nice_step * magnitude

    @staticmethod
    def _format_tick(value: float, step: float) -> str:
        if abs(value) < step * 1e-9:
            value = 0
        decimals = max(0, -math.floor(math.log10(step))) if step < 1 else 0
        return f"{value:.{decimals}f}"

    @staticmethod
    def _ticks(value_range: tuple[float, float], step: float) -> list[float]:
        low, high = value_range
        tick = math.ceil(low / step - 1e-10) * step
        ticks = []
        while tick <= high + step * 1e-10:
            ticks.append(0.0 if abs(tick) < step * 1e-10 else tick)
            tick += step
        return ticks

    def _draw_graph(self, event: tk.Event[tk.Misc] | None = None) -> None:
        width = self.graph_canvas.winfo_width()
        height = self.graph_canvas.winfo_height()
        if width <= 1 or height <= 1:
            return

        if (
            self.logging_var.get() == "Logging"
            and self._graph_logging_active
            and self._measurement_started_at is not None
        ):
            elapsed = time.monotonic() - self._measurement_started_at
            self.x_range = (0.0, max(10.0, math.ceil(elapsed / 10) * 10))
        elif self.logging_var.get() == "Continuous":
            self.x_range = (-self._continuous_window_seconds, 0.0)

        self.graph_canvas.delete("all")
        left, top, right, bottom = 72, 30, width - 28, height - 58
        if right <= left or bottom <= top:
            return

        self._graph_plot_bounds = left, top, right, bottom
        x_min, x_max = self.x_range
        y_min, y_max = self.y_range
        x_scale = (right - left) / (x_max - x_min)
        y_scale = (bottom - top) / (y_max - y_min)
        x_to_canvas = lambda value: left + (value - x_min) * x_scale
        y_to_canvas = lambda value: bottom - (value - y_min) * y_scale

        x_step = self._tick_step(x_max - x_min)
        y_step = self._tick_step(y_max - y_min)
        x_axis_y = bottom
        y_axis_x = left
        self._graph_axes = y_axis_x, x_axis_y

        for seconds in self._ticks(self.x_range, x_step):
            x = x_to_canvas(seconds)
            self.graph_canvas.create_line(
                x, top, x, bottom, fill="#d3d3d3", dash=(2, 4)
            )
            self.graph_canvas.create_line(
                x, x_axis_y, x, x_axis_y + 5, fill="#404040"
            )
            self.graph_canvas.create_text(
                x,
                x_axis_y + 16,
                text=self._format_tick(seconds, x_step),
                fill="#404040",
            )

        for distance in self._ticks(self.y_range, y_step):
            y = y_to_canvas(distance)
            self.graph_canvas.create_line(
                left, y, right, y, fill="#d3d3d3", dash=(2, 4)
            )
            self.graph_canvas.create_line(
                y_axis_x - 5, y, y_axis_x, y, fill="#404040"
            )
            self.graph_canvas.create_text(
                y_axis_x - 10,
                y,
                text=self._format_tick(distance, y_step),
                anchor="e",
                fill="#404040",
            )

        if self._measurement_started_at is not None:
            elapsed_now = (
                self._measurement_stopped_at
                if self._measurement_stopped_at is not None
                else time.monotonic()
            ) - self._measurement_started_at
            visible_samples = []
            for sample_time, distance in self._graph_samples:
                plot_time = (
                    sample_time - elapsed_now
                    if self.logging_var.get() == "Continuous"
                    else sample_time
                )
                if x_min <= plot_time <= x_max and y_min <= distance <= y_max:
                    visible_samples.append(
                        (x_to_canvas(plot_time), y_to_canvas(distance))
                    )
            for first, second in zip(visible_samples, visible_samples[1:]):
                self.graph_canvas.create_line(
                    *first, *second, fill="#1769aa", width=2
                )

        self.graph_canvas.create_line(
            y_axis_x, top, y_axis_x, bottom, fill="#404040", width=2
        )
        self.graph_canvas.create_line(
            left, x_axis_y, right, x_axis_y, fill="#404040", width=2
        )
        self.graph_canvas.create_text(
            (left + right) / 2,
            height - 15,
            text="Time (seconds)",
            fill="#202020",
        )
        self.graph_canvas.create_text(
            20,
            (top + bottom) / 2,
            text="Distance (mm)",
            angle=90,
            fill="#202020",
        )

    def _start_graph_pan(self, event: tk.Event[tk.Misc]) -> None:
        bounds = self._graph_plot_bounds
        if bounds is None:
            return
        left, top, right, bottom = bounds
        if left <= event.x <= right and top <= event.y <= bottom:
            self._graph_pan_start = (
                event.x,
                event.y,
                self.x_range,
                self.y_range,
            )

    def _pan_graph(self, event: tk.Event[tk.Misc]) -> None:
        if self._graph_pan_start is None or self._graph_plot_bounds is None:
            return
        start_x, start_y, start_x_range, start_y_range = self._graph_pan_start
        left, top, right, bottom = self._graph_plot_bounds
        x_span = start_x_range[1] - start_x_range[0]
        y_span = start_y_range[1] - start_y_range[0]
        x_shift = -(event.x - start_x) * x_span / (right - left)
        y_shift = (event.y - start_y) * y_span / (bottom - top)
        if self.logging_var.get() != "Continuous":
            self.x_range = (
                start_x_range[0] + x_shift,
                start_x_range[1] + x_shift,
            )
        self.y_range = self._clamp_y_range(
            start_y_range[0] + y_shift,
            start_y_range[1] + y_shift,
        )
        self._draw_graph()

    def _stop_graph_pan(self, event: tk.Event[tk.Misc]) -> None:
        self._graph_pan_start = None

    @staticmethod
    def _clamp_y_range(low: float, high: float) -> tuple[float, float]:
        max_distance = 1100.0
        span = high - low
        if span >= max_distance:
            return 0.0, max_distance
        if low < 0:
            high -= low
            low = 0.0
        if high > max_distance:
            low -= high - max_distance
            high = max_distance
        return max(0.0, low), min(max_distance, high)

    def _zoom_graph(
        self, axis: str, factor: float, canvas_x: float, canvas_y: float
    ) -> None:
        bounds = self._graph_plot_bounds
        if bounds is None:
            return
        left, top, right, bottom = bounds
        if axis == "x":
            if self.logging_var.get() == "Logging" and self._graph_logging_active:
                return
            low, high = self.x_range
            new_span = max(0.001, (high - low) * factor)
            if self.logging_var.get() == "Logging":
                self.x_range = (0.0, new_span)
            else:
                self._continuous_window_seconds = new_span
                self.x_range = (-new_span, 0.0)
        else:
            low, high = self.y_range
            anchor = high - (canvas_y - top) * (high - low) / (bottom - top)
            new_span = min(1100.0, max(0.001, (high - low) * factor))
            low = anchor - (anchor - low) * (new_span / (high - low))
            self.y_range = self._clamp_y_range(low, low + new_span)
        self._draw_graph()

    def _on_graph_mousewheel(self, event: tk.Event[tk.Misc]) -> str | None:
        bounds = self._graph_plot_bounds
        axes = self._graph_axes
        if bounds is None or axes is None:
            return None
        left, top, right, bottom = bounds
        y_axis_x, x_axis_y = axes
        axis = None
        if (
            left <= event.x <= right
            and bottom - 12 <= event.y <= bottom + 50
        ):
            axis = "x"
        elif (
            top <= event.y <= bottom
            and abs(event.x - y_axis_x) <= 12
        ):
            axis = "y"
        if axis is None:
            return None

        delta = getattr(event, "delta", 0)
        if getattr(event, "num", None) == 4:
            delta = 120
        elif getattr(event, "num", None) == 5:
            delta = -120
        if delta:
            self._zoom_graph(axis, 0.8 if delta > 0 else 1.25, event.x, event.y)
        return "break"

    def _start_graph_logging(self) -> None:
        if not self._connected:
            self.status_var.set("Connect to the STM32 before starting a measurement.")
            return
        if self._graph_logging_active:
            return

        command = "3" if self.mode_var.get() == "Single" else "1"
        self.send_command(command)
        self._measurement_started_at = time.monotonic()
        self._measurement_stopped_at = None
        self._active_measurement_mode = self.mode_var.get()
        self._graph_samples.clear()
        self._distance_line_buffer = ""
        self._graph_logging_active = True
        if self.logging_var.get() == "Logging":
            self.x_range = (0.0, 10.0)
        else:
            self.x_range = (-self._continuous_window_seconds, 0.0)
        self._update_graph_logging_status()
        self._update_measurement_buttons()
        self._draw_graph()

    def _stop_graph_logging(self, send_stop_command: bool = False) -> None:
        if not self._graph_logging_active:
            return
        if (
            self._active_measurement_mode == "Auto" or send_stop_command
        ) and self._connected:
            self.send_command("2")
        self._graph_logging_active = False
        self._measurement_stopped_at = time.monotonic()
        self._active_measurement_mode = None
        self._update_graph_logging_status()
        self._update_measurement_buttons()
        self._draw_graph()

    def _on_measurement_mode_change(self) -> None:
        if self._graph_logging_active:
            self._stop_graph_logging(send_stop_command=True)
        self._update_logging_switch()

    def _on_logging_mode_change(self) -> None:
        span = self.x_range[1] - self.x_range[0]
        if self.logging_var.get() == "Continuous":
            self.x_range = (-self._continuous_window_seconds, 0.0)
        else:
            elapsed = (
                time.monotonic() - self._measurement_started_at
                if self._measurement_started_at is not None
                else 0.0
            )
            self.x_range = (0.0, max(span, elapsed, 10.0))
        self._update_trigger_button()
        self._update_graph_logging_status()
        self._draw_graph()

    def _trigger_graph(self) -> None:
        if self.logging_var.get() != "Continuous" or self._last_distance_mm is None:
            return

        distance = self._last_distance_mm
        low = max(0.0, distance - 5.0)
        high = min(1100.0, distance + 5.0)
        if high - low < 10.0:
            if low == 0.0:
                high = 10.0
            else:
                low = 1090.0
        self.y_range = (low, high)
        self._draw_graph()

    def _on_laser_state_change(self) -> None:
        state = self.laser_state_var.get()
        if state == self._last_laser_state:
            return
        if not self._connected:
            self.status_var.set("Connect to the STM32 before changing the laser state.")
            self.laser_state_var.set(self._last_laser_state)
            return

        self.send_command("LASER_ON" if state == "ON" else "LASER_OFF")
        self._last_laser_state = state

    def _on_laser_power_change(self, value: str) -> None:
        percent = min(100.0, max(0.0, float(value)))
        self.laser_power_percent_var.set(f"{percent:g}%")
        power_value = round(percent * 512 / 100)
        self._cancel_laser_power_send()
        if power_value == self._last_laser_power_value:
            return
        if not self._connected:
            self.status_var.set("Connect to the STM32 before changing laser power.")
            return

        self._pending_laser_power_value = power_value
        self._laser_power_after_id = self.root.after(
            1000, self._send_pending_laser_power
        )

    def _cancel_laser_power_send(self) -> None:
        if self._laser_power_after_id is not None:
            self.root.after_cancel(self._laser_power_after_id)
            self._laser_power_after_id = None
        self._pending_laser_power_value = None

    def _send_pending_laser_power(self) -> None:
        self._laser_power_after_id = None
        power_value = self._pending_laser_power_value
        self._pending_laser_power_value = None
        if power_value is None or power_value == self._last_laser_power_value:
            return
        if not self._connected:
            return

        self.send_command(str(power_value))
        self._last_laser_power_value = power_value

    def _update_laser_controls(self) -> None:
        state = "normal" if self._connected else "disabled"
        if not self._connected:
            self._cancel_laser_power_send()
        for button in self.laser_switch_buttons:
            button.configure(state=state)
        self.laser_power_scale.configure(state=state)

    def _update_trigger_button(self) -> None:
        if not hasattr(self, "trigger_button"):
            return
        enabled = (
            self.logging_var.get() == "Continuous"
            and self._last_distance_mm is not None
        )
        self.trigger_button.configure(state="normal" if enabled else "disabled")

    def _update_graph_logging_status(self) -> None:
        self.graph_logging_status_var.set(
            "Graph: Logging" if self._graph_logging_active else "Graph: Not logging"
        )

    def _update_measurement_buttons(self) -> None:
        if not hasattr(self, "number_buttons"):
            return
        start_button, stop_button = self.number_buttons
        start_button.configure(
            state=(
                "normal"
                if self._connected and not self._graph_logging_active
                else "disabled"
            )
        )
        stop_button.configure(
            state=(
                "normal"
                if self._connected
                and self._graph_logging_active
                and self.mode_var.get() == "Auto"
                else "disabled"
            )
        )

    def _process_distance_data(self, text: str) -> None:
        self._distance_line_buffer += text
        lines = self._distance_line_buffer.split("\n")
        self._distance_line_buffer = lines.pop()
        if len(self._distance_line_buffer) > 4096:
            self._distance_line_buffer = self._distance_line_buffer[-4096:]

        if self._distance_line_buffer.rstrip("\r").endswith("mm"):
            lines.append(self._distance_line_buffer)
            self._distance_line_buffer = ""

        for line in lines:
            match = re.fullmatch(
                r"\s*(D|PL|PS):\s*(\d+(?:\.\d*)?|\.\d+)\s*mm\s*\r?",
                line,
                re.IGNORECASE,
            )
            if match:
                identifier = match.group(1).upper()
                value = float(match.group(2))
                if not 0 <= value <= 1100:
                    continue

                if identifier == "PL":
                    self.laser_power_consumption_var.set(f"{value:g}")
                elif identifier == "PS":
                    self.system_power_consumption_var.set(f"{value:g}")
                else:
                    self._last_distance_mm = value
                    self.distance_var.set(f"{value:g}")
                    self._update_trigger_button()
                    if (
                        self._graph_logging_active
                        and self._measurement_started_at is not None
                    ):
                        elapsed = time.monotonic() - self._measurement_started_at
                        self._graph_samples.append((elapsed, value))
                        if self.logging_var.get() == "Continuous":
                            window_start = elapsed - (
                                self.x_range[1] - self.x_range[0]
                            )
                            self._graph_samples = [
                                sample
                                for sample in self._graph_samples
                                if sample[0] >= window_start
                            ]
                        if self.mode_var.get() == "Single":
                            self._graph_logging_active = False
                            self._measurement_stopped_at = time.monotonic()
                            self._active_measurement_mode = None
                            self._update_graph_logging_status()
                            self._update_measurement_buttons()
                        self._draw_graph()

    def _update_logging_switch(self) -> None:
        state = "normal" if self.mode_var.get() == "Auto" else "disabled"
        for button in self.logging_switch_buttons:
            button.configure(state=state)
        self._update_measurement_buttons()

    def _show_page(self) -> None:
        selected_page = self.page_var.get()
        for page_name, page in self.pages.items():
            if page_name == selected_page:
                page.grid(sticky="nsew")
            else:
                page.grid_remove()

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
                self._graph_logging_active = False
                self._measurement_started_at = None
                self._measurement_stopped_at = None
                self._active_measurement_mode = None
                self._update_graph_logging_status()
                self._update_laser_controls()
                self.connect_button.configure(text="Disconnect", state="normal")
                self.command_entry.configure(state="normal")
                self.send_button.configure(state="normal")
                self._update_measurement_buttons()
                self.status_var.set(f"Connected to {value}")
            elif event == "data":
                self._process_distance_data(value)
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
                if self._graph_logging_active:
                    self._graph_logging_active = False
                    self._measurement_stopped_at = time.monotonic()
                self._active_measurement_mode = None
                self._stop_event = None
                self._update_graph_logging_status()
                self._update_laser_controls()
                self.connect_button.configure(text="Connect", state="normal")
                self.command_entry.configure(state="disabled")
                self.send_button.configure(state="disabled")
                self._update_measurement_buttons()
                if not self.status_var.get().startswith("Serial error:"):
                    self.status_var.set("Disconnected")

        if self._connected and self._graph_logging_active:
            elapsed = (
                time.monotonic() - self._measurement_started_at
                if self._measurement_started_at is not None
                else 0.0
            )
            if self.logging_var.get() == "Continuous" or elapsed > self.x_range[1]:
                self._draw_graph()
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

    def clear_graph(self) -> None:
        self._graph_samples.clear()
        self._draw_graph()

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