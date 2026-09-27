// Zstandard frame decoder (RFC 8878) for compressed .blend files. Decoding
// only, no dictionaries; content checksums are skipped. Every read is
// bounds-checked: malformed input throws instead of reading out of range.
#include "zstd_decode.hpp"
#include <array>
#include <cstring>
#include <stdexcept>

namespace props {
namespace {
[[noreturn]] void corrupt(const char* what){throw std::runtime_error(std::string("Corrupt Zstandard data: ")+what);}
int highBit(uint32_t v){int n=-1;while(v){v>>=1;++n;}return n;}
// Bits [pos,pos+n) of a little-endian bit array; positions below zero read as 0.
uint64_t bitsAt(const uint8_t* data,size_t size,int64_t pos,int n){
    if(n==0)return 0;
    uint64_t value=0;
    for(int i=0;i<n;++i){int64_t bit=pos+i;if(bit<0)continue;size_t byte=size_t(bit>>3);if(byte>=size)corrupt("bitstream overrun");value|=uint64_t((data[byte]>>(bit&7))&1)<<i;}
    return value;
}
// Backward bitstream (Huffman literals, FSE sequences): starts after the
// highest set bit of the last byte and reads towards the beginning.
struct BackwardBits {
    const uint8_t* data;size_t size;int64_t pos;
    BackwardBits(const uint8_t* d,size_t n):data(d),size(n){
        if(!n||!d[n-1])corrupt("missing bitstream end marker");
        pos=int64_t(n-1)*8+highBit(d[n-1]);
    }
    uint64_t read(int n){pos-=n;return bitsAt(data,size,pos,n);}
    uint64_t peek(int n)const{return bitsAt(data,size,pos-n,n);}
    void skip(int n){pos-=n;}
    bool overflowed()const{return pos<0;}
    bool finished()const{return pos==0;}
};
// Forward bitstream (FSE table descriptions).
struct ForwardBits {
    const uint8_t* data;size_t size;int64_t pos=0;
    uint64_t read(int n){auto v=bitsAt(data,size,pos,n);pos+=n;return v;}
    uint64_t peek(int n)const{return bitsAt(data,size,pos,n);}
    size_t bytesUsed()const{return size_t((pos+7)>>3);}
};
struct FseCell{uint16_t symbol;uint8_t bits;uint16_t base;};
struct FseTable{int log=0;std::vector<FseCell> cells;};
void buildFse(FseTable& t,const int16_t* counts,int symbols,int log){
    if(log<1||log>15)corrupt("FSE accuracy");
    size_t size=size_t(1)<<log;t.log=log;t.cells.assign(size,{});
    std::vector<uint16_t> next(symbols);size_t high=size-1;
    for(int s=0;s<symbols;++s)if(counts[s]==-1){t.cells[high--].symbol=uint16_t(s);next[s]=1;}else next[s]=uint16_t(counts[s]);
    size_t step=(size>>1)+(size>>3)+3,mask=size-1,position=0;
    for(int s=0;s<symbols;++s)for(int i=0;i<counts[s];++i){t.cells[position].symbol=uint16_t(s);do position=(position+step)&mask;while(position>high);}
    if(position!=0)corrupt("FSE table spread");
    for(size_t i=0;i<size;++i){
        auto s=t.cells[i].symbol;uint32_t state=next[s]++;
        int bits=log-highBit(state);t.cells[i].bits=uint8_t(bits);t.cells[i].base=uint16_t((state<<bits)-size);
    }
}
// Normalized counts (RFC 8878 4.1.1). Returns bytes consumed.
size_t readFseDescription(FseTable& t,const uint8_t* src,size_t size,int maxSymbol,int maxLog){
    ForwardBits in{src,size};
    int log=int(in.read(4))+5;if(log>maxLog)corrupt("FSE accuracy too high");
    std::vector<int16_t> counts(maxSymbol+1,0);
    int remaining=(1<<log)+1,threshold=1<<log,bits=log+1,symbol=0;
    while(remaining>1&&symbol<=maxSymbol){
        int max=(2*threshold-1)-remaining;int count;
        uint64_t low=in.peek(bits-1);
        if(int(low)<max){count=int(low);in.pos+=bits-1;}
        else{count=int(in.peek(bits));if(count>=threshold)count-=max;in.pos+=bits;}
        count-=1;remaining-=count<0?-count:count;counts[symbol++]=int16_t(count);
        if(count==0){
            for(;;){int repeat=int(in.read(2));for(int i=0;i<repeat&&symbol<=maxSymbol;++i)counts[symbol++]=0;if(repeat!=3)break;}
        }
        while(remaining<threshold&&threshold>1){--bits;threshold>>=1;}
    }
    if(remaining!=1)corrupt("FSE counts do not sum to the table size");
    buildFse(t,counts.data(),symbol,log);
    auto used=in.bytesUsed();if(used>size)corrupt("FSE description overrun");
    return used;
}
void rleFse(FseTable& t,uint8_t symbol){t.log=0;t.cells.assign(1,{symbol,0,0});}
struct Huffman{int maxBits=0;std::vector<uint8_t> symbol,bits;};
size_t readHuffman(Huffman& h,const uint8_t* src,size_t size){
    if(!size)corrupt("empty Huffman description");
    uint8_t header=src[0];std::array<uint8_t,256> weights{};size_t count=0,used=0;
    if(header<128){
        size_t compressed=header;if(compressed+1>size)corrupt("Huffman weights overrun");
        FseTable table;auto desc=readFseDescription(table,src+1,compressed,255,6);
        BackwardBits in(src+1+desc,compressed-desc);
        uint32_t s1=uint32_t(in.read(table.log)),s2=uint32_t(in.read(table.log));
        for(;;){
            if(count>=255)corrupt("too many Huffman weights");
            weights[count++]=uint8_t(table.cells[s1].symbol);s1=table.cells[s1].base+uint32_t(in.read(table.cells[s1].bits));
            if(in.overflowed()){weights[count++]=uint8_t(table.cells[s2].symbol);break;}
            if(count>=255)corrupt("too many Huffman weights");
            weights[count++]=uint8_t(table.cells[s2].symbol);s2=table.cells[s2].base+uint32_t(in.read(table.cells[s2].bits));
            if(in.overflowed()){weights[count++]=uint8_t(table.cells[s1].symbol);break;}
        }
        used=1+compressed;
    }else{
        count=header-127;used=1+(count+1)/2;if(used>size)corrupt("Huffman weights overrun");
        for(size_t i=0;i<count;++i){uint8_t b=src[1+i/2];weights[i]=i%2?b&15:b>>4;}
    }
    uint32_t total=0;for(size_t i=0;i<count;++i){if(weights[i]>11)corrupt("Huffman weight");if(weights[i])total+=1u<<(weights[i]-1);}
    if(!total)corrupt("empty Huffman tree");
    int maxBits=highBit(total)+1;uint32_t left=(1u<<maxBits)-total;
    if(left&(left-1))corrupt("Huffman weights are not a power of two");
    weights[count++]=uint8_t(highBit(left)+1);
    if(maxBits>11)corrupt("Huffman code too long");
    h.maxBits=maxBits;size_t tableSize=size_t(1)<<maxBits;h.symbol.assign(tableSize,0);h.bits.assign(tableSize,0);
    // Symbols fill the table by increasing weight, then symbol order.
    std::array<uint32_t,13> start{};uint32_t position=0;
    for(int w=1;w<=maxBits;++w){start[w]=position;for(size_t s=0;s<count;++s)if(weights[s]==w)position+=1u<<(w-1);}
    if(position!=tableSize)corrupt("Huffman table size");
    for(size_t s=0;s<count;++s){int w=weights[s];if(!w)continue;uint32_t n=1u<<(w-1);for(uint32_t i=0;i<n;++i){h.symbol[start[w]+i]=uint8_t(s);h.bits[start[w]+i]=uint8_t(maxBits+1-w);}start[w]+=n;}
    return used;
}
void decodeHuffmanStream(const Huffman& h,const uint8_t* src,size_t size,uint8_t* out,size_t count){
    BackwardBits in(src,size);
    for(size_t i=0;i<count;++i){auto index=size_t(in.peek(h.maxBits));out[i]=h.symbol[index];in.skip(h.bits[index]);}
    if(!in.finished())corrupt("Huffman stream length");
}
constexpr uint32_t LLBase[36]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,18,20,22,24,28,32,40,48,64,128,256,512,1024,2048,4096,8192,16384,32768,65536};
constexpr uint8_t LLBits[36]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,2,2,3,3,4,6,7,8,9,10,11,12,13,14,15,16};
constexpr uint32_t MLBase[53]={3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,37,39,41,43,47,51,59,67,83,99,131,259,515,1027,2051,4099,8195,16387,32771,65539};
constexpr uint8_t MLBits[53]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,2,2,3,3,4,4,5,7,8,9,10,11,12,13,14,15,16};
constexpr int16_t LLDefault[36]={4,3,2,2,2,2,2,2,2,2,2,2,2,1,1,1,2,2,2,2,2,2,2,2,2,3,2,1,1,1,1,1,-1,-1,-1,-1};
constexpr int16_t MLDefault[53]={1,4,3,2,2,2,2,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,-1,-1,-1,-1,-1,-1,-1};
constexpr int16_t OFDefault[29]={1,1,1,1,1,1,2,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,-1,-1,-1,-1,-1};
struct FrameState{Huffman huffman;bool haveHuffman=false;FseTable ll,of,ml;bool haveLL=false,haveOF=false,haveML=false;uint32_t rep[3]={1,4,8};};
size_t readSequenceTable(FseTable& t,bool& have,int mode,const uint8_t* src,size_t size,const int16_t* defaults,int defaultSymbols,int defaultLog,int maxSymbol,int maxLog){
    switch(mode){
    case 0:buildFse(t,defaults,defaultSymbols,defaultLog);have=true;return 0;
    case 1:if(!size)corrupt("RLE sequence table");if(src[0]>maxSymbol)corrupt("RLE sequence symbol");rleFse(t,src[0]);have=true;return 1;
    case 2:{auto used=readFseDescription(t,src,size,maxSymbol,maxLog);have=true;return used;}
    default:if(!have)corrupt("repeated sequence table without a previous one");return 0;
    }
}
[[noreturn]] void tooLarge(){throw std::runtime_error("Compressed .blend expands beyond the size limit");}
// Block_Maximum_Size (RFC 8878) bounds the decompressed size of every block as
// well as its compressed size. Checked before each append, like the caller's
// size limit, so a hostile block fails before it allocates past either bound.
void checkRoom(const Bytes& out,size_t blockStart,size_t maxBlock,uint64_t limit,uint64_t n){
    if(n>maxBlock-(out.size()-blockStart))corrupt("block larger than its maximum size");
    if(n>limit-out.size())tooLarge();
}
void decodeBlock(FrameState& st,const uint8_t* src,size_t size,Bytes& out,size_t frameStart,size_t maxBlock,uint64_t limit){
    const size_t blockStart=out.size();
    // Literals section.
    if(!size)corrupt("empty block");
    int type=src[0]&3,format=(src[0]>>2)&3;size_t regenerated=0,compressed=0,header=0;
    std::vector<uint8_t> literals;
    if(type<2){
        if(format==0||format==2){regenerated=src[0]>>3;header=1;}
        else if(format==1){if(size<2)corrupt("literals header");regenerated=(src[0]>>4)|(size_t(src[1])<<4);header=2;}
        else{if(size<3)corrupt("literals header");regenerated=(src[0]>>4)|(size_t(src[1])<<4)|(size_t(src[2])<<12);header=3;}
        if(regenerated>maxBlock)corrupt("literals size");
        if(type==0){if(header+regenerated>size)corrupt("raw literals overrun");literals.assign(src+header,src+header+regenerated);header+=regenerated;}
        else{if(header+1>size)corrupt("RLE literals overrun");literals.assign(regenerated,src[header]);header+=1;}
    }else{
        int streams=format==0?1:4;uint64_t c=0;
        if(format<2){if(size<3)corrupt("literals header");c=src[0]|(uint64_t(src[1])<<8)|(uint64_t(src[2])<<16);regenerated=(c>>4)&0x3FF;compressed=(c>>14)&0x3FF;header=3;}
        else if(format==2){if(size<4)corrupt("literals header");c=src[0]|(uint64_t(src[1])<<8)|(uint64_t(src[2])<<16)|(uint64_t(src[3])<<24);regenerated=(c>>4)&0x3FFF;compressed=(c>>18)&0x3FFF;header=4;}
        else{if(size<5)corrupt("literals header");c=src[0]|(uint64_t(src[1])<<8)|(uint64_t(src[2])<<16)|(uint64_t(src[3])<<24)|(uint64_t(src[4])<<32);regenerated=(c>>4)&0x3FFFF;compressed=(c>>22)&0x3FFFF;header=5;}
        if(regenerated>maxBlock||header+compressed>size)corrupt("compressed literals size");
        const uint8_t* p=src+header;size_t left=compressed;
        if(type==2){auto used=readHuffman(st.huffman,p,left);st.haveHuffman=true;p+=used;left-=used;}
        else if(!st.haveHuffman)corrupt("treeless literals without a previous Huffman table");
        literals.resize(regenerated);
        if(streams==1)decodeHuffmanStream(st.huffman,p,left,literals.data(),regenerated);
        else{
            if(left<6)corrupt("literal jump table");
            size_t s1=p[0]|(p[1]<<8),s2=p[2]|(p[3]<<8),s3=p[4]|(p[5]<<8);p+=6;left-=6;
            if(s1+s2+s3>left)corrupt("literal stream sizes");
            size_t s4=left-s1-s2-s3,part=(regenerated+3)/4;
            if(3*part>regenerated)corrupt("literal stream split");
            decodeHuffmanStream(st.huffman,p,s1,literals.data(),part);
            decodeHuffmanStream(st.huffman,p+s1,s2,literals.data()+part,part);
            decodeHuffmanStream(st.huffman,p+s1+s2,s3,literals.data()+2*part,part);
            decodeHuffmanStream(st.huffman,p+s1+s2+s3,s4,literals.data()+3*part,regenerated-3*part);
        }
        header+=compressed;
    }
    // Sequences section.
    const uint8_t* p=src+header;size_t left=size-header;
    if(!left)corrupt("missing sequences header");
    size_t sequences=0;
    if(p[0]<128){sequences=p[0];p+=1;left-=1;}
    else if(p[0]<255){if(left<2)corrupt("sequence count");sequences=((size_t(p[0])-128)<<8)+p[1];p+=2;left-=2;}
    else{if(left<3)corrupt("sequence count");sequences=p[1]+(size_t(p[2])<<8)+0x7F00;p+=3;left-=3;}
    size_t literalAt=0;
    if(sequences){
        if(!left)corrupt("sequence modes");
        int modes=p[0];p+=1;left-=1;
        if(modes&3)corrupt("reserved sequence mode bits");
        auto used=readSequenceTable(st.ll,st.haveLL,modes>>6,p,left,LLDefault,36,6,35,9);p+=used;left-=used;
        used=readSequenceTable(st.of,st.haveOF,(modes>>4)&3,p,left,OFDefault,29,5,31,8);p+=used;left-=used;
        used=readSequenceTable(st.ml,st.haveML,(modes>>2)&3,p,left,MLDefault,53,6,52,9);p+=used;left-=used;
        BackwardBits in(p,left);
        uint32_t ll=uint32_t(in.read(st.ll.log)),of=uint32_t(in.read(st.of.log)),ml=uint32_t(in.read(st.ml.log));
        for(size_t n=0;n<sequences;++n){
            auto& lc=st.ll.cells.at(ll);auto& oc=st.of.cells.at(of);auto& mc=st.ml.cells.at(ml);
            uint32_t ofCode=oc.symbol,mlCode=mc.symbol,llCode=lc.symbol;
            if(ofCode>31||mlCode>52||llCode>35)corrupt("sequence code");
            uint32_t offsetValue=(1u<<ofCode)+uint32_t(in.read(int(ofCode)));
            uint32_t matchLength=MLBase[mlCode]+uint32_t(in.read(MLBits[mlCode]));
            uint32_t literalLength=LLBase[llCode]+uint32_t(in.read(LLBits[llCode]));
            uint32_t offset;
            if(offsetValue>3){offset=offsetValue-3;st.rep[2]=st.rep[1];st.rep[1]=st.rep[0];st.rep[0]=offset;}
            else{
                uint32_t index=offsetValue-1+(literalLength==0?1:0);
                if(index==0)offset=st.rep[0];
                else{
                    offset=index==3?st.rep[0]-1:st.rep[index];
                    if(index>1)st.rep[2]=st.rep[1];
                    st.rep[1]=st.rep[0];st.rep[0]=offset;
                }
            }
            if(n+1<sequences){
                ll=lc.base+uint32_t(in.read(lc.bits));
                ml=mc.base+uint32_t(in.read(mc.bits));
                of=oc.base+uint32_t(in.read(oc.bits));
            }
            if(literalAt+literalLength>literals.size())corrupt("literal length overrun");
            checkRoom(out,blockStart,maxBlock,limit,uint64_t(literalLength)+matchLength);
            out.insert(out.end(),literals.begin()+literalAt,literals.begin()+literalAt+literalLength);literalAt+=literalLength;
            if(!offset||offset>out.size()-frameStart)corrupt("match offset before frame start");
            size_t from=out.size()-offset;
            for(uint32_t i=0;i<matchLength;++i)out.push_back(out[from+i]);
        }
        if(!in.finished())corrupt("sequence stream length");
    }
    checkRoom(out,blockStart,maxBlock,limit,literals.size()-literalAt);
    out.insert(out.end(),literals.begin()+literalAt,literals.end());
}
}
Bytes zstdDecompress(std::span<const uint8_t> src,uint64_t limit){
    if(src.empty())corrupt("no frames");
    Bytes out;size_t at=0;
    auto u32=[&](size_t pos){if(pos+4>src.size())corrupt("truncated frame");return uint32_t(src[pos])|uint32_t(src[pos+1])<<8|uint32_t(src[pos+2])<<16|uint32_t(src[pos+3])<<24;};
    while(at<src.size()){
        uint32_t magic=u32(at);
        if((magic&0xFFFFFFF0u)==0x184D2A50u){auto skip=u32(at+4);if(skip>src.size()-at-8)corrupt("skippable frame size");at+=8+skip;continue;}
        if(magic!=0xFD2FB528u)corrupt("unknown frame magic");
        at+=4;if(at>=src.size())corrupt("truncated frame header");
        uint8_t descriptor=src[at++];
        int fcsFlag=descriptor>>6;bool single=(descriptor>>5)&1,checksum=(descriptor>>2)&1;int dictFlag=descriptor&3;
        if(descriptor&8)corrupt("reserved frame bit");
        uint64_t window=0;
        if(!single){if(at>=src.size())corrupt("window descriptor");uint8_t wd=src[at++];int exponent=wd>>3,mantissa=wd&7;uint64_t base=uint64_t(1)<<(10+exponent);window=base+(base/8)*mantissa;}
        size_t dictBytes=dictFlag==0?0:dictFlag==1?1:dictFlag==2?2:4;
        uint32_t dictId=0;for(size_t i=0;i<dictBytes;++i){if(at>=src.size())corrupt("dictionary id");dictId|=uint32_t(src[at++])<<(8*i);}
        if(dictId)throw std::runtime_error("Zstandard dictionaries are not supported");
        size_t fcsBytes=fcsFlag==0?(single?1:0):fcsFlag==1?2:fcsFlag==2?4:8;
        uint64_t contentSize=0;for(size_t i=0;i<fcsBytes;++i){if(at>=src.size())corrupt("content size");contentSize|=uint64_t(src[at++])<<(8*i);}
        if(fcsBytes==2)contentSize+=256;
        if(single)window=contentSize;
        size_t maxBlock=size_t(std::min<uint64_t>(128*1024,std::max<uint64_t>(window,1)));
        FrameState state;size_t frameStart=out.size();
        for(;;){
            if(at+3>src.size())corrupt("truncated block header");
            uint32_t header=src[at]|(uint32_t(src[at+1])<<8)|(uint32_t(src[at+2])<<16);at+=3;
            bool last=header&1;int type=(header>>1)&3;size_t size=header>>3;
            if(type==3)corrupt("reserved block type");
            size_t payload=type==1?1:size;
            if(payload>src.size()-at)corrupt("block overruns input");
            if(size>maxBlock)corrupt("block larger than its maximum size");
            if(type<2&&size>limit-out.size())tooLarge();
            if(type==0)out.insert(out.end(),src.begin()+at,src.begin()+at+size);
            else if(type==1)out.insert(out.end(),size,src[at]);
            else decodeBlock(state,src.data()+at,size,out,frameStart,maxBlock,limit);
            at+=payload;
            if(last)break;
        }
        if(fcsBytes&&out.size()-frameStart!=contentSize)corrupt("frame content size mismatch");
        if(checksum){if(at+4>src.size())corrupt("missing checksum");at+=4;}
    }
    return out;
}
}
