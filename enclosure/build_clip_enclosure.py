"""NiceDeck two-part clip case. Reuses four board screws and the v1 desk cradle.
Millimetres; original glass datum Z0, revised glass/front face Z-3.2.
Run with enclosure/.venv/bin/python enclosure/build_clip_enclosure.py.
"""
from pathlib import Path
import json, math
import cadquery as cq
import trimesh
ROOT=Path(__file__).resolve().parent
OUT=ROOT/'output-clip-v3'; OUT.mkdir(exist_ok=True)
W,H,R=58.,78.,5.
FRONT,JOIN,BACK=-3.2,17.6,20.
SHIFT=-3.2
MOUNT=9.7+SHIFT
USB_Z=8.37+SHIFT
MOUNTS=[(x,y) for x in [-14.5,14.5] for y in [-21.31,20.89]]
REFERENCE=ROOT/'reference/esp32-s3-touch-lcd-2_20241108.stp'
def box(w,h,d,z=0,x=0,y=0):
    return cq.Workplane('XY').box(w,h,d,centered=(True,True,False)).translate((x,y,z))
def rr(w,h,r,z,d):return box(w,h,d,z).edges('|Z').fillet(r)
def cyl(x,y,r,z,h):return cq.Workplane('XY').center(x,y).circle(r).extrude(h).translate((0,0,z))
def desktop(s):return s.rotate((0,0,0),(1,0,0),115).rotate((0,0,0),(0,0,1),180).translate((0,0,65))
board=cq.importers.importStep(str(REFERENCE)).val().rotate((0,0,0),(0,1,0),180).translate((0,1.49,3.6+SHIFT))
# Same external envelope as the user's printed stand. No case screws/inserts.
shell=rr(W,H,R,FRONT,JOIN-FRONT)
shell=shell.cut(rr(38.,59.7,2.9,FRONT-.1,4.-FRONT+.2))
shell=shell.cut(rr(50.,70.,4.,1.4,JOIN-1.4+.2))
shell=shell.cut(box(16.,36.,12.,USB_Z-6.,y=-41.))
for x in [-9.46,9.49]:shell=shell.cut(box(4.2,14.,4.6,6.15+SHIFT,x=x,y=-33.))
# Small side windows accept hooks; squeeze the exposed polymer hooks inward
# after removing the pod from the stand to release the carrier.
for sign in [-1,1]:shell=shell.cut(box(5.,10.2,3.15,2.3,x=sign*27.2))
shell=shell.clean()

def make_carrier(screw_length):
    seat=MOUNT+screw_length-1.7
    c=rr(W,H,R,JOIN,BACK-JOIN)
    for x,y in MOUNTS:
        c=c.union(cyl(x,y,3.1,MOUNT,JOIN-MOUNT+.01))
        c=c.cut(cyl(x,y,1.2,MOUNT-.1,BACK-MOUNT+.2))
        c=c.cut(cyl(x,y,2.3,seat,BACK-seat+.1))
    for x in [-4.,4.]:c=c.cut(box(2.,14.,4.,JOIN-.5,x=x,y=13.))
    # Two 1.2mm-thick cantilevers. Hook overlap is 0.55mm; shoulder is
    # 12.4mm from its root, keeping nominal assembly bending strain low.
    for sign in [-1,1]:
        beam=box(1.2,9.,JOIN-2.4+.1,2.4,x=sign*23.8)
        # XZ workplane extrusion goes in -Y. Center the hook on Y=0.
        profile=[(24.3,2.4),(24.4,2.4),(25.55,4.5),(25.55,5.2),(24.3,5.2)]
        hook=cq.Workplane('XZ').polyline(profile).close().extrude(9.).translate((0,4.5,0))
        if sign<0:hook=hook.mirror('YZ',union=False)
        c=c.union(beam).union(hook)
        gusset=cq.Workplane('XZ').polyline([(22.,JOIN),(23.3,JOIN-2.),(23.3,JOIN+.1)]).close().extrude(9.).translate((0,4.5,0))
        if sign<0:gusset=gusset.mirror('YZ',union=False)
        c=c.union(gusset)
        for y in [-26.,26.]:c=c.union(box(1.2,8.,2.7,15.,x=sign*24.1,y=y))
    c=c.union(box(12.,1.2,2.7,15.,y=33.9))
    for x in [-17.,17.]:c=c.union(box(4.,1.2,2.7,15.,x=x,y=-33.9))
    # Variant label is recessed on the back; choose by actual under-head length.
    label=cq.Workplane('XY').text('M2x'+str(screw_length),3.,.5,combine=False).translate((0,30.,BACK-.3))
    c=c.cut(label)
    return c.clean()
carriers={n:make_carrier(n) for n in [10,12]}
stand=cq.importers.importStep(str(ROOT/'output/desk_cradle.step'))
parts={'shell':shell,**{'carrier_m2x'+str(n):c for n,c in carriers.items()}}
# A matching pair of side strips tests the actual clip engagement at much less
# cost than a full case. No electronics are fitted to this latch-only coupon.
clip_region=box(10.,14.,30.,FRONT-1,x=24.)
parts['fit_clip_shell']=shell.intersect(clip_region).clean()
parts['fit_clip_carrier']=carriers[10].intersect(clip_region).clean()
# Lay a screw in either open-ended channel, with the underside of its head
# against the left edge. A 10mm screw reaches the 10 stop; a 12mm screw only
# fits the longer channel. This is a coarse selection gauge, not metrology.
gauge=box(17.,14.,2.4,0,x=8.5)
for length,y in [(10,-3.5),(12,3.5)]:
    gauge=gauge.cut(box(length+.3,2.6,2.,.8,x=(length+.3)/2-.05,y=y))
    label=cq.Workplane('XY').text(str(length),2.3,.5,combine=False).translate((14.7,y,2.1))
    gauge=gauge.cut(label)
