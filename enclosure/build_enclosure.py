"""Waveshare ESP32-S3-Touch-LCD-2 v1 enclosure. Millimetres.
Run with enclosure/.venv/bin/python enclosure/build_enclosure.py.
Canonical axes: X across glass, Y up glass, Z behind glass; lens centre XY0,
front glass Z0. Vendor transform: rotate180 about X then rotate180 about Z,
or explicitly mirror Y? We use rotate180 about Y (flips X,Z) plus Y+1.49,
Z+3.6. The assembly is left-right symmetric except component/button positions.
"""
from pathlib import Path
import json, math
import cadquery as cq
import trimesh

ROOT=Path(__file__).resolve().parent
OUT=ROOT/'output'; OUT.mkdir(exist_ok=True)
REFERENCE=ROOT/'reference/esp32-s3-touch-lcd-2_20241108.stp'
W,H,R=58.,78.,5.
FRONT, LIP_BACK, JOIN, BACK=-3.2,-0.4,17.6,20.
BOARD_HOLES=[(x,y) for x in [-14.5,14.5] for y in [-21.31,20.89]]
CASE_HOLES=[(x,y) for x in [-24.,24.] for y in [-33.,33.]]
USB_Z=8.37

# Rotate about Y flips vendor X and depth. Y stays upright, lens centre offset.
def canonical(s): return s.rotate((0,0,0),(0,1,0),180).translate((0,1.49,3.6))
def box(w,h,d,z=0,x=0,y=0):
    return cq.Workplane('XY').box(w,h,d,centered=(True,True,False)).translate((x,y,z))
def rr(w,h,r,z,d):
    return box(w,h,d,z).edges('|Z').fillet(r)
def cyl(x,y,r,z,h):
    return cq.Workplane('XY').center(x,y).circle(r).extrude(h).translate((0,0,z))
def hexhole(x,y,af,z,h):
    return cq.Workplane('XY').center(x,y).polygon(6,af/math.cos(math.pi/6)).extrude(h).translate((0,0,z))
def solid(v): return v.val() if hasattr(v,'val') else v

def desktop(v):
    # canonical Y is screen-up, Z points behind glass: 65deg from desk.
    return v.rotate((0,0,0),(1,0,0),115).rotate((0,0,0),(0,0,1),180).translate((0,0,65))

board=canonical(cq.importers.importStep(str(REFERENCE)).val())
# Front shell prints bezel-down. 0.45 mm radial glass pocket allowance.
frame=rr(W,H,R,FRONT,JOIN-FRONT)
frame=frame.cut(rr(36.0,57.7,2.0,FRONT-.1,4.0))
frame=frame.cut(rr(38.0,59.7,2.9,LIP_BACK,5.0))
frame=frame.cut(rr(50.,70.,4.,4.,JOIN-4.+.2))
for x,y in CASE_HOLES:
    # Continuous screw towers and open-front captive M3 nut pockets.
    frame=frame.union(cyl(x,y,4.1,FRONT,JOIN-FRONT))
    frame=frame.cut(cyl(x,y,1.7,FRONT-.1,JOIN-FRONT+.2))
    frame=frame.cut(hexhole(x,y,5.8,FRONT-.1,3.-FRONT+.1))
# Direct cable well: clear 16x12 mm; insertion direction stays in board plane.
usb_clear=box(16.,36.,12.,USB_Z-6.,y=-41.)
frame=frame.cut(usb_clear)
# Edge-facing BOOT and RESET access, measured from source STEP; holes not buttons.
for x in [-9.46,9.49]:
    frame=frame.cut(box(4.2,14.,4.6,6.15,y=-33.,x=x))
frame=frame.clean()

# Rear cover prints exterior-down: standoffs grow vertically off the plate.
cover=rr(W,H,R,JOIN,BACK-JOIN)
for x,y in BOARD_HOLES:
    cover=cover.union(cyl(x,y,2.7,9.7,JOIN-9.7+.01))
    cover=cover.cut(cyl(x,y,1.2,9.6,BACK-9.6+.1))
for x,y in CASE_HOLES:
    cover=cover.cut(cyl(x,y,1.7,JOIN-.1,BACK-JOIN+.2))
# Two narrow rear ventilation slots avoid the header-pin envelopes.
for x in [-4.,4.]: cover=cover.cut(box(2.,14.,4.,JOIN-.5,x=x,y=13.))
cover=cover.clean()

# Separate cradle. The board pod slides down into two open inclined channels.
# Large central opening leaves the plug, strain relief and cable unconstrained.
base=box(68.,74.,3.2,0,y=5.)
base=base.cut(box(22.,48.,4.,-.1,y=-17.))
profile=[(-32.,0.),(42.,0.),(35.,65.),(-10.,65.)]
rail=cq.Workplane('YZ').polyline(profile).close().extrude(8.)
stand=base.union(rail.translate((-34.,0,0))).union(rail.translate((26.,0,0)))
slot=desktop(box(W+.8,210.,BACK-FRONT+.8,FRONT-.4,y=65.6)) # bottomY=-39.4
stand=stand.cut(slot)
# Clearance for the real M3 socket-head screws projecting behind the rear cover.
for x in [-24.,24.]:
    stand=stand.cut(desktop(box(7.,210.,4.,20.,x=x,y=65.6)))
stand=stand.clean()

