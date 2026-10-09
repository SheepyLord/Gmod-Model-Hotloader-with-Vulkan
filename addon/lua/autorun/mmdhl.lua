if SERVER then
    AddCSLuaFile()
    AddCSLuaFile('mmdhl/client.lua')
    AddCSLuaFile('mmdhl/ui.lua')
    AddCSLuaFile('mmdhl/library.lua')
    AddCSLuaFile('mmdhl/entity_editor.lua')
    AddCSLuaFile('mmdhl/debug.lua')
    AddCSLuaFile('mmdhl/menu.lua')
    AddCSLuaFile('mmdhl/carrier.lua')
    AddCSLuaFile('mmdhl/actors.lua')
    AddCSLuaFile('mmdhl/first_person.lua')
    AddCSLuaFile('mmdhl/sharing.lua')
    AddCSLuaFile('mmdhl/scene_sharing.lua')
    AddCSLuaFile('mmdhl/native_render.lua')
    AddCSLuaFile('mmdhl/faceposer.lua')
    AddCSLuaFile('mmdhl/native_tools.lua')
    AddCSLuaFile('mmdhl/materials.lua')
    AddCSLuaFile('mmdhl/settings.lua')
    AddCSLuaFile('mmdhl/performance.lua')
    AddCSLuaFile('mmdhl/physics_settings.lua')
    AddCSLuaFile('mmdhl/physics_lod.lua')
    AddCSLuaFile('mmdhl/instances.lua')
    AddCSLuaFile('mmdhl/client_ragdolls.lua')
    AddCSLuaFile('mmdhl/player_copies.lua')
    AddCSLuaFile('mmdhl/persistence.lua')
    AddCSLuaFile('mmdhl/secondary_backend.lua')
    AddCSLuaFile('mmdhl/secondary_collision.lua')
    AddCSLuaFile('mmdhl/physics_reset.lua')
    AddCSLuaFile('mmdhl/collision_editor.lua')
    AddCSLuaFile('mmdhl/props/shared.lua')
    AddCSLuaFile('mmdhl/props/collision.lua')
    AddCSLuaFile('mmdhl/props/render.lua')
    AddCSLuaFile('mmdhl/props/library.lua')
    AddCSLuaFile('mmdhl/props/tool.lua')
    AddCSLuaFile('mmdhl/props/editor.lua')
    AddCSLuaFile('mmdhl/bodygroups.lua')
    AddCSLuaFile('mmdhl/names.lua')
    AddCSLuaFile('mmdhl/terms.lua')
    AddCSLuaFile('mmdhl/workshop.lua')
    AddCSLuaFile('mmdhl/workshop_export.lua')
    AddCSLuaFile('mmdhl/physics_editor.lua')
    AddCSLuaFile('mmdhl/bone_mapper.lua')
    AddCSLuaFile('mmdhl/file_access.lua')
end
mmdhl = mmdhl or {}
if SERVER then
    AddCSLuaFile('mmdhl/i18n.lua')
    AddCSLuaFile('mmdhl/installation.lua')
    AddCSLuaFile('mmdhl/installation_ui.lua')
    AddCSLuaFile('mmdhl/native_policy.lua')
    AddCSLuaFile('mmdhl/compatibility_policy.lua')
end
include('mmdhl/i18n.lua')
include('mmdhl/installation.lua')
if CLIENT then include('mmdhl/installation_ui.lua') include('mmdhl/menu.lua') end
if not mmdhl.CheckInstallation() then include('mmdhl/debug.lua') return end
local mountedPackages={}
function mmdhl.MountPackage(path)
    if mountedPackages[path] then return true end
    local prefix='data/mmd_hotloader/'
    if not isstring(path) or path:sub(1,#prefix)~=prefix then return false end
    -- In single player and on a listen server the server has already mounted the
    -- carrier in this process and the model is loaded. Mounting it again makes the
    -- game reload it: its server ragdolls briefly take models/error.mdl, and
    -- C_ServerRagdoll prints "models/error.mdl missing vcollide data".
    local key=CLIENT and path:match('^data/mmd_hotloader/rigs/(%x+)/carrier%.gma$')
    if key and #file.Find('models/mmd/'..key:sub(1,16)..'/*.mdl','GAME')>0 then mountedPackages[path]=true return true end
    local mount,err=mmdhl.native.GetMountablePackage(path:sub(#prefix+1))
    if not mount then return false,err end
    local ok=game.MountGMA(mount)
    if ok then mountedPackages[path]=true end
    return ok
end
function mmdhl.Decode(value, err)
    if value == nil then return nil, err end
    if isstring(value) then return util.JSONToTable(value) end
    return value
end
if SERVER then include('mmdhl/server.lua') else include('mmdhl/client.lua') end
include('mmdhl/carrier.lua')
include('mmdhl/materials.lua')
include('mmdhl/actors.lua')
include('mmdhl/sharing.lua')
include('mmdhl/scene_sharing.lua')
include('mmdhl/props/shared.lua')
include('mmdhl/props/collision.lua')
if SERVER then include('mmdhl/props/server.lua') else include('mmdhl/props/render.lua') include('mmdhl/props/library.lua') include('mmdhl/props/tool.lua') include('mmdhl/props/editor.lua') end
if CLIENT then include('mmdhl/first_person.lua') end
include('mmdhl/secondary_backend.lua')
include('mmdhl/secondary_collision.lua')
include('mmdhl/settings.lua')
if CLIENT then include('mmdhl/instances.lua') end
if CLIENT then include('mmdhl/client_ragdolls.lua') end
if CLIENT then include('mmdhl/player_copies.lua') end
if CLIENT then include('mmdhl/performance.lua') end
include('mmdhl/physics_reset.lua')
if CLIENT then include('mmdhl/physics_settings.lua') end
if CLIENT then include('mmdhl/physics_lod.lua') end
include('mmdhl/faceposer.lua')
include('mmdhl/native_tools.lua')
include('mmdhl/persistence.lua')
include('mmdhl/collision_editor.lua')
include('mmdhl/physics_editor.lua')
if CLIENT then include('mmdhl/names.lua') include('mmdhl/terms.lua') end
include('mmdhl/workshop.lua')
if CLIENT then include('mmdhl/workshop_export.lua') end
include('mmdhl/bone_mapper.lua')
include('mmdhl/file_access.lua')
include('mmdhl/debug.lua')
-- Engine Entity methods take precedence over SENT methods. Dispatch only this
-- class to its adapter; preserve the original behavior for every other entity.
do
    if not istable(mmdhl.entityAdaptersInstalled) then mmdhl.entityAdaptersInstalled={} end
    local meta=FindMetaTable('Entity')
    for _,name in ipairs({'GetFlexNum','GetFlexName','GetFlexType','GetFlexBounds','HasFlexManipulatior','GetFlexScale','GetFlexWeight','SetFlexWeight','SetFlexScale','LookupBone','GetBoneCount','GetBoneName','GetBoneParent','GetBoneMatrix','GetBonePosition','GetManipulateBoneAngles','ManipulateBoneAngles','LookupAttachment','GetAttachment','SetEyeTarget'}) do
        if mmdhl.entityAdaptersInstalled[name] then continue end
        mmdhl.entityAdaptersInstalled[name]=true
        local original=meta[name]
        meta[name]=function(ent,...)
            if IsValid(ent) and ent:GetClass()=='mmdhl_ragdoll' then
                local stored=scripted_ents.GetStored('mmdhl_ragdoll')
                if stored and stored.t[name] then return stored.t[name](ent,...) end
            end
            if original then return original(ent,...) end
        end
    end
end
