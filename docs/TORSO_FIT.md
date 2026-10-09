# The carrier's torso and bone pins (2.3.0)

Every character spawns as a 56-bone ValveBiped "carrier" with 18 physics parts
(`docs/NATIVE_BACKEND.md`). Up to 2.2 the fitter picked the chest (`Spine4`) and
the middle spine (`Spine2`) by name: `上半身3` was the chest and `上半身2` the
middle spine. Many models are built the other way round, `上半身 > 上半身3 >
上半身2 > 首` (Ganyu and other Genshin Impact edits, several Wuthering Waves and
Honkai: Star Rail models). Their middle spine then sat above the chest, the chest
mesh rode the lower-back part, and ragdolls and NPCs collapsed with the chest on
the ground (issue #9). Since 2.3.0 the fitter reads the torso from the skeleton's
shape, as the SCMI importer's spine fix did.

## What players notice

* Ragdolls, NPCs and player models of such models keep their chest up: the chest
  part now follows the bone that holds the neck and both shoulders.
* About 40 % of models had `Spine2` and `Spine4` on the same bone, because a name
  such as `右腕捩3` or `後髪3` was read as "3" and taken for `上半身3`. They now get
  a real middle spine (`上半身1` when the model has one) or a synthesized one.
  Models without finger bones no longer get fingers on twist or ribbon bones.
* Every model is fitted once more (rig generator 31). Placed characters keep their
  carrier until they are spawned again.
* Collision corrections saved for the `Spine4` part of a model whose chest bone
  changed are relative to the old chest bone (on Ganyu 2.7 units below the new
  one); check them in the collision editor after updating.
* When a model lacks body parts, the error names every missing part with the bone
  names that were searched, for example `No bone found for: left thigh (searched
  左足, leg_L, left leg), right forearm (searched 右ひじ, 右肘, elbow_R, right elbow)`.

## How the chest is chosen

`native/rig_torso.hpp` (`resolveTorso`, pure and unit-tested) runs inside
`fitRig` after the other carrier bones are matched by name. The chain runs from
`Spine1` (t = 0) to the neck (t = 1; without a neck, a quarter of the way down
from the head).

1. **The chest holds the neck and both shoulders.** It is their nearest common
   ancestor (the clavicles, or the upper arms when there are none; the head when
   there is no neck). If the neck hangs from `Spine1` itself, the bone holding the
   shoulders is the chest, and the other way round.
2. **The chain** is every bone from `Spine1` up to the chest. A bone moved by the
   model's own physics (a dynamic rigid body, a VRM spring), a breast helper
   (`おっぱい`, `乳`, `breast`, `boob`, `pai`, `mune`, `bust`), a bone already
   used by another body part, or one farther than 0.26 of the chain's length from
   the `Spine1`-neck line is never a torso bone. When the chest itself is refused,
   the highest usable chain bone below it is the chest.
3. **Bands.** The chest must lie between t = 0.10 and 0.95. A chest at the neck
   (a neck base holding the neck and shoulders) gives way to the highest chain
   bone in the band below it. Otherwise it moves with a chest synthesized at
   t = 0.55, the point 2.2 used for models without one.
4. **The middle spine** is a chain bone below the chest with t between 0.04 and
   the chest's t - 0.04: an `上半身…` / `upper body…` name first, then other
   spine or chest names, the one nearest halfway. Other helpers never drive it
   (their bone morphs or physics would be overridden); without one it is
   synthesized halfway between `Spine1` and the chest, as before.
5. **A chain bone at the chest's place** (within 0.04, such as a duplicate
   `上半身2+`) moves with the chest.
6. **No humanoid chain above `Spine1`** (the neck and shoulders hang elsewhere):
   the 2.2 names decide (`上半身3`, else `上半身2`, else `chest`; `上半身2` is the
   middle spine below a `上半身3`), still checked against the bands. **A neck
   below `Spine1`**: the 2.2 names decide, without bands.

