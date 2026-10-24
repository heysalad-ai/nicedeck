# NiceDeck enclosure: v3 shell and corrected v3b carrier

Print one `output-clip-v3/shell.stl`, one `output-screw-fix-v3b/NiceDeck-carrier-10mm-screw-fix.stl`, and optionally `output/desk_cradle.stl`. STEP exports and editable CadQuery generators are included. The earlier `carrier_m2x10.step` is retained as an input to the v3b generator, not the recommended print.

Use four board screws measured at approximately 10 mm from under the head to the tip. The carrier provides 8.3 mm nominal grip length, leaving 1.7 mm engagement. Verify your actual screws before assembly. No additional case screws or inserts are required; two printed clips retain the shell.

Suggested starting profile: Bambu A1, 0.4 mm nozzle, PLA, 0.2 mm layers, 3 walls, 15% gyroid. Slice the STLs for your printer at 100% scale. Check orientation and support needs in your slicer; no printer-specific G-code is distributed.

Unplug the board. Seat its metal mounting pillars on the carrier posts. Start all four screws loosely through the rear recesses, then tighten gently. Screws engage the metal pillars, never the glass. Insert the assembly through the shell rear with USB-C toward the bottom opening, then engage both side clips without pressing on the screen. Confirm USB seating, glass clearance and touch operation before use.

The final v3b fit and clip durability are not physically verified. This is not a waterproof enclosure. If screws become tight before the parts are secured, stop and check their reach. See [carrier notes](output-screw-fix-v3b/PRINT-ME.md).

To regenerate, install `enclosure/requirements.txt` into a virtual environment. Obtain the manufacturer model from the URL in `reference/source.json` and put it at `reference/esp32-s3-touch-lcd-2_20241108.stp`. Run `build_enclosure.py`, `build_clip_enclosure.py`, then `build_screw_reach_fix.py` from the repository root. The manufacturer's model is not included or covered by the project licence.
