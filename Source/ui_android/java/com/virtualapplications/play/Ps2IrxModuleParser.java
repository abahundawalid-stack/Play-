package com.virtualapplications.play;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

/**
 * PS2 IOP IRX structural parser.
 * Parses the PS2 IRX ELF type, .iopmod metadata, and the PS2SDK import/export
 * table signatures embedded in loadable bytes. Every address mapping and
 * table interpretation is bounds checked. Candidate data is labelled.
 */
public final class Ps2IrxModuleParser {
    private static final long ELF_IRX_TYPE = 0xFF80L;
    private static final long PT_LOAD = 1L;
    private static final long PT_IRXMOD = 0x70000080L;
    private static final long SHT_IRXMOD = 0x70000080L;
    private static final long IMPORT_MAGIC = 0x41E00000L;
    private static final long EXPORT_MAGIC = 0x41C00000L;
    private static final int MAX_PHDR = 256, MAX_SHDR = 8192, MAX_TABLES = 4096;
    private Ps2IrxModuleParser() {}

    private static final class Seg {
        long off, va, fileSize, memSize, flags;
        Seg(long o,long v,long f,long m,long fl){off=o;va=v;fileSize=f;memSize=m;flags=fl;}
    }
    private static final class Section {
        String name=""; long type,addr,off,size,link,info,entsize;
    }
    private static final class Table {
        long fileOff, va, next; int version, mode; String name; int entries;
        String kind;
    }

    public static String analyze(byte[] data) {
        if(data==null || data.length<52) return "PS2_IRX=INVALID_SHORT";
        ByteBuffer b=ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN);
        if((b.get(0)&255)!=0x7f || b.get(1)!='E' || b.get(2)!='L' || b.get(3)!='F') return "PS2_IRX=NOT_ELF";
        if((b.get(4)&255)!=1 || (b.get(5)&255)!=1) return "PS2_IRX=REQUIRES_ELF32_LE";
        int type=u16(b,16), machine=u16(b,18);
        if(type!=(int)ELF_IRX_TYPE) return "PS2_IRX=NOT_IRX_ELF_TYPE_0x"+Integer.toHexString(type);
        if(machine!=8) return "PS2_IRX=IRX_WRONG_MACHINE_"+machine;

        long phoff=u32(b,28), shoff=u32(b,32);
        int phent=u16(b,42), phnum=u16(b,44), shent=u16(b,46), shnum=u16(b,48), shstr=u16(b,50);
        if(phnum>MAX_PHDR || (phnum>0 && (phent<32 || !range(phoff,(long)phent*phnum,data.length))))
            return "PS2_IRX=INVALID_PHDR_TABLE";
        List<Seg> loads=new ArrayList<>();
        long moduleOff=-1, moduleSize=0; String moduleSource="NOT_FOUND";
        for(int i=0;i<phnum;i++) {
            int p=(int)phoff+i*phent; long pt=u32(b,p), off=u32(b,p+4), va=u32(b,p+8), fs=u32(b,p+16), ms=u32(b,p+20), fl=u32(b,p+24);
            if(!range(off,fs,data.length) || fs>ms || va+ms>0x100000000L || va+ms<va) continue;
            if(pt==PT_LOAD) loads.add(new Seg(off,va,fs,ms,fl));
            if(pt==PT_IRXMOD) { moduleOff=off; moduleSize=fs; moduleSource="PT_IRXMOD"; }
        }

        List<Section> sections=new ArrayList<>();
        boolean sectionTableValid=shnum>0 && shnum<=MAX_SHDR && shent>=40 && range(shoff,(long)shent*shnum,data.length) && shstr<shnum;
        if(sectionTableValid) {
            int ns=(int)shoff+shstr*shent; long namesOff=u32(b,ns+16), namesSize=u32(b,ns+20);
            if(range(namesOff,namesSize,data.length)) {
                for(int i=0;i<shnum;i++) {
                    int p=(int)shoff+i*shent; Section s=new Section();
                    s.name=readString(b,(int)namesOff,(int)namesSize,u32(b,p)); s.type=u32(b,p+4);
                    s.addr=u32(b,p+12);s.off=u32(b,p+16);s.size=u32(b,p+20);s.link=u32(b,p+24);s.info=u32(b,p+28);s.entsize=u32(b,p+36);
                    sections.add(s);
                    if(s.type==SHT_IRXMOD && range(s.off,s.size,data.length) && moduleOff<0) {moduleOff=s.off;moduleSize=s.size;moduleSource="SHT_IRXMOD:"+s.name;}
                    if(s.name.equals(".iopmod") && range(s.off,s.size,data.length) && moduleOff<0) {moduleOff=s.off;moduleSize=s.size;moduleSource="SECTION_NAME:.iopmod";}
                }
            } else sectionTableValid=false;
        }