parts['screw_length_gauge']=gauge.clean()
report={'units':'mm','status':'FIT PROTOTYPE — screw length and physical clip fit must be checked',
 'glass_face_z':SHIFT,'shell_front_z':FRONT,'glass_edge_clearance_mm':.45,
 'outside_mm':[W,H,BACK-FRONT],'reuse_original_stand':True,
 'hardware':'Four M2 board screws only; choose the carrier for measured 10mm or 12mm under-head length.',
 'mount_contact_z':MOUNT,'m2_nominal_engagement_mm':1.7,'m2_head_pocket_diameter_mm':4.6,
 'usb_opening_mm':[16.,12.],'assumed_plug_envelope_mm':[14.,10.,24.],
 'clip':{'count':2,'beam_thickness_mm':1.2,'beam_width_mm':9.,'root_to_shoulder_mm':12.4,'hook_overlap_mm':.55,'axial_clearance_mm':.25,
         'nominal_bending_strain_estimate':3*1.2*.55/(2*12.4**2),'note':'Linear cantilever estimate only; layer adhesion, creep and stress concentrations are not proven.'},
 'variants':{},'parts':{},'intersections_mm3':{}}
for name,p in parts.items():
    orient=p.rotate((0,0,0),(1,0,0),180) if 'carrier' in name else p
    bb=orient.val().BoundingBox();orient=orient.translate((-bb.xmin,-bb.ymin,-bb.zmin))
    cq.exporters.export(orient,str(OUT/(name+'.stl')),tolerance=.04,angularTolerance=.12)
    cq.exporters.export(p,str(OUT/(name+'.step')))
    mesh=trimesh.load(OUT/(name+'.stl'),force='mesh')
    report['parts'][name]={'watertight':bool(mesh.is_watertight),'winding_consistent':bool(mesh.is_winding_consistent),'bodies':len(mesh.split()),'cad_valid':bool(p.val().isValid()),'bounds_mm':mesh.extents.tolist(),'volume_mm3':float(mesh.volume)}
    assert mesh.is_watertight and mesh.is_winding_consistent and len(mesh.split())==1 and mesh.volume>0 and p.val().isValid(),name
    assert max(mesh.extents)<256,name
for n,c in carriers.items():
    seat=MOUNT+n-1.7
    report['variants'][str(n)]={'screw_length_mm':n,'grip_mm':seat-MOUNT,'head_seat_z':seat,'head_recess_mm':BACK-seat}
    report['intersections_mm3']['board_vs_carrier_'+str(n)]=abs(board.intersect(c.val()).Volume())
    report['intersections_mm3']['shell_vs_carrier_'+str(n)]=abs(shell.val().intersect(c.val()).Volume())
    report['intersections_mm3']['carrier_vs_stand_'+str(n)]=abs(desktop(c).val().intersect(stand.val()).Volume())
report['intersections_mm3']['board_vs_shell']=abs(board.intersect(shell.val()).Volume())
report['intersections_mm3']['shell_vs_stand']=abs(desktop(shell).val().intersect(stand.val()).Volume())
plug=box(14.,24.,10.,USB_Z-5.,y=-25.9-12.)
report['intersections_mm3']['plug_vs_shell']=abs(plug.val().intersect(shell.val()).Volume())
report['intersections_mm3']['plug_vs_carrier']=abs(plug.val().intersect(carriers[10].val()).Volume())
report['intersections_mm3']['plug_vs_stand']=abs(desktop(plug).val().intersect(stand.val()).Volume())
for key,value in report['intersections_mm3'].items():assert value<.001,(key,value)
# During rear insertion only the flexible hooks are allowed to touch the shell.
# Check a rigid clearance envelope with those hooks removed, plus the full board.
rigid_carrier=carriers[10]
for sign in [-1,1]:rigid_carrier=rigid_carrier.cut(box(3.,10.,3.2,2.2,x=sign*25.8))
insertion_max=0.
for offset in [i*.5 for i in range(49)]:
    assembly_body=cq.Compound.makeCompound([board.translate((0,0,offset)),rigid_carrier.val().translate((0,0,offset))])
    volume=abs(shell.val().intersect(assembly_body).Volume())
    insertion_max=max(insertion_max,volume)
    assert volume<.001,('rigid_insertion',offset,volume)
report['rear_insertion']={'checked_offsets_mm':[0,24],'step_mm':.5,'max_unintended_intersection_mm3':insertion_max,'flexible_hook_contact_excluded':True}
assembly=cq.Assembly(name='NiceDeck_four_screw_clip_case')
assembly.add(desktop(board),name='vendor_board_reference',color=cq.Color(.04,.12,.14))
assembly.add(desktop(shell),name='shell',color=cq.Color(.85,.81,.74))
assembly.add(desktop(carriers[10]),name='carrier_m2x10',color=cq.Color(.3,.32,.33))
assembly.add(stand,name='reuse_original_stand',color=cq.Color(.15,.16,.18))
assembly.save(str(OUT/'assembly.step'))
(OUT/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2),flush=True)
