# What needs a real controller

Everything this package does sits above SDL's drivers: the virtual joystick replaces the device, not SDL's view of it. These can
only be checked with real hardware in someone's hands:

- the HID reports themselves: a DualSense or DualShock 4 over USB and Bluetooth, a Switch Pro, and SDL's HIDAPI drivers
  turning them into buttons (the virtual device skips straight to SDL's standard layout)
- Windows' own stacks: XInput, DirectInput, Windows.Gaming.Input and RawInput, and what each reports for the same pad
  (a trigger as an axis or a button, a d-pad as a hat or buttons, GUIDs, dead zones); the Xbox profile uses the IDs SDL's XInput
  driver reports, not the driver
- generic DirectInput pads whose axes and hats aren't where the port's fallback expects them
- rumble and every other output: the harness sees the call SDL makes, not a motor turning; the same for LEDs, adaptive
  triggers and gyro
- Bluetooth pairing, latency, a real cable pull, wake from sleep
- Steam Input, controller overlays and remapping tools between the pad and the game
- focus: the window being active while a real pad is used (virtual joystick events follow SDL's focus rule too, so that
  is covered, but an overlay taking focus is not)
