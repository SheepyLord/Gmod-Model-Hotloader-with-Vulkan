#include "model_notes.hpp"
#include "test_platform.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace mmd;
static void check(bool value,const char* what){if(!value)throw std::runtime_error(std::string("Model terms validation failed: ")+what);}
static Bytes encode(const std::wstring& w,unsigned codepage){return encodeCodepage(w,codepage);}
static Bytes utf16(const std::wstring& w,bool bom){Bytes b;if(bom){b.push_back(0xFF);b.push_back(0xFE);}for(wchar_t c:w){b.push_back(uint8_t(c&255));b.push_back(uint8_t(c>>8));}return b;}
static void write(const fs::path& p,const Bytes& b){fs::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),std::streamsize(b.size()));}
static bool validUtf8(const std::string& s){
 for(size_t i=0;i<s.size();){auto c=uint8_t(s[i]);size_t n=c<0x80?1:(c>>5)==6?2:(c>>4)==14?3:(c>>3)==30?4:0;if(!n||i+n>s.size())return false;
  for(size_t k=1;k<n;k++)if((uint8_t(s[i+k])>>6)!=2)return false;i+=n;}
 return true;
}
static Bytes pmx(bool utf8Text,const std::wstring& name,const std::wstring& comment){
 Bytes b={'P','M','X',' '};float version=2.f;auto p=reinterpret_cast<unsigned char*>(&version);b.insert(b.end(),p,p+4);b.push_back(8);
 for(uint8_t g:{uint8_t(utf8Text?1:0),uint8_t(0),uint8_t(4),uint8_t(4),uint8_t(4),uint8_t(4),uint8_t(4),uint8_t(4)})b.push_back(g);
 for(auto& t:{name,std::wstring(L"English"),comment,std::wstring(L"Terms: no redistribution")}){auto e=utf8Text?encode(t,65001):utf16(t,false);int32_t n=int32_t(e.size());auto q=reinterpret_cast<unsigned char*>(&n);b.insert(b.end(),q,q+4);b.insert(b.end(),e.begin(),e.end());}
 return b;
}
static Bytes glb(const std::string& json){
 Bytes b={'g','l','T','F',2,0,0,0,0,0,0,0};std::string padded=json;while(padded.size()%4)padded.push_back(' ');
 uint32_t size=uint32_t(padded.size()),type=0x4E4F534Au;auto a=reinterpret_cast<unsigned char*>(&size),t=reinterpret_cast<unsigned char*>(&type);
 b.insert(b.end(),a,a+4);b.insert(b.end(),t,t+4);b.insert(b.end(),padded.begin(),padded.end());uint32_t total=uint32_t(b.size());std::memcpy(b.data()+8,&total,4);return b;
}
static const Json* readme(const Json& notes,const std::string& name){for(auto& r:notes["readmes"])if(r["name"]==name)return &r;return nullptr;}
int main(){try{
 auto base=fs::temp_directory_path()/("mmdhl-terms-test-"+std::to_string(processId()));
 struct Clean{fs::path dir;~Clean(){std::error_code ec;fs::remove_all(dir,ec);}} clean{base};
 auto pack=base/L"pack",model=pack/L"model";
 // Encodings found in model downloads.
 std::wstring japanese=L"このモデルの再配布は禁止です。\r\nR-18用途での使用を禁止します。\r\n";
 std::wstring chinese=L"禁止转载，禁止用于商业用途。";
 std::string encoding;
 check(decodeText(encode(japanese,932),encoding)==utf8(L"このモデルの再配布は禁止です。\nR-18用途での使用を禁止します。")&&encoding=="shift_jis","Shift-JIS with CRLF");
 check(decodeText(encode(chinese,936),encoding)==utf8(chinese)&&encoding=="gbk","GBK is not mistaken for Shift-JIS");
 check(decodeText(encode(L"禁止轉載，禁止用於商業用途。",950),encoding)==utf8(L"禁止轉載，禁止用於商業用途。")&&encoding=="big5","Big5 is not mistaken for GBK");
 check(decodeText(encode(L"재배포 금지. 상업적 이용 금지.",949),encoding)==utf8(L"재배포 금지. 상업적 이용 금지.")&&encoding=="uhc","UHC Korean");
 check(decodeText(utf16(japanese,true),encoding)==utf8(L"このモデルの再配布は禁止です。\nR-18用途での使用を禁止します。")&&encoding=="utf-16le","UTF-16LE with BOM");
 auto bom=encode(L"﻿No redistribution.",65001);check(decodeText(bom,encoding)=="No redistribution."&&encoding=="utf-8","UTF-8 BOM removed");
 check(decodeText(Bytes{'a','\0','b','\x07','\r','c'},encoding)=="ab\nc","control characters dropped");
 // A model in a folder of its own: every text file counts, readmes first.
 write(model/L"Tester.pmx",pmx(false,L"テスター",L"改変OK・再配布禁止\r\nR-18禁止"));
 write(model/L"readme.txt",encode(japanese,932));
 write(model/L"利用規約.txt",encode(L"﻿商用利用は禁止です。",65001));
 write(model/L"notes.txt",encode(L"Made with love.",65001));
 write(model/L"说明.txt",encode(chinese,936));
 Bytes binary(4096,0);binary[0]='M';write(model/L"data.txt",binary);
 std::string big(100000,'x');for(size_t i=0;i<big.size();i+=10)big[i]='\n';write(model/L"LICENSE",Bytes(big.begin(),big.end()));
 write(pack/L"README_EN.txt",utf16(L"Do not use this model in games.",true));
 write(pack/L"changelog.txt",encode(L"v1.0",65001));
 auto notes=inspectModelNotes(model/L"Tester.pmx");
 check(notes["file"]=="Tester.pmx","file name");
 check(notes["embedded"]["name"]==utf8(L"テスター")&&notes["embedded"]["comment"]==utf8(L"改変OK・再配布禁止\nR-18禁止")&&notes["embedded"]["commentEnglish"]=="Terms: no redistribution","PMX UTF-16 name and comments");
 auto r=readme(notes,"readme.txt");check(r&&(*r)["encoding"]=="shift_jis"&&(*r)["matched"]==true&&(*r)["folder"]=="model","Shift-JIS readme");
 r=readme(notes,utf8(L"利用規約.txt"));check(r&&(*r)["text"]==utf8(L"商用利用は禁止です。"),"terms file with a Japanese name");
 check(readme(notes,"notes.txt")&&(*readme(notes,"notes.txt"))["matched"]==false,"other text beside a lone model");
 r=readme(notes,utf8(L"说明.txt"));check(r&&(*r)["encoding"]=="gbk","GBK readme");
 check(!readme(notes,"data.txt"),"binary files skipped");
 r=readme(notes,"LICENSE");check(r&&(*r)["truncated"]==true&&(*r)["text"].get<std::string>().size()<=48*1024,"long licence truncated");
 r=readme(notes,"README_EN.txt");check(r&&(*r)["folder"]=="parent"&&(*r)["text"]=="Do not use this model in games.","readme in the folder above");
 check(!readme(notes,"changelog.txt"),"unrelated text above the model folder ignored");
 check(notes["readmes"].size()<=8&&notes["readmes"][0]["matched"]==true,"readmes first, at most eight");
 for(auto& d:notes["readmes"])check(validUtf8(d["text"].get<std::string>()),"all text is valid UTF-8");
 check(!notes.dump().empty(),"serializable");
 // A crowded download folder: only files named like readmes.
 auto downloads=base/L"downloads";
 for(int i=0;i<5;i++)write(downloads/(L"m"+std::to_wstring(i)+L".pmx"),pmx(true,L"M",L"UTF-8 comment "+std::to_wstring(i)));
 for(int i=0;i<15;i++)write(downloads/(L"todo"+std::to_wstring(i)+L".txt"),encode(L"private",65001));
 write(downloads/L"Terms of use.md",encode(L"# Terms\nNo sexual or violent use.",65001));
 notes=inspectModelNotes(downloads/L"m0.pmx");
 check(notes["embedded"]["comment"]=="UTF-8 comment 0","PMX UTF-8 comment");
 check(notes["readmes"].size()==1&&notes["readmes"][0]["name"]=="Terms of use.md","crowded folder keeps only named documents");
 // PMD: Shift-JIS name and comment.
 Bytes pmd={'P','m','d'};float v=1.f;auto p=reinterpret_cast<unsigned char*>(&v);pmd.insert(pmd.end(),p,p+4);
 auto field=[&](const std::wstring& w,size_t n){auto e=encode(w,932);e.resize(n,0);pmd.insert(pmd.end(),e.begin(),e.end());};
 field(L"初音ミク",20);field(L"配布禁止です",256);write(base/L"pmd"/L"old.pmd",pmd);
 notes=inspectModelNotes(base/L"pmd"/L"old.pmd");
 check(notes["embedded"]["name"]==utf8(L"初音ミク")&&notes["embedded"]["comment"]==utf8(L"配布禁止です"),"PMD Shift-JIS fields");
 // VRM 0.x and 1.0 licences, and the glTF copyright line.
 write(base/L"vrm"/L"a.vrm",glb(R"({"asset":{"version":"2.0","copyright":"(c) Tester"},"extensions":{"VRM":{"meta":{"title":"Avatar","author":"Tester","licenseName":"Redistribution_Prohibited","allowedUserName":"OnlyAuthor","violentUssageName":"Disallow","sexualUssageName":"Disallow","commercialUssageName":"Disallow"}}}})"));
 notes=inspectModelNotes(base/L"vrm"/L"a.vrm");
 auto& m0=notes["vrm"]["meta"];
 check(notes["vrm"]["version"]=="0.x"&&m0["allowRedistribution"]==false&&m0["sexualUsage"]=="Disallow"&&m0["violentUsage"]=="Disallow"&&m0["allowedUser"]=="OnlyAuthor"&&m0["title"]=="Avatar","VRM 0.x licence");
 check(notes["embedded"]["copyright"]=="(c) Tester","glTF copyright");
 write(base/L"vrm1"/L"b.vrm",glb(R"({"asset":{"version":"2.0"},"extensions":{"VRMC_vrm":{"specVersion":"1.0","meta":{"name":"B","authors":["C"],"licenseUrl":"https://vrm.dev/licenses/1.0/","allowExcessivelySexualUsage":false,"allowRedistribution":false,"avatarPermission":"onlyAuthor"}}}})"));
 notes=inspectModelNotes(base/L"vrm1"/L"b.vrm");
 auto& m1=notes["vrm"]["meta"];
 check(notes["vrm"]["version"]=="1.0"&&m1["allowRedistribution"]==false&&m1["allowExcessivelySexualUsage"]==false&&m1["avatarPermission"]=="onlyAuthor"&&m1["authors"]==Json::array({"C"}),"VRM 1.0 licence");
 write(base/L"glb"/L"prop.glb",glb(R"({"asset":{"version":"2.0","copyright":"CC BY 4.0 Someone"}})"));
 notes=inspectModelNotes(base/L"glb"/L"prop.glb");
 check(!notes.contains("vrm")&&notes["embedded"]["copyright"]=="CC BY 4.0 Someone","plain glTF: copyright only");
 bool rejected=false;try{inspectModelNotes(base/L"missing.pmx");}catch(...){rejected=true;}check(rejected,"missing file reported");
 // The path comes from Lua: only beside a local model file, never on another computer.
 write(base/L"obj"/L"prop.obj",encode(L"v 0 0 0\n",65001));write(base/L"obj"/L"readme.txt",encode(L"Terms",65001));
 check(inspectModelNotes(base/L"obj"/L"prop.obj")["readmes"].size()==1,"a static prop's readme");
 for(auto path:{base/L"obj"/L"readme.txt",fs::path(L"\\\\127.0.0.1\\mmdhl-test\\a.pmx"),fs::path(L"//127.0.0.1/mmdhl-test/a.pmx"),fs::path(L"\\\\?\\UNC\\127.0.0.1\\s\\a.pmx"),fs::path(L"a.pmx")}){
  rejected=false;try{inspectModelNotes(path);}catch(...){rejected=true;}check(rejected,"notes read beside a text file, a network path or a relative path");
 }
 std::cout<<"PASS: readme discovery (own folder, folder above, crowded folders), Shift-JIS/GBK/Big5/UHC/UTF-16/UTF-8 text, PMX/PMD comments, VRM 0.x/1.0 licences, glTF copyright, local model files only\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
