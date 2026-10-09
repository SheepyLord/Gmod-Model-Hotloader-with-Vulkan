"""Write the bone window's shared fixtures:

  tests/fixtures/bonemap/rules.json   structural bone-map rules, run by both
                                      tests/character_import_tests.cpp (checkBoneMap)
                                      and tests/test_bone_mapper.py (BM.Validate)

  python scripts/make-bonemap-fixtures.py

The skeletons were written for this project (no third-party data). Values are
0-based bone indices; -1 is none.
"""
import json, pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
VB = 'ValveBiped.Bip01_'


def bone(name, parent, pos, weighted, flags=()):
    return {'name': name, 'parent': parent, 'position': pos, 'weighted': weighted, 'flags': list(flags)}


BASIC = [bone('Root', -1, [0, 0, 0], 0), bone('Hips', 0, [0, .95, 0], 100), bone('Spine', 1, [0, 1.05, 0], 100), bone('Chest', 2, [0, 1.3, 0], 100),
         bone('Neck', 3, [0, 1.45, 0], 50), bone('Head', 4, [0, 1.55, 0], 200),
         bone('LeftShoulder', 3, [.06, 1.4, 0], 30), bone('LeftArm', 6, [.17, 1.4, 0], 100), bone('LeftForeArm', 7, [.43, 1.4, 0], 100), bone('LeftHand', 8, [.68, 1.4, 0], 80),
         bone('RightShoulder', 3, [-.06, 1.4, 0], 30), bone('RightArm', 10, [-.17, 1.4, 0], 100), bone('RightForeArm', 11, [-.43, 1.4, 0], 100), bone('RightHand', 12, [-.68, 1.4, 0], 80),
         bone('LeftUpLeg', 1, [.09, .9, 0], 150), bone('LeftLeg', 14, [.09, .5, 0], 120), bone('LeftFoot', 15, [.09, .08, 0], 60), bone('LeftToeBase', 16, [.09, .02, .1], 20),
         bone('RightUpLeg', 1, [-.09, .9, 0], 150), bone('RightLeg', 18, [-.09, .5, 0], 120), bone('RightFoot', 19, [-.09, .08, 0], 60), bone('RightToeBase', 20, [-.09, .02, .1], 20),
         bone('Hair_01', 5, [0, 1.66, -.08], 10), bone('Hair_02', 22, [0, 1.56, -.12], 10), bone('Hair_end', 23, [0, 1.46, -.14], 0),
         bone('Cape', 3, [0, 1.3, -.1], 10, ['secondary']),
         # Weighted bones no body part uses.
         bone('RightArmTwist', 11, [-.3, 1.4, 0], 10), bone('LeftKneePad', 15, [.12, .5, .05], 10)]
FLAT = [bone('Root', -1, [0, 0, 0], 0)] + [bone(b['name'], 0, b['position'], b['weighted']) for b in BASIC[1:22]]
BASE = {'Pelvis': 1, 'Spine1': 2, 'Spine2': -1, 'Spine4': 3, 'Neck1': 4, 'Head1': 5, 'L_Clavicle': 6, 'L_UpperArm': 7, 'L_Forearm': 8, 'L_Hand': 9,
        'R_Clavicle': 10, 'R_UpperArm': 11, 'R_Forearm': 12, 'R_Hand': 13, 'L_Thigh': 14, 'L_Calf': 15, 'L_Foot': 16, 'L_Toe0': 17,
        'R_Thigh': 18, 'R_Calf': 19, 'R_Foot': 20, 'R_Toe0': 21}


def key(name):
    return name if name in ('', 'Eye_L', 'Eye_R') else VB + name


def values(**change):
    v = dict(BASE)
    v.update(change)
    return {key(k): b for k, b in v.items()}


def issue(code, slot, severity='error'):
    return {'code': code, 'slot': key(slot), 'severity': severity}


