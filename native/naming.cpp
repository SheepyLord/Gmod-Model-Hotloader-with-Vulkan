#include "runtime.hpp"
#include <icu.h>
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace mmd {
// Source and several Lua tools lowercase paths byte-by-byte. UTF-8 filenames
// can consequently become invalid (for example 星 and 涟). Keep engine paths
// ASCII; the untouched authored names remain in the material/rig metadata.
std::string readableName(std::string_view input,size_t maxBytes){
    struct Transliterator {
        UTransliterator* value=nullptr;
        Transliterator(){
            UErrorCode status=U_ZERO_ERROR;
            value=utrans_openU(u"Any-Latin; Latin-ASCII",-1,UTRANS_FORWARD,nullptr,0,nullptr,&status);
            if(U_FAILURE(status))throw std::runtime_error("Cannot initialize Unicode path transliteration");
        }
        ~Transliterator(){utrans_close(value);}
    };
    thread_local Transliterator transliterator;
    auto original=wide(input);
    // Only a short prefix can contribute to a path; do not truncate a surrogate.
    if(original.size()>256){original.resize(256);if(original.back()>=0xd800&&original.back()<=0xdbff)original.pop_back();}
    std::vector<UChar> buffer;
    int32_t length=0;
    for(size_t capacity=std::max<size_t>(256,original.size()*8+1);;){
        buffer.assign(capacity,0);std::copy(original.begin(),original.end(),buffer.begin());
        length=int32_t(original.size());int32_t limit=length;UErrorCode status=U_ZERO_ERROR;
        utrans_transUChars(transliterator.value,buffer.data(),&length,int32_t(buffer.size()),0,&limit,&status);
        if(status==U_BUFFER_OVERFLOW_ERROR){capacity=std::max<size_t>(capacity*2,size_t(length)+1);continue;}
        if(U_FAILURE(status))throw std::runtime_error("Cannot transliterate model/material name");
        break;
    }
    std::string out;bool separator=false;
    for(int32_t i=0;i<length;i++){
        auto c=buffer[i];
        if(c>=128||!(std::isalnum(static_cast<unsigned char>(c))||c=='-')){separator=!out.empty();continue;}
        if(out.size()+1+(separator?1:0)>maxBytes)break;
        if(separator)out+='_';separator=false;out+=char(std::tolower(static_cast<unsigned char>(c)));
    }
    return out.empty()?"unnamed":out;
}

std::string materialPath(const std::string& id,size_t slot,std::string_view name){
    // Common authored material terms, longest match first. Unknown words are
    // romanized, rather than guessed or replaced with an anonymous slot number.
    static const std::pair<std::string_view,std::string_view> terms[]={
        {"ハイライト","highlight"},{"アイライン","eyeliner"},{"まつげ","lashes"},{"まゆ毛","brows"},
        {"スカート","skirt"},{"リボン","ribbon"},{"アクセサリ","accessory"},{"睫眉","lashes_brows"},{"眉睫","brows_lashes"},
        {"前髪","bangs"},{"後髪","back_hair"},{"白目","eye_white"},{"眼白","eye_white"},{"眼睛","eyes"},{"目光","eye_highlight"},{"目影","eye_shadow"},
        {"髮飾","hair_accessory"},{"髪飾","hair_accessory"},{"发饰","hair_accessory"},{"头发","hair"},{"頭髮","hair"},
        {"頭紗","veil"},{"头饰","head_accessory"},{"頭飾","head_accessory"},{"手臂","arm"},{"手套","gloves"},{"指甲","nails"},
        {"上衣","top"},{"内裤","underwear"},{"内褲","underwear"},{"鞋子","shoes"},{"裙子","skirt"},{"肩甲","shoulder_armor"},
        {"帽子","hat"},{"眉毛","brows"},{"牙齿","teeth"},{"牙齒","teeth"},{"舌头","tongue"},{"舌頭","tongue"},
        {"腰带","belt"},{"腰帶","belt"},{"腿环","leg_band"},{"腿環","leg_band"},{"金属","metal"},{"金屬","metal"},
        {"透明","transparent"},{"宝石","gem"},{"寶石","gem"},{"耳环","earring"},{"耳環","earring"},{"戒指","ring"},
        {"纹饰","pattern"},{"紋飾","pattern"},{"表情","expression"},{"照れ","blush"},
        {"顔","face"},{"顏","face"},{"颜","face"},{"脸","face"},{"臉","face"},{"髪","hair"},{"髮","hair"},
        {"眉","brow"},{"睫","lashes"},{"目","eye"},{"瞳","iris"},{"口","mouth"},{"舌","tongue"},{"齿","teeth"},{"齒","teeth"},{"歯","teeth"},
        {"肌","skin"},{"体","body"},{"體","body"},{"首","neck"},{"衣","clothes"},{"裙","skirt"},{"袖","sleeve"},{"鞋","shoes"},{"袜","stockings"},{"襪","stockings"},
        {"饰","accessory"},{"飾","accessory"},{"链","chain"},{"鏈","chain"},{"环","ring"},{"環","ring"},{"纱","gauze"},{"紗","gauze"},{"带","ribbon"},{"帶","ribbon"},
        {"花","flower"},{"叶","leaf"},{"葉","leaf"},{"星","star"},{"羽","feather"},{"甲","armor"},
        {"後","back"},{"后","back"},{"前","front"},{"内","inner"},{"外","outer"},{"白","white"},{"黒","black"},{"黑","black"},{"透","sheer"},{"+","plus"}
    };
    std::string translated;
    for(size_t at=0;at<name.size();){
        std::string_view source,target;
        for(auto& [a,b]:terms)if(a.size()>source.size()&&name.substr(at).starts_with(a)){source=a;target=b;}
        if(!source.empty()){translated+=' ';translated+=target;translated+=' ';at+=source.size();}
        else translated+=name[at++];
    }
    return "mmd/"+id.substr(0,16)+"/"+readableName(translated)+"_"+std::to_string(slot+1);
}
}