# Cheap separate coupon: one rear mounting skeleton tests all 4 M2 positions,
# pin clearance and screw engagement; one lower bezel fragment tests USB access.
mount_coupon=box(36.,3.,2.4,JOIN,y=-21.31).union(box(36.,3.,2.4,JOIN,y=20.89))
mount_coupon=mount_coupon.union(box(3.,45.2,2.4,JOIN,x=-16.5,y=-.21)).union(box(3.,45.2,2.4,JOIN,x=16.5,y=-.21))
for x,y in BOARD_HOLES:
    mount_coupon=mount_coupon.union(cyl(x,y,2.7,9.7,BACK-9.7))
    mount_coupon=mount_coupon.cut(cyl(x,y,1.2,9.6,BACK-9.6+.1))
mount_coupon=mount_coupon.clean()
usb_coupon=frame.intersect(box(42.,18.,30.,FRONT-1,y=-30.)).clean()

parts={'front_shell':frame,'rear_cover':cover,'desk_cradle':stand,
       'fit_mounts':mount_coupon,'fit_usb_opening':usb_coupon}
# Every STL is already arranged with its intended print face on Z=0.
print_parts={}
for name,p in parts.items():
    if name in ['rear_cover','fit_mounts']:
        p=p.rotate((0,0,0),(1,0,0),180)
    b=p.val().BoundingBox(); p=p.translate((-b.xmin,-b.ymin,-b.zmin))
    cq.exporters.export(p,str(OUT/(name+'.stl')),tolerance=.04,angularTolerance=.12)
    cq.exporters.export(parts[name],str(OUT/(name+'.step')))
    print_parts[name]=p

assembly=cq.Assembly(name='OpenStreamDeck_enclosure_v1')
assembly.add(desktop(board),name='vendor_board_reference',color=cq.Color(.05,.35,.40))
assembly.add(desktop(frame),name='front_shell',color=cq.Color(.12,.13,.15))
assembly.add(desktop(cover),name='rear_cover',color=cq.Color(.20,.21,.23))
assembly.add(stand,name='desk_cradle',color=cq.Color(.12,.13,.15))
assembly.save(str(OUT/'assembly.step'))
# CAD-derived SVG previews, not generative illustrations.
assembled=cq.Compound.makeCompound([solid(desktop(frame)),solid(desktop(cover)),solid(stand)])
cq.exporters.export(assembled,str(OUT/'assembly.svg'),opt={'width':1000,'height':900,'projectionDir':(1,-2,1),'showHidden':False,'strokeWidth':.4})

report={'units':'mm','status':'PROTOTYPE — physical fit test required',
 'source_step':REFERENCE.name,'glass_mm':[37.1,58.8], 'glass_front_z':0.,
 'usb_mouth':{'x':0.,'y':-25.5033,'z':USB_Z,'direction':[0,-1,0]},
 'usb_opening_mm':[16.,12.], 'assumed_max_plug_body_mm':[14.,10.,24.],
 'note_plug':'Maximum housing width/thickness include clearance; actual plug still needs fit coupon. No right-angle adapter assumed.',
 'mount_holes_xy':BOARD_HOLES,'boss_contact_depth':9.7,'header_max_depth':15.5,
 'rear_inner_depth':JOIN,'header_clearance':JOIN-15.5,
 'm2_screw_length':12.,'m2_grip':BACK-9.7,'m2_nominal_engagement':12.-(BACK-9.7),
 'front_glass_clearance':.4,'glass_side_clearance':.45,
 'parts':{},'intersections_mm3':{}}
for name in parts:
    mesh=trimesh.load(OUT/(name+'.stl'),force='mesh')
    report['parts'][name]={'watertight':bool(mesh.is_watertight),'winding_consistent':bool(mesh.is_winding_consistent),'bodies':len(mesh.split()),'bounds_mm':mesh.extents.tolist(),'volume_mm3':float(mesh.volume),'cad_valid':bool(parts[name].val().isValid())}
    assert mesh.is_watertight and mesh.is_winding_consistent and len(mesh.split())==1 and mesh.volume>0,name
    assert max(mesh.extents)<256,name
# Exact solid collisions, including all 313 solids in the vendor model.
for name in ['front_shell','rear_cover','fit_mounts','fit_usb_opening']:
    intersection=board.intersect(parts[name].val())
    volume=abs(intersection.Volume())
    report['intersections_mm3']['board_vs_'+name]=volume
    assert volume<.001,(name,volume)
report['intersections_mm3']['frame_vs_cover']=abs(frame.val().intersect(cover.val()).Volume())
report['intersections_mm3']['pod_vs_stand']=abs(cq.Compound.makeCompound([solid(desktop(frame)),solid(desktop(cover))]).intersect(stand.val()).Volume())
# Test oversized plug envelope's unobstructed straight insertion through shell.
plug=box(14.,24.,10.,USB_Z-5.,y=-25.9-12.)
report['intersections_mm3']['plug_envelope_vs_shell']=abs(plug.val().intersect(frame.val()).Volume())
report['intersections_mm3']['plug_envelope_vs_stand']=abs(desktop(plug).val().intersect(stand.val()).Volume())
heads=cq.Compound.makeCompound([cyl(x,y,2.75,20.,3.).val() for x,y in CASE_HOLES]+[cyl(x,y,1.9,20.,2.).val() for x,y in BOARD_HOLES])
report['intersections_mm3']['screw_heads_vs_stand']=abs(desktop(heads).intersect(stand.val()).Volume())
for key,val in report['intersections_mm3'].items(): assert val<.001,(key,val)
(OUT/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2),flush=True)
