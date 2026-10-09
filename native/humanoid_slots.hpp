#pragma once
// The carrier body parts a character's bones are assigned to (the bone window,
// the converter and the fitter's pins), and the MMD names the converters write.
// addon/lua/mmdhl/bone_mapper_rules.lua mirrors the catalogue; the parity
// fixture tests/fixtures/bonemap/slots.json keeps the two equal.
#include <array>
#include <span>
#include <string>
#include <string_view>
namespace mmd {
// key: the full ValveBiped name (the eyes, which only the converter assigns, are
// Eye_L/Eye_R). anchors: the slot's bone must be a descendant of the bone of
// the first assigned anchor. partner: the slot on the other side.
struct SlotInfo { const char* key; const char* id; const char* group; const char* family; char side; int segment;
                  bool required, physical, recommended, convertOnly; std::array<const char*,3> anchors;
                  const char* partner; const char* mmdJp; const char* mmdEn; };
#define MMDHL_VB "ValveBiped.Bip01_"
#define MMDHL_FINGERS(S,s,G,J,E,P) \
 {MMDHL_VB S"_Finger0",s"_thumb_1",G,"thumb",*S,1,false,false,false,false,{MMDHL_VB S"_Hand",nullptr,nullptr},MMDHL_VB P"_Finger0",J"親指０","thumb0_" E}, \
 {MMDHL_VB S"_Finger01",s"_thumb_2",G,"thumb",*S,2,false,false,false,false,{MMDHL_VB S"_Finger0",nullptr,nullptr},MMDHL_VB P"_Finger01",J"親指１","thumb1_" E}, \
 {MMDHL_VB S"_Finger02",s"_thumb_3",G,"thumb",*S,3,false,false,false,false,{MMDHL_VB S"_Finger01",nullptr,nullptr},MMDHL_VB P"_Finger02",J"親指２","thumb2_" E}, \
 {MMDHL_VB S"_Finger1",s"_index_1",G,"index",*S,1,false,false,false,false,{MMDHL_VB S"_Hand",nullptr,nullptr},MMDHL_VB P"_Finger1",J"人指１","fore1_" E}, \
 {MMDHL_VB S"_Finger11",s"_index_2",G,"index",*S,2,false,false,false,false,{MMDHL_VB S"_Finger1",nullptr,nullptr},MMDHL_VB P"_Finger11",J"人指２","fore2_" E}, \
 {MMDHL_VB S"_Finger12",s"_index_3",G,"index",*S,3,false,false,false,false,{MMDHL_VB S"_Finger11",nullptr,nullptr},MMDHL_VB P"_Finger12",J"人指３","fore3_" E}, \
 {MMDHL_VB S"_Finger2",s"_middle_1",G,"middle",*S,1,false,false,false,false,{MMDHL_VB S"_Hand",nullptr,nullptr},MMDHL_VB P"_Finger2",J"中指１","middle1_" E}, \
 {MMDHL_VB S"_Finger21",s"_middle_2",G,"middle",*S,2,false,false,false,false,{MMDHL_VB S"_Finger2",nullptr,nullptr},MMDHL_VB P"_Finger21",J"中指２","middle2_" E}, \
 {MMDHL_VB S"_Finger22",s"_middle_3",G,"middle",*S,3,false,false,false,false,{MMDHL_VB S"_Finger21",nullptr,nullptr},MMDHL_VB P"_Finger22",J"中指３","middle3_" E}, \
 {MMDHL_VB S"_Finger3",s"_ring_1",G,"ring",*S,1,false,false,false,false,{MMDHL_VB S"_Hand",nullptr,nullptr},MMDHL_VB P"_Finger3",J"薬指１","third1_" E}, \
 {MMDHL_VB S"_Finger31",s"_ring_2",G,"ring",*S,2,false,false,false,false,{MMDHL_VB S"_Finger3",nullptr,nullptr},MMDHL_VB P"_Finger31",J"薬指２","third2_" E}, \
 {MMDHL_VB S"_Finger32",s"_ring_3",G,"ring",*S,3,false,false,false,false,{MMDHL_VB S"_Finger31",nullptr,nullptr},MMDHL_VB P"_Finger32",J"薬指３","third3_" E}, \
 {MMDHL_VB S"_Finger4",s"_little_1",G,"little",*S,1,false,false,false,false,{MMDHL_VB S"_Hand",nullptr,nullptr},MMDHL_VB P"_Finger4",J"小指１","little1_" E}, \
 {MMDHL_VB S"_Finger41",s"_little_2",G,"little",*S,2,false,false,false,false,{MMDHL_VB S"_Finger4",nullptr,nullptr},MMDHL_VB P"_Finger41",J"小指２","little2_" E}, \
 {MMDHL_VB S"_Finger42",s"_little_3",G,"little",*S,3,false,false,false,false,{MMDHL_VB S"_Finger41",nullptr,nullptr},MMDHL_VB P"_Finger42",J"小指３","little3_" E}
// 54 rows: the order of the window's Tab key and of its automatic advance.
inline constexpr SlotInfo HumanoidSlots[]={
 {MMDHL_VB"Pelvis","hips","torso","pelvis",0,0,true,true,false,false,{},"","下半身","lower body"},
 {MMDHL_VB"Spine1","spine","torso","spine",0,0,true,true,false,false,{},"","上半身","upper body"},
 {MMDHL_VB"Spine2","middle_spine","torso","spine",0,0,false,false,false,false,{MMDHL_VB"Spine1"},"","上半身2","upper body2"},
 // Written 上半身2 instead when the middle spine is empty (an MMD model's second segment).
 {MMDHL_VB"Spine4","chest","torso","spine",0,0,false,true,true,false,{MMDHL_VB"Spine2",MMDHL_VB"Spine1"},"","上半身3","upper body3"},
 {MMDHL_VB"Neck1","neck","torso","neck",0,0,false,false,true,false,{MMDHL_VB"Spine4",MMDHL_VB"Spine1"},"","首","neck"},
 {MMDHL_VB"Head1","head","torso","head",0,0,true,true,false,false,{MMDHL_VB"Neck1",MMDHL_VB"Spine4",MMDHL_VB"Spine1"},"","頭","head"},
 {"Eye_L","left_eye","eyes","eye",'L',0,false,false,false,true,{MMDHL_VB"Head1"},"Eye_R","左目","eye_L"},
 {"Eye_R","right_eye","eyes","eye",'R',0,false,false,false,true,{MMDHL_VB"Head1"},"Eye_L","右目","eye_R"},
 {MMDHL_VB"L_Clavicle","left_shoulder","left_arm","clavicle",'L',0,false,true,true,false,{MMDHL_VB"Spine4",MMDHL_VB"Spine1"},MMDHL_VB"R_Clavicle","左肩","shoulder_L"},
 {MMDHL_VB"L_UpperArm","left_upper_arm","left_arm","upperarm",'L',0,true,true,false,false,{MMDHL_VB"L_Clavicle",MMDHL_VB"Spine4",MMDHL_VB"Spine1"},MMDHL_VB"R_UpperArm","左腕","arm_L"},
 {MMDHL_VB"L_Forearm","left_forearm","left_arm","forearm",'L',0,true,true,false,false,{MMDHL_VB"L_UpperArm"},MMDHL_VB"R_Forearm","左ひじ","elbow_L"},
 {MMDHL_VB"L_Hand","left_hand","left_arm","hand",'L',0,true,true,false,false,{MMDHL_VB"L_Forearm"},MMDHL_VB"R_Hand","左手首","wrist_L"},
 {MMDHL_VB"R_Clavicle","right_shoulder","right_arm","clavicle",'R',0,false,true,true,false,{MMDHL_VB"Spine4",MMDHL_VB"Spine1"},MMDHL_VB"L_Clavicle","右肩","shoulder_R"},
 {MMDHL_VB"R_UpperArm","right_upper_arm","right_arm","upperarm",'R',0,true,true,false,false,{MMDHL_VB"R_Clavicle",MMDHL_VB"Spine4",MMDHL_VB"Spine1"},MMDHL_VB"L_UpperArm","右腕","arm_R"},
 {MMDHL_VB"R_Forearm","right_forearm","right_arm","forearm",'R',0,true,true,false,false,{MMDHL_VB"R_UpperArm"},MMDHL_VB"L_Forearm","右ひじ","elbow_R"},
 {MMDHL_VB"R_Hand","right_hand","right_arm","hand",'R',0,true,true,false,false,{MMDHL_VB"R_Forearm"},MMDHL_VB"L_Hand","右手首","wrist_R"},
 {MMDHL_VB"L_Thigh","left_thigh","left_leg","thigh",'L',0,true,true,false,false,{MMDHL_VB"Pelvis"},MMDHL_VB"R_Thigh","左足","leg_L"},
 {MMDHL_VB"L_Calf","left_lower_leg","left_leg","calf",'L',0,true,true,false,false,{MMDHL_VB"L_Thigh"},MMDHL_VB"R_Calf","左ひざ","knee_L"},
 {MMDHL_VB"L_Foot","left_foot","left_leg","foot",'L',0,true,true,false,false,{MMDHL_VB"L_Calf"},MMDHL_VB"R_Foot","左足首","ankle_L"},
 {MMDHL_VB"L_Toe0","left_toes","left_leg","toe",'L',0,false,false,true,false,{MMDHL_VB"L_Foot"},MMDHL_VB"R_Toe0","左つま先","toe_L"},
 {MMDHL_VB"R_Thigh","right_thigh","right_leg","thigh",'R',0,true,true,false,false,{MMDHL_VB"Pelvis"},MMDHL_VB"L_Thigh","右足","leg_R"},
 {MMDHL_VB"R_Calf","right_lower_leg","right_leg","calf",'R',0,true,true,false,false,{MMDHL_VB"R_Thigh"},MMDHL_VB"L_Calf","右ひざ","knee_R"},
 {MMDHL_VB"R_Foot","right_foot","right_leg","foot",'R',0,true,true,false,false,{MMDHL_VB"R_Calf"},MMDHL_VB"L_Foot","右足首","ankle_R"},
 {MMDHL_VB"R_Toe0","right_toes","right_leg","toe",'R',0,false,false,true,false,{MMDHL_VB"R_Foot"},MMDHL_VB"L_Toe0","右つま先","toe_R"},
 MMDHL_FINGERS("L","left","left_fingers","左","L","R"),
 MMDHL_FINGERS("R","right","right_fingers","右","R","L")};
#undef MMDHL_FINGERS
#undef MMDHL_VB
inline std::span<const SlotInfo> humanoidSlots(){return HumanoidSlots;}
inline const SlotInfo* slotByKey(std::string_view key){for(auto& s:HumanoidSlots)if(key==s.key)return &s;return nullptr;}
// The keys of an import request and of manifest.conversion.boneMap: every slot
// except the eyes. ValveBiped.Bip01_Spine is always synthesized and never mapped.
inline bool mappedSlotKey(std::string_view key){auto s=slotByKey(key);return s&&!s->convertOnly;}
inline constexpr size_t MappedSlotCount=52;

// VRM humanoid bone -> MMD Japanese and English names (vrm.cpp).
struct HumanName {const char* vrm;const char* jp;const char* en;};
inline constexpr HumanName humanNames[]={
 {"hips","下半身","lower body"},{"spine","上半身","upper body"},{"chest","上半身2","upper body2"},{"upperChest","上半身3","upper body3"},
 {"neck","首","neck"},{"head","頭","head"},{"jaw","顎","jaw"},{"leftEye","左目","eye_L"},{"rightEye","右目","eye_R"},
 {"leftShoulder","左肩","shoulder_L"},{"leftUpperArm","左腕","arm_L"},{"leftLowerArm","左ひじ","elbow_L"},{"leftHand","左手首","wrist_L"},
 {"rightShoulder","右肩","shoulder_R"},{"rightUpperArm","右腕","arm_R"},{"rightLowerArm","右ひじ","elbow_R"},{"rightHand","右手首","wrist_R"},
 {"leftUpperLeg","左足","leg_L"},{"leftLowerLeg","左ひざ","knee_L"},{"leftFoot","左足首","ankle_L"},{"leftToes","左つま先","toe_L"},
 {"rightUpperLeg","右足","leg_R"},{"rightLowerLeg","右ひざ","knee_R"},{"rightFoot","右足首","ankle_R"},{"rightToes","右つま先","toe_R"},
 {"leftThumbMetacarpal","左親指０","thumb0_L"},{"leftThumbProximal","左親指１","thumb1_L"},{"leftThumbDistal","左親指２","thumb2_L"},
 {"leftIndexProximal","左人指１","fore1_L"},{"leftIndexIntermediate","左人指２","fore2_L"},{"leftIndexDistal","左人指３","fore3_L"},
 {"leftMiddleProximal","左中指１","middle1_L"},{"leftMiddleIntermediate","左中指２","middle2_L"},{"leftMiddleDistal","左中指３","middle3_L"},
 {"leftRingProximal","左薬指１","third1_L"},{"leftRingIntermediate","左薬指２","third2_L"},{"leftRingDistal","左薬指３","third3_L"},
 {"leftLittleProximal","左小指１","little1_L"},{"leftLittleIntermediate","左小指２","little2_L"},{"leftLittleDistal","左小指３","little3_L"},
 {"rightThumbMetacarpal","右親指０","thumb0_R"},{"rightThumbProximal","右親指１","thumb1_R"},{"rightThumbDistal","右親指２","thumb2_R"},
 {"rightIndexProximal","右人指１","fore1_R"},{"rightIndexIntermediate","右人指２","fore2_R"},{"rightIndexDistal","右人指３","fore3_R"},
 {"rightMiddleProximal","右中指１","middle1_R"},{"rightMiddleIntermediate","右中指２","middle2_R"},{"rightMiddleDistal","右中指３","middle3_R"},
 {"rightRingProximal","右薬指１","third1_R"},{"rightRingIntermediate","右薬指２","third2_R"},{"rightRingDistal","右薬指３","third3_R"},
 {"rightLittleProximal","右小指１","little1_R"},{"rightLittleIntermediate","右小指２","little2_R"},{"rightLittleDistal","右小指３","little3_R"}};
// VRM 1.0 renamed the thumb (0.x proximal/intermediate/distal = 1.0 metacarpal/proximal/distal).
inline std::string humanName1(std::string name){
 for(auto side:{"left","right"}){auto s=std::string(side);
  if(name==s+"ThumbProximal")return s+"ThumbMetacarpal";if(name==s+"ThumbIntermediate")return s+"ThumbProximal";}
 return name;
}
}
