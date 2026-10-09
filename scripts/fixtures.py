"""Generate redistributable PMX 2.1 regression assets; no third-party model data."""
import pathlib,struct,math
ROOT=pathlib.Path(__file__).resolve().parents[1]
def pack(fmt,*v):return struct.pack('<'+fmt,*v)
def text(s):
 b=s.encode('utf-8');return pack('i',len(b))+b
def vec(*v):return pack('f'*len(v),*v)
def png(width,height,rgba):
 """A minimal RGBA PNG (stdlib only)."""
 import zlib
 rows=b''.join(b'\0'+rgba[y*width*4:(y+1)*width*4] for y in range(height))
 def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
 return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b'')
def make(rope=False,cycle=False,texture=False,humanoid=False,joint=(0,1),material_count=2,chain=0,chain_damping=.5,chain_mass_ratio=1,chain_masses=None,chain_locked=False,extra_vertices=0,bad='',morph_vertex=3,morph_offset=1,island=False,texture_name='checker.dds',group_chain=0,group_lattice=0,control_root=False):
 # bad: one non-finite or runaway value per section. The loader repairs them (soft bodies are
 # rejected); the repair kinds mirror released models: a NaN vertex, NaN normal, BDEF4 weights
 # of (1, 1, 1, -2) on one bone, a bone parented to itself, a 1e21 kg anchor, a 1e14 spring.
 nan=float('nan');inf=float('inf')
 b=bytearray(b'PMX '+pack('fB',2.1,8)+bytes([1,1,4,4,4,4,4,4]))
 b+=text('MMDHL regression rope' if rope else 'MMDHL regression cloth')+text('Generated fixture')+text('CC0 procedural test geometry')+text('')
 bones=[('root',(0,0,0),1 if cycle else 0 if bad=='self_parent' else -1),('link',(0,5,0),0),('effector',(0,10,0),1),('IK',(2,9,0),0)]
 if humanoid:
  # Keep the authored cloth rig and add an independent standard primary skeleton.
  # Its pelvis drives the cloth root so moving-attachment feedback is exercised.
  bones[0]=('root',(0,0,0),4)
  bones += [('lower body',(0,9,0),-1),('upper body',(0,10,0),4),('upper body2',(0,13,0),5),('neck',(0,16,0),6),('head',(0,17,0),7)]
  for side,sign in [('left',1),('right',-1)]:
   parent=6
   for name,x,y in [('shoulder',1,14),('arm',2,14),('elbow',5,13),('wrist',8,12)]:
    bones.append((side+' '+name,(x*sign,y,0),parent));parent=len(bones)-1
   parent=4
   for name,y,z in [('leg',9,0),('knee',5,0),('ankle',1,0),('toe',.5,-1)]:
    bones.append((side+' '+name,(sign,y,z),parent));parent=len(bones)-1
  # A rigid hair chain hanging from the head: kinematic follower plus spring-jointed capsules, no soft body.
  parent=8
  for k in range(chain):
   bones.append(('hair'+str(k),(0,17-(k+1)*1.2,.3),parent));parent=len(bones)-1
 hair=len(bones)-chain
 # IK constraints by goal bone: (effector, loops, angle limit, [(link, (lower, upper) or None)]).
 ik={3:(2,8,inf if bad=='ik' else .3,[(1,((-1,-1,-1),(1,1,1)))])}
 if control_root:
  # MMD control roots above the pelvis (全ての親 > センター > グルーブ > lower body) and an
  # unrelated root (操作中心), as most MMD rigs author them; nothing in Source drives them.
  top=len(bones)
  bones += [('全ての親',(0,0,0),-1),('センター',(0,8,0),top),('グルーブ',(0,8.5,0),top+1),('操作中心',(0,0,-.5),-1)]
  bones[4]=('lower body',(0,9,0),top+2)
  # Leg and toe IK under 全ての親, and a heel IK whose goal hangs from the toe IK goal (an
  # uncontrolled link below the Source-driven ankle, as high-heel rigs author it).
  bones += [('左足IK親',(1,0,0),top),('左足ＩＫ',(1,1,0),top+4),('左つま先ＩＫ',(1,.5,-1),top+5),('left heel',(1,.8,.4),15),('left heel tip',(1,0,.4),top+7),('left heel IK',(1,0,.4),top+6)]
  ik[top+5]=(15,40,2,[(14,((-3.1416,0,0),(-.0087,0,0))),(13,None)])
  ik[top+6]=(16,3,4,[(15,None)])
  ik[top+9]=(top+8,8,.5,[(top+7,None)])
 vertices=[(-2,0,0),(2,0,0),(0,3,0)]
 vertices += [(x-2,10-y,0) for y in range(5) for x in range(5)]
 # Unreferenced vertices inside the existing bounds: part of no material or soft body.
 vertices += [(0,1,0)]*extra_vertices
 # A separate BDEF1 triangle in the Core material: no CPU-deformed vertex shares it.
 alone=len(vertices)
 if island:vertices += [(3,0,1),(4,0,1),(3,1,1)]
 # Core vertices partly weighted to the control roots: BDEF2, BDEF4 with the groove the smallest
 # of four weights (a CPU vertex under hardware skinning), BDEF1 on the groove and on the
 # unrelated root, and BDEF2 across 全ての親 and センター.
 control=len(vertices);weights=[]
 if control_root:
  vertices += [(-1,9,.5),(1,9,.5),(0,8.5,1),(0,0,-.5),(0,4,.5)]
  weights=[(1,(4,top+2),(.85,)),(2,(13,4,21,top+2),(.4,.3,.2,.1)),(0,(top+2,),()),(0,(top+3,),()),(1,(top+1,top),(.5,))]
 b+=pack('i',len(vertices))
 for i,p in enumerate(vertices):
  t=weights[i-control][0] if i>=control else 0 if i>=alone else i%5;b+=(vec(nan,p[1],p[2]) if bad=='vertex_position' and i==0 else vec(*p))+(vec(0,0,0) if bad=='zero_normal' and i<3 else vec(nan,0,-1) if bad=='normal' and i==1 else vec(0,0,-1))+(vec(nan,0) if bad=='uv' and i==0 else vec((i%5)/4,(i//5)/6))+vec(.2,.3,0,0)+pack('B',t)
  if i>=control:_,ids,w=weights[i-control];b+=pack('i'*len(ids),*ids)+vec(*w)
  elif t==0:b+=pack('i',0)
  elif t in (1,3):b+=pack('ii',0,1)+vec(.6)
  elif bad=='weights' and i==2:b+=pack('iiii',1,1,1,1)+vec(1,1,1,-2)
  else:b+=pack('iiii',0,1,0,1)+vec(.25,.25,.25,.25)
  if t==3:b+=vec(*p)+vec(0,0,0)+vec(0,0,0)
  b+=vec(1)
 indices=[0,1,2]+([alone,alone+1,alone+2] if island else [])+([control,control+1,control+2,control+2,control+3,control+4] if control_root else [])
 core=len(indices)
 for y in range(4):
  for x in range(4):
   a=3+y*5+x;indices.extend([a,a+1,a+5,a+1,a+6,a+5])
 b+=pack('i',len(indices))+pack('I'*len(indices),*indices)
 b+=pack('i',1)+text(texture_name) if texture else pack('i',0)
 b+=pack('i',material_count)
 for i,count in enumerate([core,len(indices)-core]+[0]*(material_count-2)):
  b+=text('Core' if i==0 else 'Fabric')+text('')+vec(nan if bad=='material' else .3,.55,.8,1)+vec(.1,.1,.1)+vec(8)+vec(.15,.15,.15)+pack('B',31)+vec(0,0,0,1)+vec(.4)+pack('iiBB',0 if texture else -1,-1,0,1)+pack('B',0)+text('')+pack('i',count)
 b+=pack('i',len(bones))
 for i,(name,p,parent) in enumerate(bones):
  flags=0x001e|(0x0020 if i in ik else 0)
  b+=text(name)+text(name)+vec(*p)+pack('iiH',parent,0,flags)+(vec(nan,nan,nan) if bad=='bone_tail' else vec(0,1,0))
  if i in ik:
   effector,loops,angle,links=ik[i];b+=pack('iifI',effector,loops,angle,len(links))
   for link,limit in links:b+=pack('iB',link,1 if limit else 0)+(vec(*limit[0])+vec(*limit[1]) if limit else b'')
 morphs=[]
 morphs.append(text('move vertex')+text('')+pack('BBi',1,1,1)+pack('I',morph_vertex)+vec(nan if bad=='morph_vertex' else morph_offset,0,0))
 morphs.append(text('bone rotation')+text('')+pack('BBi',1,2,1)+pack('i',1)+vec(0,0,0)+vec(0,0,math.sin(.15),nan if bad=='morph_bone' else math.cos(.15)))
 morphs.append(text('tint')+text('')+pack('BBi',1,8,1)+pack('iB',-1,1)+vec(nan if bad=='morph_material' else .1,0,0,0)+vec(0,0,0)+vec(0)+vec(.1,0,0)+vec(0,0,0,0)+vec(0)+vec(0,0,0,0)*3)
 morphs.append(text('group')+text('')+pack('BBi',1,0,2)+pack('if',0,inf if bad=='morph_group' else .5)+pack('if',2,.5))
 morphs.append(text('impulse')+text('')+pack('BBi',1,10,1)+pack('iB',1,0)+vec(nan if bad=='morph_impulse' else 1,0,0)+vec(0,0,1))
 # Group morphs after the five above. A chain multiplies by 1000 per link and ends at the
 # impulse (4); a lattice names its next level twice and ends at the vertex morph (0).
 for k in range(group_chain):
  morphs.append(text('chain'+str(k))+text('')+pack('BBi',1,0,1)+pack('if',len(morphs)+1 if k+1<group_chain else 4,1000))
 for k in range(group_lattice):
  child=len(morphs)+1 if k+1<group_lattice else 0
  morphs.append(text('lattice'+str(k))+text('')+pack('BBi',1,0,2)+pack('if',child,1)+pack('if',child,1))
 b+=pack('i',len(morphs))+b''.join(morphs)+pack('i',0) # display frames
 if chain:
  first=hair
  b+=pack('i',chain+1+(1 if control_root else 0))+text('head')+text('')+pack('iBHB',8,0,0xfffe,0)+vec(.5,.5,.5)+vec(0,17,0)+vec(0,0,0)+vec(1,.5,.5,0,.5)+pack('B',0)
  for k in range(chain):
   b+=text('hair'+str(k))+text('')+pack('iBHB',first+k,1,0xfffd,2)+vec(.25,.7,0)+vec(0,bones[first+k][1][1]-.6,.3)+vec(0,0,0)+vec(chain_masses[k] if chain_masses else .2*(chain_mass_ratio if k%2 else 1),chain_damping,.5,0,.5)+pack('B',1)
  # A kinematic follower on センター: it must follow the body, not stay at the world origin.
  if control_root:b+=text('center')+text('')+pack('iBHB',top+1,0,0xfffe,0)+vec(.5,.5,.5)+vec(0,8,0)+vec(0,0,0)+vec(1,.5,.5,0,.5)+pack('B',0)
  b+=pack('i',chain)
  for k in range(chain):
   b+=text('hair joint'+str(k))+text('')+pack('Bii',0,k,k+1)+vec(0,bones[first+k][1][1],.3)+vec(0,0,0)+vec(0,0,0)*2+(vec(0,0,0)*4 if chain_locked else vec(-.5,-.5,-.5)+vec(.5,.5,.5)+vec(0,0,0)+vec(4,4,4))
  b+=pack('i',0)
  return bytes(b)
 b+=pack('i',3)
 for i,mode in enumerate([0,1,2]):
  b+=text('body'+str(i))+text('')+pack('iBHB',i,0,0,0)+vec(.4,.4,.4)+vec(0,i*5,0)+vec(nan if bad=='body_orientation' and i==1 else 0,0,0)+vec(inf if bad=='body_mass' and i==1 else 1e21 if bad=='body_heavy' and i==1 else 1,nan if bad=='body_damping' and i==1 else .2,.2,0,.5)+pack('B',mode)
 b+=pack('i',1)+text('spring')+text('')+pack('Bii',0,*joint)+vec(0,5,0)+vec(0,0,0)+vec(0,0,0)*2+vec(nan if bad=='joint_limit' else -.4,-.4,-.4)+vec(.4,.4,.4)+vec(0,0,0)+vec(inf if bad=='joint_spring' else 1e14 if bad=='joint_stiff' else 1,1,1)
 b+=pack('i',1)+text('soft fabric')+text('')+pack('BiBHBii',1 if rope else 0,1,1,0,1,2,0)+vec(.4,.08)+pack('i',0)
 b+=vec(nan if bad=='soft' else 1,.02,0,0,0,0,.5,0,1,.1,1,.7) # config
 b+=vec(1,.1,1,.5,.5,.5)+pack('iiii',0,10**9 if bad=='soft_iterations' else 6,0,4)+vec(.8,.8,.8)
 b+=pack('i',1)+pack('iIB',0,3,0)+pack('iII',2,3,7)
 return bytes(b)
if __name__=='__main__':
 out=ROOT/'tests/fixtures';out.mkdir(parents=True,exist_ok=True)
 for name,data in [('cloth21.pmx',make()),('rope21.pmx',make(True)),('native-cloth21.pmx',make(humanoid=True)),('native-rope21.pmx',make(rope=True,humanoid=True)),('textured21.pmx',make(texture=True)),('cycle.pmx',make(cycle=True)),('truncated.pmx',make()[:170])]:
  (out/name).write_bytes(data)
 for name,endpoints in [('self',(1,1)),('invalid',(1,50)),('world',(-1,1))]:
  (out/('native-joint-'+name+'.pmx')).write_bytes(make(humanoid=True,joint=endpoints))
 (out/'native-large-materials.pmx').write_bytes(make(humanoid=True,material_count=513))
 (out/'native-chain.pmx').write_bytes(make(humanoid=True,chain=8))
 (out/'native-stretch-prone.pmx').write_bytes(make(humanoid=True,chain=8,chain_damping=.999999))
 (out/'native-mass-ratio.pmx').write_bytes(make(humanoid=True,chain=8,chain_mass_ratio=32))
 # A locked chain authored like MMD wings: masses fall x4 per link from 1e8.
 (out/'native-heavy-chain.pmx').write_bytes(make(humanoid=True,chain=8,chain_masses=[1e8/4**k for k in range(8)],chain_locked=True))
 (out/'native-material-overflow.pmx').write_bytes(make(humanoid=True,material_count=130))
 # The rope again after extra model vertices: its soft body must not change.
 (out/'native-rope21-extra.pmx').write_bytes(make(rope=True,humanoid=True,extra_vertices=12))
 # A vertex morph that moves a hardware-skinned vertex far outside the model. Every cloth
 # vertex touches a CPU triangle, so the morph moves the island's first vertex (28): only
 # its bone box bounds it.
 (out/'native-far-morph.pmx').write_bytes(make(humanoid=True,island=True,morph_vertex=28,morph_offset=100))
 # MMD control roots above the pelvis with vertices, a follower body and IK goals under them (issue #6).
 (out/'native-control-root.pmx').write_bytes(make(humanoid=True,chain=2,control_root=True))
 # Alpha-textured materials for the alpha-test coverage the RTX Remix renderer uses: a
 # quarter of the texels opaque, and a layer entirely below the 0.5 reference (alpha 100).
 (out/'cutout-quarter.png').write_bytes(png(4,4,b''.join(bytes((200,180,160,255 if i%4==0 else 0)) for i in range(16))))
 (out/'cutout-none.png').write_bytes(png(4,4,bytes((30,30,30,100))*16))
 (out/'native-cutout-quarter.pmx').write_bytes(make(texture=True,texture_name='cutout-quarter.png'))
 (out/'native-cutout-none.pmx').write_bytes(make(texture=True,texture_name='cutout-none.png'))
 # An atlas padded with transparent texels: only the five texels of row 0 the Core triangle
 # maps to (u 0-0.5, v 0) are opaque, 5 of 64 overall.
 (out/'cutout-atlas.png').write_bytes(png(8,8,b''.join(bytes((220,200,190,255 if i<5 else 0)) for i in range(64))))
 (out/'native-cutout-atlas.pmx').write_bytes(make(texture=True,texture_name='cutout-atlas.png'))
 # Nested group morphs: 13 links of coefficient 1000 overflow a float at weight 1, and
 # 40 levels that name their child twice reach 2^40 expansions.
 (out/'native-group-chain.pmx').write_bytes(make(humanoid=True,group_chain=13))
 (out/'native-group-lattice.pmx').write_bytes(make(humanoid=True,group_lattice=40))
 # NaN bone tails, as some exporters write: display data, so the model loads and the jiggle ignores them.
 (out/'native-nan-tail.pmx').write_bytes(make(humanoid=True,bad='bone_tail'))
 # One corrupt value per numeric section: the loader repairs them, except soft bodies, which
 # it rejects before caching.
 for bad in ['zero_normal','uv','material','ik','morph_vertex','morph_bone','morph_material','morph_group','morph_impulse','body_orientation','body_mass','body_damping','joint_limit','joint_spring','soft','soft_iterations',
             'vertex_position','normal','weights','self_parent','body_heavy','joint_stiff']:
  (out/('corrupt-'+bad+'.pmx')).write_bytes(make(bad=bad))
 # One BC1 block with alternating red/green texels, stored in a standard DDS.
 header=[124,0x81007,4,4,8,0,1]+[0]*11+[32,4,struct.unpack('<I',b'DXT1')[0],0,0,0,0,0]+[0x1000,0,0,0,0]
 (out/'checker.dds').write_bytes(b'DDS '+pack('I'*31,*header)+pack('HHI',0xf800,0x07e0,0x11441144))
 print(out)
