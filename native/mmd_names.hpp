// Compositional MMD expression names supplement SCMI's exact-name table.
#pragma once
#include "runtime.hpp"
#include <algorithm>
#include <cctype>
namespace mmd {
inline std::string englishMorph(std::string original,const Json& names){
 while(!original.empty()&&std::isspace((unsigned char)original.back()))original.pop_back();
 if(names.contains(original))return names.at(original).get<std::string>();
 static const std::map<std::string,std::string> extra={
 {"上","brows_up"},{"下","brows_down"},{"平行","brows_flat"},{"入","brows_in"},
 {"キリッ","eyes_sharp"},{"たれ目","eyes_droop"},{"笑い目","eyes_smile"},{"悲しい目","eyes_sad"},
 {"恐ろしい子！","eyes_shock"},{"カメラ目","eyes_camera"},{"ハイライト消","eyes_highlight_off"},
 {"ああ","mouth_aa"},{"ワ","mouth_wa"},{"ん","mouth_n"},{"なんで","mouth_why"},
 {"口横狭げ","mouth_narrow"},{"口横広げ","mouth_wide"},{"頬を膨","cheeks_puff"},{"齶","jaw"},{"齶前","jaw_forward"},{"齶上","jaw_up"},
 {"鼻上","nose_up"},{"鼻下","nose_down"},{"口上","mouth_up"},{"口下","mouth_down"},
 {"口","mouth_move"},{"唇中","lips_in"},{"口角前","mouth_corners_forward"},
 {"？","face_question"},{"！","face_exclamation"},{"汗","face_sweat"},{"はちゅ","face_chibi"},{"惊","face_shock"},{"はぁと","eyes_hearts"},
 {"星目","eyes_stars"},{"脸红","cheeks_blush"},{"汗颜","face_sweat"},{"怒","face_angry"},{"！！","face_exclamation_double"},
 {"目影濃","eyes_shadow_dark"},{"目影消","eyes_shadow_off"},{"環ON","halo_show"},{"後紗ON","back_veil_show"},{"頭紗","head_veil"},{"合成大昔漣","variant_large_cyrene"}};
 if(extra.contains(original))return extra.at(original);
 const char* wideDigits[]={"０","１","２","３","４","５","６","７","８","９"};
 for(int i=0;i<10;i++)for(size_t pos=original.find(wideDigits[i]);pos!=std::string::npos;pos=original.find(wideDigits[i],pos+1))original.replace(pos,3,1,char('0'+i));
 std::string side,number;
 if(original.ends_with("左")){side="_left";original.resize(original.size()-3);}else if(original.ends_with("右")){side="_right";original.resize(original.size()-3);}
 while(!original.empty()&&original.back()>='0'&&original.back()<='9'){number=original.back()+number;original.pop_back();}
 std::string base=names.value(original,"");if(base.empty()&&extra.contains(original))base=extra.at(original);
 if(base.empty()&&!original.empty()&&std::all_of(original.begin(),original.end(),[](unsigned char c){return c<128;})){for(unsigned char c:original)base+=std::isalnum(c)?char(std::tolower(c)):'_';}
 if(base.empty())return {};return base+(number.empty()?"":"_"+number)+side;
}
}