FLAT_ORDER = ['L_UpperArm', 'L_Forearm', 'L_Hand', 'R_UpperArm', 'R_Forearm', 'R_Hand', 'L_Thigh', 'L_Calf', 'L_Foot', 'R_Thigh', 'R_Calf', 'R_Foot', 'Head1']
VECTORS = [
    ('valid assignment', 'basic', values(), [], []),
    ('a required part without a bone', 'basic', values(L_Hand=-1), [], [issue('required', 'L_Hand')]),
    ('the hips without a bone', 'basic', values(Pelvis=-1), [], [issue('required', 'Pelvis')]),
    ('a forearm without a bone does not also put the hand out of order', 'basic', values(L_Forearm=-1), [], [issue('required', 'L_Forearm')]),
    ('one bone for two parts', 'basic', values(L_Hand=8), [], [issue('duplicate', 'L_Hand')]),
    ('the chest and the middle spine on one bone', 'basic', values(Spine2=3), [], [issue('duplicate', 'Spine4')]),
    ('the toes on the foot bone', 'basic', values(L_Toe0=16), [], [issue('duplicate', 'L_Toe0')]),
    ('an eye on the head bone', 'basic', {**values(), 'Eye_R': 5}, [], [issue('duplicate', 'Eye_R')]),
    ('a forearm on the other arm', 'basic', values(L_Forearm=26), [], [issue('order', 'L_Forearm'), issue('order', 'L_Hand')]),
    ('a neck below a leg is a warning, the head above it an error', 'basic', values(Neck1=27), [], [issue('order', 'Neck1', 'warning'), issue('order', 'Head1')]),
    ('the chest above the neck bone', 'basic', values(Spine4=4, Neck1=3), [], [issue('order', 'Neck1', 'warning'), issue('order', 'L_Clavicle'), issue('order', 'R_Clavicle')]),
    ('legs swapped between sides break the leg chains', 'basic', values(L_Thigh=18, R_Thigh=14), [], [issue('order', 'L_Calf'), issue('order', 'R_Calf')]),
    ('a thigh hanging from the upper body', 'basic', values(L_Thigh=22), [], [issue('leg_on_spine', 'L_Thigh'), issue('order', 'L_Calf')]),
    ('the hips and the spine swapped', 'basic', values(Pelvis=2, Spine1=1), [], [issue('order', 'L_Thigh'), issue('order', 'R_Thigh'), issue('leg_on_spine', 'L_Thigh'), issue('leg_on_spine', 'R_Thigh')]),
    ('a required part on a bone moved by physics', 'basic', values(L_Hand=25), [], [issue('order', 'L_Hand'), issue('physics', 'L_Hand')]),
    ('an optional part on a bone moved by physics', 'basic', values(L_Toe0=25), [], [issue('order', 'L_Toe0'), issue('physics', 'L_Toe0', 'warning')]),
    ('an eye outside the head', 'basic', {**values(), 'Eye_L': 27}, [], [issue('order', 'Eye_L')]),
    ('an eye inside the head', 'basic', {**values(), 'Eye_L': 22}, [], []),
    ('no middle spine and no chest', 'basic', values(Spine2=3, Spine4=-1), [], []),
    ('the head on the neck bone with no neck', 'basic', values(Head1=4, Neck1=-1), [], []),
    ('no shoulder: the upper arm hangs from the chest', 'basic', values(L_Clavicle=-1), [], []),
    ('the root as the hips', 'basic', values(Pelvis=0), [], []),
    ('no toes', 'basic', values(L_Toe0=-1, R_Toe0=-1), [], []),
    ('a hair part swings', 'basic', values(), [22], []),
    ('two overlapping hair parts', 'basic', values(), [22, 23], []),
    ('a cape swings', 'basic', values(), [25], []),
    ('the head cannot swing', 'basic', values(), [5], [issue('jiggle_body', 'Head1')]),
    ('the chest cannot swing', 'basic', values(), [3], [issue('jiggle_body', 'Spine4')]),
    ('a swinging part with an eye in it', 'basic', {**values(), 'Eye_L': 22}, [22], [issue('jiggle_body', 'Eye_L')]),
    ('at most 64 swinging parts', 'basic', values(), [22] * 65, [issue('jiggle_too_many', '')]),
    ('a flat skeleton: order is only a warning', 'flat', values(L_Toe0=-1, R_Toe0=-1, L_Clavicle=-1, R_Clavicle=-1, Neck1=-1, Spine4=-1), [],
     [issue('order', k, 'warning') for k in FLAT_ORDER]),
    ('a flat skeleton keeps hard errors', 'flat', values(L_Toe0=-1, R_Toe0=-1, L_Clavicle=-1, R_Clavicle=-1, Neck1=-1, Spine4=-1, L_Hand=-1), [],
     [issue('required', 'L_Hand')] + [issue('order', k, 'warning') for k in FLAT_ORDER if k != 'L_Hand']),
]


def main():
    out = {'about': 'Bone map rules shared by checkBoneMap (C++) and BM.Validate (Lua); written by scripts/make-bonemap-fixtures.py.',
           'skeletons': {'basic': {'bones': BASIC}, 'flat': {'bones': FLAT}},
           'vectors': [{'name': n, 'skeleton': s, 'values': v, 'chainRoots': c, 'expect': e} for n, s, v, c, e in VECTORS]}
    path = ROOT / 'tests/fixtures/bonemap/rules.json'
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(out, ensure_ascii=False, indent=1) + '\n', encoding='utf-8', newline='\n')
    print(f'Wrote {path} ({len(VECTORS)} vectors)')


if __name__ == '__main__':
    main()
