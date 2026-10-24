"""Carrier-only revision for the photographed approximately 10mm M2 screw.
Preserve the proven outer fit and mounting plane; widen the head pockets.
Run with enclosure/.venv/bin/python enclosure/build_screw_reach_fix.py.
"""
from pathlib import Path
import json
import cadquery as cq
import trimesh

ROOT = Path(__file__).resolve().parent
OUT = ROOT / 'output-screw-fix-v3b'
OUT.mkdir(exist_ok=True)
MOUNT, SEAT, BACK = 6.5, 14.8, 20.0
HEAD_DIAMETER = 5.2
MOUNTS = [(x, y) for x in (-14.5, 14.5) for y in (-21.31, 20.89)]
old = cq.importers.importStep(str(ROOT / 'output-clip-v3/carrier_m2x10.step'))
carrier = old
for x, y in MOUNTS:
    pocket = cq.Workplane('XY').center(x, y).circle(HEAD_DIAMETER / 2).extrude(BACK - SEAT + .1).translate((0, 0, SEAT))
    carrier = carrier.cut(pocket)
    # Lead-in only in the rear plate, so the tall post retains its full OD.
    lead = cq.Solid.makeCone(2.6, 2.9, .5, cq.Vector(x, y, BACK - .5))
    carrier = carrier.cut(lead)
carrier = carrier.clean()

# One mounting post with the identical shaft hole, head seat and rear plate.
# Optional inexpensive reach check, before committing to the full carrier.
x, y = MOUNTS[0]
region = cq.Workplane('XY').box(10, 10, 15, centered=(True, True, False)).translate((x, y, MOUNT))
coupon = carrier.intersect(region).clean()
parts = {'NiceDeck-carrier-10mm-screw-fix': carrier, 'optional-one-screw-fit-check': coupon}
report = {'units': 'mm', 'screw_under_head_length_assumed': 10, 'photo_measurement': 'Approximately 10mm, not a calibrated measurement',
          'shaft_clearance_diameter': 2.4, 'head_pocket_diameter': HEAD_DIAMETER,
          'grip_length': SEAT-MOUNT, 'nominal_thread_engagement': 10-(SEAT-MOUNT),
          'rear_head_recess_depth': BACK-SEAT, 'post_outer_diameter': 6.2,
          'minimum_post_wall_at_head_pocket': (6.2-HEAD_DIAMETER)/2,
          'change_vs_m2x12': 'Head seat 2mm closer to board',
          'change_vs_m2x10': 'Head pocket widened 4.6 to 5.2mm; rear lead-in added; seat unchanged',
          'physical_fit': 'Unverified until fitted to actual screw and board', 'parts': {}}
added = abs(carrier.val().cut(old.val()).Volume())
assert added < .001, added
report['material_added_vs_previous_m2x10_mm3'] = added
report['material_removed_mm3'] = old.val().Volume() - carrier.val().Volume()
for name, part in parts.items():
    cq.exporters.export(part, str(OUT / (name+'.step')))
    oriented = part.rotate((0,0,0), (1,0,0), 180)
    b = oriented.val().BoundingBox()
    oriented = oriented.translate((-b.xmin, -b.ymin, -b.zmin))
    cq.exporters.export(oriented, str(OUT / (name+'.stl')), tolerance=.035, angularTolerance=.1)
    mesh = trimesh.load(OUT / (name+'.stl'), force='mesh')
    checks = {'cad_valid': part.val().isValid(), 'watertight': bool(mesh.is_watertight),
              'consistent_winding': bool(mesh.is_winding_consistent), 'bodies': len(mesh.split()),
              'bounds_mm': mesh.extents.tolist(), 'volume_mm3': float(mesh.volume)}
    assert all(checks[k] for k in ('cad_valid','watertight','consistent_winding'))
    assert checks['bodies']==1 and mesh.volume>0 and max(mesh.extents)<256
    report['parts'][name]=checks
(OUT/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
