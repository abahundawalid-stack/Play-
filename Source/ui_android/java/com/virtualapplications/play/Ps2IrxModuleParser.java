package com.virtualapplications.play;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/** PS2 IOP/IRX structural inspector. Candidates are confidence-labelled, not guessed as facts. */
public final class Ps2IrxModuleParser {
    private Ps2IrxModuleParser() {}
    private static final class Sec {String name;long type,flags,addr,off,size,link,info,align,entsize;}
    public static String analyze(byte[] data) {
        if(data==null||data.length<52)return "IRX=INVALID_SHORT";
        ByteBuffer b=ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN);
        if((b.get(0)&255)!=0x7f||b.get(1)!='E'||b.get(2)!='L'||b.get(3)!='F')return "IRX=NOT_ELF";
        if((b.get(4)&255)!=1||(b.get(5)&255)!=1)return "IRX=REQUIRES_ELF32_LE";
        int machine=u16(b,18),type=u16(b,16);long shoff=u32(b,32);int shent=u16(b,46),shnum=u16(b,48),shstr=u16(b,50);
        StringBuilder out=new StringBuilder("PS2_IRX_ANALYSIS{TYPE=").append(type).append(";MACHINE=").append(machine);
        if(shnum<=0||shnum>8192||shent<40||!range(shoff,(long)shent*shnum,data.length)||shstr>=shnum){
            return out.append(";SECTION_TABLE=INVALID_OR_ABSENT;MODULE_INFO=NOT_FOUND;IMPORTS=NOT_PARSED;EXPORTS=NOT_PARSED;RELOCATIONS=NOT_PARSED}").toString();
        }
        int ns=(int)shoff+shstr*shent;long no=u32(b,ns+16),nz=u32(b,ns+20);
        if(!range(no,nz,data.length))return out.append(";SHSTRTAB=INVALID}").toString();
        List<Sec> secs=new ArrayList<>();
        for(int i=0;i<shnum;i++){int p=(int)shoff+i*shent;Sec s=new Sec();s.name=str(b,(int)no,(int)nz,u32(b,p));s.type=u32(b,p+4);s.flags=u32(b,p+8);s.addr=u32(b,p+12);s.off=u32(b,p+16);s.size=u32(b,p+20);s.link=u32(b,p+24);s.info=u32(b,p+28);s.align=u32(b,p+32);s.entsize=u32(b,p+36);secs.add(s);}
        out.append(";SECTIONS=").append(secs.size());
        Sec mod=null;long relocCount=0;StringBuilder rel=new StringBuilder();
        for(Sec s:secs){if(s.name.equals(".iopmod")||s.name.equals(".module_info")||s.name.equals(".iop_module"))mod=s;
            if(s.type==9||s.type==4){long ent=s.entsize>0?s.entsize:(s.type==9?8:12);if(ent>0&&s.size%ent==0&&range(s.off,s.size,data.length)){relocCount+=s.size/ent;if(rel.length()<1200)rel.append(s.name).append(':').append(s.size/ent).append(',');}}
        }
        if(mod==null){for(Sec s:secs)if(s.size>=16&&s.size<=4096&&s.name.toLowerCase(Locale.ROOT).contains("iop")){mod=s;break;}}
        if(mod!=null&&range(mod.off,mod.size,data.length)&&mod.size>=16){int p=(int)mod.off;int ver=u16(b,p);String name=ascii(data,p+2,8);int flags=u16(b,p+10);long entry=u16(b,p+12),gp=u16(b,p+14);
            out.append(";MODULE_INFO_SECTION=").append(clean(mod.name)).append(";MODULE_NAME=").append(cleanName(name)).append(";MODULE_VERSION=0x").append(Integer.toHexString(ver)).append(";MODULE_FLAGS=0x").append(Integer.toHexString(flags)).append(";ENTRY_FIELD=0x").append(Long.toHexString(entry)).append(";GP_FIELD=0x").append(Long.toHexString(gp)).append(";MODULE_INFO_CONFIDENCE=").append(mod.name.equals(".iopmod")?"HIGH_SECTION_NAME":"MEDIUM_HEURISTIC");
        }else out.append(";MODULE_INFO=NOT_FOUND");
        // PS2 IOP library tables are located using section names where available. Raw signature scanning is reported as candidates only.
        int imports=0,exports=0;StringBuilder importNames=new StringBuilder(),exportNames=new StringBuilder();
        for(Sec s:secs){String n=s.name.toLowerCase(Locale.ROOT);if(!range(s.off,s.size,data.length)||s.size<16)continue;
            if(n.contains("import")||n.contains("stub")){imports+=scanLibraryRecords(data,(int)s.off,(int)s.size,importNames);}
            if(n.contains("export")||n.contains("libent")){exports+=scanLibraryRecords(data,(int)s.off,(int)s.size,exportNames);}
        }
        out.append(";IMPORT_TABLE_SECTION_CANDIDATES=").append(imports).append(";IMPORT_LIBRARY_NAMES=").append(importNames.length()==0?"NONE":importNames)
           .append(";EXPORT_TABLE_SECTION_CANDIDATES=").append(exports).append(";EXPORT_LIBRARY_NAMES=").append(exportNames.length()==0?"NONE":exportNames)
           .append(";RELOCATION_SECTIONS_ENTRIES=").append(relocCount).append(";RELOCATION_INVENTORY=").append(rel.length()==0?"NONE":rel)
           .append(";DEPENDENCY_MODEL=IMPORT_LIBRARY_NAMES_ARE_CANDIDATE_DEPENDENCIES;CONFIDENCE=SECTION_NAMES_AND_MODULE_HEADER_ARE_STRUCTURAL_EVIDENCE;LIMITATIONS=PS2SDK_VARIANTS_AND_RELOCATION_SEMANTICS_REQUIRE_VALIDATED_LAYOUTS}");
        return out.toString();
    }
    private static int scanLibraryRecords(byte[] d,int off,int len,StringBuilder names){int found=0;int end=off+len;
        // Candidate header: 8-byte printable library identifier followed by version/mode/count words.
        for(int p=off;p+14<=end;p+=4){boolean printable=true;int chars=0;for(int j=0;j<8;j++){int c=d[p+j]&255;if(c==0)break;if(c<0x21||c>0x7e){printable=false;break;}chars++;}
            if(!printable||chars<2)continue;int count=(d[p+12]&255)|((d[p+13]&255)<<8);if(count==0||count>512)continue;
            String name=ascii(d,p,8).replace("\0","").trim();if(names.length()>0)names.append(',');names.append(cleanName(name)).append("(count=").append(count).append(")");found++;p+=8;
        }return found;}
    private static String str(ByteBuffer b,int base,int size,long rel){if(rel<0||rel>=size)return "";int p=base+(int)rel,e=p;while(e<base+size&&b.get(e)!=0)e++;byte[] x=new byte[e-p];for(int i=0;i<x.length;i++)x[i]=b.get(p+i);return new String(x,StandardCharsets.UTF_8);}
    private static String ascii(byte[] d,int p,int n){return new String(d,p,n,StandardCharsets.US_ASCII);}
    private static String cleanName(String s){return s.replaceAll("[^A-Za-z0-9_.-]","_").replace("_+","_");}
    private static String clean(String s){return cleanName(s);}
    private static int u16(ByteBuffer b,int p){return b.getShort(p)&65535;}private static long u32(ByteBuffer b,int p){return b.getInt(p)&0xffffffffL;}
    private static boolean range(long o,long n,long z){return o>=0&&n>=0&&o<=z&&n<=z-o;}
}
