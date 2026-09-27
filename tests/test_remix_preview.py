"""Preview materials must retain textures/cutouts without changing world VMTs."""
from pathlib import Path
import re
from lupa import LuaRuntime
root=Path(__file__).resolve().parents[1]
lua=LuaRuntime(unpack_returned_tuples=True)

# GLua (Garry's Mod's LuaJIT dialect) has a `continue` statement; standard Lua cannot parse
# it. glua_to_lua rewrites each one as `goto continue_N` and wraps the body of the for/while
# loop it belongs to as `do do <body> end ::continue_N:: end`. The inner do ... end keeps the
# label outside the scope of every local the body declares (a goto may not jump into one)
# and lets the body still end in return or break. Line numbers are unchanged. Strings and
# comments are skipped; `continue` in repeat ... until (whose condition may read body locals)
# and other GLua-only syntax are rejected, not guessed at: `//` would parse as floor division.
TOKEN=re.compile(r'''
   (?P<space>\s+)
 | (?P<comment>--(?:\[(?P<cl>=*)\[.*?\](?P=cl)\]|[^\n]*))
 | (?P<string>\[(?P<sl>=*)\[.*?\](?P=sl)\]|"(?:\\z\s*|\\.|[^"\\\n])*"|'(?:\\z\s*|\\.|[^'\\\n])*')
 | (?P<number>0[xX][0-9A-Fa-f.]*(?:[pP][+-]?[0-9]+)?|(?:[0-9]+\.?[0-9]*|\.[0-9]+)(?:[eE][+-]?[0-9]+)?)
 | (?P<name>[A-Za-z_][A-Za-z0-9_]*)
 | (?P<glua>//|/\*|!=|&&|\|\||!)
 | (?P<symbol>::|\.\.\.|\.\.|[=~<>]=|.)
''',re.S|re.X)
def glua_to_lua(code):
    out,blocks,labels=[],[],0
    for m in TOKEN.finditer(code):
        kind,text=m.lastgroup,m.group()
        where='line %d'%(code.count('\n',0,m.start())+1)
        if kind=='glua': raise ValueError('unsupported GLua syntax %r at %s'%(text,where))
        if kind=='name':
            if text in ('function','if','repeat'): blocks.append({'kind':text})
            elif text in ('for','while'): blocks.append({'kind':'loop','do':None})
            elif text=='do':
                # A for/while header's do opens that loop's body; any other do is a block.
                if blocks and blocks[-1]['kind']=='loop' and blocks[-1]['do'] is None: blocks[-1]['do']=len(out)
                else: blocks.append({'kind':'do'})
            elif text in ('end','until'):
                if not blocks or (blocks[-1]['kind']=='repeat')!=(text=='until'): raise ValueError('unbalanced %s at %s'%(text,where))
                block=blocks.pop()
                if 'label' in block:
                    out[block['do']]='do do'
                    text='end ::%s:: end'%block['label']
            elif text=='continue':
                # The innermost loop of the enclosing function; if/do blocks are transparent.
                loop=next((b for b in reversed(blocks) if b['kind'] in ('loop','repeat','function')),None)
                if not loop or loop['kind']!='loop': raise ValueError('continue outside a for/while body at '+where)
                if 'label' not in loop:
                    labels+=1
                    loop['label']='continue_%d'%labels
                text='goto '+loop['label']
        out.append(text)
    if blocks: raise ValueError('unclosed %s block'%blocks[-1]['kind'])
    return ''.join(out)
# The rewrite keeps GLua semantics: nested loops, locals after continue, while, a body ending
# in return; keywords in comments and strings are left alone.
lua.execute(glua_to_lua('''
local out={}
for i=1,6 do
 if i%2==0 then continue end -- comments are skipped: continue end
 local square=i*i
 for j=1,3 do if j==2 then continue end out[#out+1]=square+j end
 if i==5 then out[#out+1]='five' continue end
 local k=0 while k<2 do k=k+1 if k==1 then continue end out[#out+1]=-k end
end
local function firstEven(t) for _,v in ipairs(t) do if v%2==1 then continue end return v end end
out[#out+1]=firstEven({1,3,4,6}) out[#out+1]='continue do end'
assert(table.concat(out,',')=='2,4,-2,10,12,-2,26,28,five,4,continue do end','GLua continue rewrite changed behaviour')
'''))
lua.execute('''
local texture={GetName=function()return 'fixture/base' end}
worldMat={IsError=function()return false end,GetTexture=function()return texture end,
 SetFloat=function()error('World material mutated')end,SetVector=function()error('World material mutated')end}
mmdhl={native={SetNativeVertexCache=function()end,RenderStats=function()return {fixedFunctionRemix=true}end}}
mmdhl.Decode=function(v)return v end
calls={}mmdhl.native.DrawPreview=function(h,i,name)calls[#calls+1]={h,i,name}end
CreateClientConVar=function()return {GetBool=function()return true end}end
cvars={AddChangeCallback=function()end}
Vector=function(...)return {...}end unpack=table.unpack
file={Exists=function()return false end}
Material=function()return worldMat end
created={}
CreateMaterial=function(name,shader,params)
 assert(name:sub(1,5)=='vgui/','Preview entered Remix scene categorization')
 assert(shader=='UnlitGeneric' and params['$alphatest']=='1')
 assert(params['$basetexture']=='fixture/base')
 local m={name=name,params=params,GetName=function(self)return self.name end,
 SetTexture=function(self,k,v)self.texture=v end,SetFloat=function(self,k,v)self[k]=v end,
 SetVector=function(self,k,v)self[k]=v end}
 created[#created+1]=m return m
end
''')
source=(root/'addon/lua/mmdhl/native_render.lua').read_text(encoding='utf8')
lua.execute(glua_to_lua(source[:source.index('local function shadowNames')]))
lua.execute('''
local info={materials={{path='fixture/material',diffuse={.2,.4,.8},alpha=1,twoSided=true}}}
assert(mmdhl.DrawLibraryPreview(11,'fixture',info))
assert(#created==1 and #calls==1)
assert(created[1].params['$nocull']=='1' and created[1]['$color'][1]==.2)
assert(created[1]['$alpha']==1 and calls[1][3]:sub(1,6)=='!vgui/')
assert(mmdhl.DrawLibraryPreview(11,'fixture',info) and #created==1,'Preview materials leaked each frame')
''')
print('PASS: RTX preview texture, tint, cutouts, VGUI isolation and cache reuse')