        StringBuilder out=new StringBuilder(8192);
        out.append("PS2_IRX_ANALYSIS{ELF_TYPE=0xFF80;MACHINE=MIPS;LOAD_SEGMENTS=").append(loads.size())
           .append(";SECTION_TABLE=").append(sectionTableValid?"VALID":"ABSENT_OR_INVALID");
        appendModuleInfo(out,data,b,loads,moduleOff,moduleSize,moduleSource);

        List<Table> tables=new ArrayList<>();
        for(Seg s:loads) scanTables(data,b,s,tables);
        // Deduplicate signatures that may be seen in overlapping PT_LOAD segments.
        Set<String> seen=new LinkedHashSet<>(); List<Table> unique=new ArrayList<>();
        for(Table t:tables){String k=t.kind+":"+t.fileOff;if(seen.add(k))unique.add(t);}
        out.append(";LIBRARY_TABLES=").append(unique.size());
        int imports=0,exports=0,shown=0;
        for(Table t:unique) {
            if(t.kind.equals("IMPORT")) imports++; else exports++;
            if(shown++<256) out.append(";TABLE{").append(t.kind).append(";OFF=0x").append(Long.toHexString(t.fileOff))
                .append(";VA=0x").append(Long.toHexString(t.va)).append(";NEXT=0x").append(Long.toHexString(t.next))
                .append(";LIB=").append(clean(t.name)).append(";VERSION=0x").append(Integer.toHexString(t.version))
                .append(";MODE=0x").append(Integer.toHexString(t.mode)).append(";FUNCTION_ENTRIES=").append(t.entries).append("}");
        }
        out.append(";IMPORT_TABLES=").append(imports).append(";EXPORT_TABLES=").append(exports);
        appendRelocations(out,b,data.length,sections,sectionTableValid);
        out.append(";DEPENDENCIES=IMPORT_TABLE_LIBRARY_NAMES;CONFIDENCE=IRX_MAGIC_AND_BOUNDS_VALIDATED;LIMITATIONS=UNRESOLVED_NEXT_POINTERS_OR_DYNAMIC_PATCHES_REPORTED_AS_RAW;NO_RUNTIME_LOAD_PROOF}");
        return out.toString();
    }

    private static void appendModuleInfo(StringBuilder out,byte[] data,ByteBuffer b,List<Seg> loads,long off,long size,String source) {
        if(off<0 || size<0 || !range(off,size,data.length) || size<24) {out.append(";MODULE_INFO=NOT_FOUND");return;}
        int p=(int)off; long moduleIdPtr=u32(b,p), entry=u32(b,p+4), gp=u32(b,p+8), text=u32(b,p+12), dataSize=u32(b,p+16), bss=u32(b,p+20);
        String name=""; int version=-1; long idFile=mapVa(moduleIdPtr,loads);
        if(idFile>=0 && range(idFile,2,data.length)) {
            version=u16(b,(int)idFile);
            name=readCString(data,(int)idFile+2,128);
        }
        out.append(";MODULE_INFO_SOURCE=").append(clean(source)).append(";MODULE_NAME=").append(clean(name.isEmpty()?"UNKNOWN":name))
           .append(";MODULE_ID_PTR=0x").append(Long.toHexString(moduleIdPtr)).append(";ENTRY=0x").append(Long.toHexString(entry))
           .append(";GP=0x").append(Long.toHexString(gp)).append(";TEXT_SIZE=").append(text).append(";DATA_SIZE=").append(dataSize)
           .append(";BSS_SIZE=").append(bss).append(";VERSION=").append(version<0?"UNMAPPED":"0x"+Integer.toHexString(version)).append(";MODULE_INFO_CONFIDENCE=STRUCTURE_BOUNDS_VALIDATED");
    }

    private static void scanTables(byte[] data,ByteBuffer b,Seg seg,List<Table> out) {
        long start=seg.off, end=seg.off+seg.fileSize;
        for(long p=(start+3)&~3L; p+20<=end && out.size()<MAX_TABLES; p+=4) {
            long magic=u32(b,(int)p); if(magic!=IMPORT_MAGIC && magic!=EXPORT_MAGIC) continue;
            Table t=new Table(); t.fileOff=p; t.va=seg.va+(p-seg.off); t.kind=magic==IMPORT_MAGIC?"IMPORT":"EXPORT";
            t.next=u32(b,(int)p+4); t.version=u16(b,(int)p+8); t.mode=u16(b,(int)p+10);
            t.name=ascii(data,(int)p+12,8).replace("\0","").trim();
            if(!printableName(t.name)) continue;
            long tableEnd=end;
            if(t.next!=0) {long n=mapVa(t.next,java.util.Collections.singletonList(seg)); if(n>p && n<=end)tableEnd=n;}
            long q=p+20;
            if(t.kind.equals("IMPORT")) {
                // PS2SDK import stub: jr ra, then addiu zero,zero,ordinal.
                while(q+8<=tableEnd && t.entries<4096) {
                    long a=u32(b,(int)q), z=u32(b,(int)q+4);
                    if(a!=0x03E00008L || (z&0xFFFF0000L)!=0x24000000L) break;
                    t.entries++; q+=8;
                }
            } else {
                while(q+4<=tableEnd && t.entries<4096) {long fn=u32(b,(int)q);if(fn==0)break;t.entries++;q+=4;}
            }
            // A valid header with no functions is still recorded; count remains zero.
            out.add(t);
        }
    }

    private static void appendRelocations(StringBuilder out,ByteBuffer b,int fileSize,List<Section> secs,boolean valid) {
        if(!valid){out.append(";RELOCATIONS=NOT_AVAILABLE_NO_SECTION_TABLE");return;}
        long total=0;StringBuilder summary=new StringBuilder();
        for(int i=0;i<secs.size();i++) {Section s=secs.get(i);if(s.type!=9 && s.type!=4)continue;
            long ent=s.entsize>0?s.entsize:(s.type==9?8:12);
            if(ent<(s.type==9?8:12) || s.size%ent!=0 || !range(s.off,s.size,fileSize))continue;
            long count=s.size/ent;total+=count;long[] types=new long[256];
            for(long n=0;n<count;n++){int p=(int)(s.off+n*ent);long info=u32(b,p+4);types[(int)(info&255)]++;}
            if(summary.length()>0)summary.append(',');summary.append(clean(s.name)).append("#").append(count).append("[MIPS:");
            boolean first=true;for(int t=0;t<types.length;t++)if(types[t]>0){if(!first)summary.append('|');first=false;summary.append(t).append('=').append(types[t]);}summary.append(']');
        }
        out.append(";RELOCATION_ENTRIES=").append(total).append(";RELOCATION_SECTIONS=").append(summary.length()==0?"NONE":summary);
    }

    private static long mapVa(long va,List<Seg> segs) {for(Seg s:segs)if(va>=s.va && va-s.va<s.fileSize)return s.off+(va-s.va);return -1;}
    private static String readString(ByteBuffer b,int base,int size,long rel){if(rel<0||rel>=size)return "";int p=base+(int)rel,e=p;while(e<base+size&&b.get(e)!=0)e++;byte[] x=new byte[e-p];for(int i=0;i<x.length;i++)x[i]=b.get(p+i);return new String(x,StandardCharsets.UTF_8);}
    private static String readCString(byte[] d,int p,int max){if(p<0||p>=d.length)return "";int e=p;while(e<d.length&&e-p<max&&d[e]!=0)e++;return new String(d,p,e-p,StandardCharsets.US_ASCII);}
    private static String ascii(byte[] d,int p,int n){return new String(d,p,n,StandardCharsets.US_ASCII);}
    private static boolean printableName(String s){int n=0;for(int i=0;i<s.length();i++){char c=s.charAt(i);if(c==0)break;if(c<33||c>126)return false;n++;}return n>=2;}
    private static String clean(String s){return s.replaceAll("[^A-Za-z0-9_.-]","_").replaceAll("_+","_");}
    private static int u16(ByteBuffer b,int p){return b.getShort(p)&65535;} private static long u32(ByteBuffer b,int p){return b.getInt(p)&0xffffffffL;}
    private static boolean range(long o,long n,long z){return o>=0&&n>=0&&o<=z&&n<=z-o;}
}