A mapped carrier origin never leaves its PMX bone (the anatomy checks compare
every physics part with its bone). A bone that cannot be a pivot is an alias of
the carrier bone instead (`mmdAliases` in the manifest): the carrier bone drives
it rigidly, which is what SCMI's bone merge achieves.

Names compare loosely (case, spaces, `_` and `-` ignored) only when both names are
plain ASCII and the folded name has at least three characters, not all digits: the
old fold dropped every Japanese character. Native flex names still use the old fold,
so saved face presets keep working.

### Diagnostics

The rig manifest carries `torso`: `{"method": "topology" | "names" |
"degenerate", "repairs": [{"code", "bones": [PMX indices], "text"}]}`. The
texts are English; the bone window shows each code as a note in the player's
language (`bonemap.torso_*`) with the names of its `bones`, in this order:

| Code | `bones` | Meaning |
|---|---|---|
| `reordered` | middle spine, chest | The `上半身N` names run against the hierarchy (Ganyu): the lower bone is the middle spine, the higher one the chest. |
| `ignored` | the bone | A `上半身3` holds no shoulders (a leaf helper, or a neck base above the chest): it follows its parent. |
| `neck_on_spine` | neck, chest | The neck hangs from `Spine1`; the bone holding the shoulders is the chest. |
| `shoulders_on_spine` | chest | The shoulders hang from `Spine1`; the bone holding the neck is the chest. |
| `rejected` | the bone | The bone holding the neck and shoulders cannot be the chest (physics, breast helper, off the torso line, used by another part). |
| `band` | bone, chest; or the bone | A chest or middle spine outside its band moves with the chest below it, or with a synthesized pivot. |
| `coincident` | bone, chest | A chain bone at the chest's place moves with the chest. |
| `swapped` | parent, chest | Name fallback: the bone named as the middle spine hangs below the one named as the chest and becomes the chest. |
| `names`, `degenerate` | none; Spine1's bone | The fallbacks of step 6. |

## Bone pins

The fit option `boneMap` sets carrier bones by hand: `{"ValveBiped.Bip01_Spine4": 12,
"ValveBiped.Bip01_L_Toe0": -1}`, a PMX bone index or -1 for none (synthesized).
The keys are the 52 assignable parts of `native/humanoid_slots.hpp` (not
`ValveBiped.Bip01_Spine` and not the eyes). Whole numbers written as `12.0`, as
`util.TableToJSON` does, are accepted. The bone window saves these pins in
`fit_overrides/<asset>.json` and every spawn sends them (`docs/CHARACTER_IMPORT.md`).

* A converted character's own assignment (`Model::conversionBoneMap`, from
  `manifest.conversion.boneMap`) is the base map; pins override it, and both
  override the name matching. A converted middle spine without a chest is the
  chest (the part with a physics body). A pinned bone leaves the part the fitter
  had given it to (issue `moved`); that part is then synthesized or, if required,
  missing.
