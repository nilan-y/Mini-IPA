"""Run command parser and sensor/USB integration tests without hardware."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
includes = [
    "Core/Inc", "USB_DEVICE/App", "USB_DEVICE/Target",
    "Drivers/STM32F0xx_HAL_Driver/Inc",
    "Drivers/CMSIS/Device/ST/STM32F0xx/Include", "Drivers/CMSIS/Include",
    "Middlewares/ST/STM32_USB_Device_Library/Core/Inc",
    "Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc",
    "Drivers/BSP/Components/vl53l4cd",
    "Drivers/BSP/Components/vl53l4cd/modules",
    "Drivers/BSP/Components/vl53l4cd/porting",
]
flags = ["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
         "-Wno-unused-parameter", "-DUSE_HAL_DRIVER", "-DSTM32F042x6"]
flags += ["-I" + str(root / "Code" / path) for path in includes]
groups = {
    "commands": ["Core/Src/usb_commands.c", "Tests/test_usb_commands.c"],
    "sensor_usb": ["Core/Src/usb_commands.c", "Core/Src/sensor_app.c",
                   "USB_DEVICE/App/usbd_cdc_if.c", "Tests/test_sensor_usb.c"],
}
with tempfile.TemporaryDirectory(prefix="mini-ipa-tests-") as directory:
    for name, sources in groups.items():
        executable = str(Path(directory) / name)
        subprocess.run(flags + [str(root / "Code" / p) for p in sources]
                       + ["-o", executable], check=True)
        subprocess.run([executable], check=True)
