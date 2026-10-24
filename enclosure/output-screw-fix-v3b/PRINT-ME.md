# NiceDeck — 10 mm screw carrier correction

Slice `NiceDeck-carrier-10mm-screw-fix.stl` for your own printer.

This replaces only the rear carrier. Reuse the existing clip shell and stand.

Bambu A1, 0.4 mm nozzle, Generic PLA, textured PEI plate, 0.2 mm layers, 3 walls, 15% gyroid, supports off. Place the rear face flat on the bed. Bambu Studio estimates **38 minutes and 11.9 g** including its configured preparation time. Choose your actual filament/plate in Bambu Studio before printing if different.

The photo suggests an approximately 10 mm screw measured from the underside of its head to its tip. It is an estimate from the ruler/photo, not a calibrated measurement. The design uses 8.3 mm of plastic beneath the screw head, leaving nominally 1.7 mm to engage the board's metal mounting pillar.

Changes:
- Head seat is 2 mm deeper than the earlier M2x12 carrier.
- Head pocket is widened from 4.6 to 5.2 mm, with a wider lead-in.
- Head seat matches the earlier M2x10 carrier; that variant's improvement is head clearance.
- Screen position, mounting contacts, clips, USB clearance and external fit are preserved.

Before fitting the board, drop a screw into a hole and check that its head reaches the bottom of the recess. Approximately 1.7 mm of threaded shaft should protrude beyond the end of the printed mounting post. Attach the carrier to the board's metal mounting pillars, starting all four screws loosely before tightening gently. The screws engage those pillars, not the glass. Stop if a screw becomes tight while the parts are still loose.


Validation: both meshes are watertight, single-body and CAD-valid. The carrier is a material-removal-only change to the previous M2x10 model, so it introduces no new collision envelope. Both Bambu slices completed successfully with no reported warnings. Physical screw fit still needs checking against the actual printed part and screw.