* A pinned `Spine2` or `Spine4` still passes the band check; outside it, the bone
  moves with a synthesized pivot and the issue `band` says so. Its bones are
  `provenance: "user"` (`"conversion"` for the converter's).
* Refused, with ImportError `fit.bone_map` and `details.issues`: a key that is not
  an assignable part, a value that is not a whole number from -1 to the bone
  count (`range`), one bone for two parts (`duplicate`), a required part pinned to
  -1 (`required`), and a `boneMap` that is not an object.
* Missing required parts (hips, spine, head, upper arms, forearms, hands, thighs,
  lower legs, feet) throw ImportError `fit.landmarks`: the message lists every part
  in words with the names searched, `details` has `missing` (ValveBiped names) and
  `searched` (ValveBiped name -> names).
* `boneMap` is part of the fitted-rig cache key (`carrierFitKey`), and a non-empty
  map bypasses the rescaled cached fit (`cachedFitApplies`, also used by
  `PreviewCarrierFit`), so pinned carriers are always fitted again.

### `native.GetBoneMapProposal(assetId, optionsJSON)` (both realms)

The fitter's choice for a loaded asset (`RequestAsset` / `AssetInfo` first), with
the pins of `options.boneMap`, computed by the same mapping code as `fitRig`
without fitting bodies:

```json
{"version": 1, "asset": "<id>",
 "bones": [{"name": "ValveBiped.Bip01_Spine4", "mmd": 10, "aliases": [], "provenance": "PMX", "required": false}, ...],
 "missing": ["ValveBiped.Bip01_L_Calf"],
 "searched": {"ValveBiped.Bip01_L_Calf": ["ValveBiped.Bip01_L_Calf", "左ひざ", "左膝", "knee_L", "left knee"]},
 "issues": [{"code": "band", "severity": "warning", "slot": "ValveBiped.Bip01_Spine4", "text": "..."}],
 "torso": {"method": "topology", "repairs": [{"code": "reordered", "bones": [9, 10], "text": "..."}]},
 "error": "No bone found for: left lower leg (searched ...)", "errorCode": "fit.landmarks"}
```

`bones` lists the 56 reference bones in carrier order (`mmd` -1 is synthesized;
`provenance` is `PMX`, `synthesized`, `user` or `conversion`). Issue codes:
`range`, `duplicate` and `required` (errors), `moved` and `band` (warnings),
`duplicate` (information, two parts matched one bone by name). `error` and
`errorCode` are what `fitRig` would throw with these options. Bad pins are issues,
not failures; a call fails (`nil, message`) only for an asset that is not loaded,
options that are not a JSON object ("Invalid bone map options") or a `boneMap`
that is not an object. `GetCapabilities().boneMap` is `{version = 1, fit = true}`.

## Compatibility

* Generator 31: fits cached by 2.2 (`fits/g30-…`) are not used, and carriers
  fitted by an older binary are refitted (`mmdhl.IsCurrentRig`); a server and a
  client must run the same native release to share carriers.
* Fits cached by 2.3.0 development builds before the torso fix lack
  `manifest.torso` and are fitted again.
* Older native modules ignore `boneMap` (they fit by names) and have no
  `GetBoneMapProposal`; the bone window then hides its fit mode.
* VRM avatars map `chest` to `上半身2` and `upperChest` to `上半身3`; their canonical
  hierarchy gives the same choice as before.

## Checking models

`python scripts/check-torso.py <model or folder>... [--glob '*/1_PMX/**/*.pmx']
[--sample N] [--json report.json] [--list]` runs `mmdhl_worker --fit` and
`--inspect` on every model, read-only, and counts inverted torsos (`Spine2` above
`Spine4`), heights out of order, PMX bones driven by two carrier bones, chests that
do not hold the neck and both shoulders, aliases and the repair codes. It exits
with 1 when a model is inverted, double-mapped or has such a chest.
`mmdhl_worker --bone-map-proposal <model> [options.json]` prints
`GetBoneMapProposal`'s answer.

On a corpus of 1,205 PMX files (1,011 fitted characters), the 2.2 rule gave 31
inverted torsos, 296 models with one PMX bone driven by two carrier bones (294 of
them `Spine2` = `Spine4`) and 3 chests holding no shoulders; 2.3.0 gives none of
them, with 30 `reordered`, 3 `ignored` and 1 `coincident` notes. 350 models
changed only their torso; 7 models without finger bones lost fingers that were
twist, ribbon or center bones; nothing else changed.

Tests: `torso` (the resolver on synthetic skeletons, pins, the proposal and the
landmark error), `animated_carriers_and_sharing` (torso variants of the fixture),
`vrm_import_and_springs` (`upperChest`), `character_import` (the conversion map
as the base), `physics_profiles` (`golden.json`, re-recorded: the manifest's
`torso` changes every rig key, the `.phy` text did not change) and
`tests/test_bone_mapper.py` (the window on the fitter's recorded answers,
`tests/fixtures/bonemap/proposal.json`, which `mmdhl_torso_tests --record-window`
rewrites and the `torso` test keeps current).
