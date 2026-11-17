package com.virtualapplications.play;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.TreeSet;

/** Conservative second-pass static analysis for ELF32 little-endian MIPS/R5900 images.
 *  All addresses are virtual addresses unless explicitly labelled FILE_OFF.
 *  This is a static candidate graph, not a claim of complete function recovery.
 */
public final class Ps2MipsDeepAnalyzer {
    private static final long U32 = 0xffffffffL;
    private static final int MAX_SEGMENTS = 128;
    private static final int MAX_BLOCKS_PRINTED = 160;
    private static final int MAX_EDGES_PRINTED = 300;
    private Ps2MipsDeepAnalyzer() {}

    private static final class Segment {
        long off, va, filesz, memsz, flags;
        Segment(long o,long v,long f,long m,long fl){off=o;va=v;filesz=f;memsz=m;flags=fl;}
    }
    private static final class Insn {
        long pc, word, target;
        int op, rs, rt, rd, fn;
        boolean control, conditional, call, indirect, terminal;
        Insn(long p,long w){
            pc=p; word=w; op=(int)(w>>>26); rs=(int)((w>>>21)&31);
            rt=(int)((w>>>16)&31); rd=(int)((w>>>11)&31); fn=(int)(w&63);
            target=-1;
            if(op==2||op==3){control=true;call=op==3;target=((pc+4)&0xf0000000L)|((w&0x03ffffffL)<<2);terminal=op==2;}
            else if(op==4||op==5||op==6||op==7||op==20||op==21||op==22||op==23){
                control=true;conditional=true;target=pc+4+(((short)w)<<2);
            } else if(op==1){
                int sub=rt;
                if(sub==0||sub==1||sub==2||sub==3||sub==16||sub==17||sub==18||sub==19){
                    control=true;conditional=true;target=pc+4+(((short)w)<<2);
                    call=sub>=16;
                }
            } else if(op==0&&(fn==8||fn==9)){
                control=true;indirect=true;call=fn==9;terminal=fn==8&&rs==31;
            }
        }
        String kind(){
            if(op==3)return "JAL"; if(op==2)return "J";
            if(indirect)return call?"JALR":"JR";
            if(conditional)return "BRANCH";
            if(op==0&&fn==12)return "SYSCALL";
            if(op==0&&fn==13)return "BREAK";
            return "OTHER";
        }
    }

    public static String analyze(byte[] elf) {
        if(elf==null||elf.length<52)return "MIPS_DEEP=INVALID_SHORT_ELF";
        if(elf[0]!=0x7f||elf[1]!='E'||elf[2]!='L'||elf[3]!='F')return "MIPS_DEEP=INVALID_MAGIC";
        if((elf[4]&255)!=1||(elf[5]&255)!=1)return "MIPS_DEEP=SKIPPED_REQUIRES_ELF32_LE";
        ByteBuffer b=ByteBuffer.wrap(elf).order(ByteOrder.LITTLE_ENDIAN);
        int machine=u16(b,18), type=u16(b,16);
        if(machine!=8)return "MIPS_DEEP=SKIPPED_MACHINE_"+machine;
        long entry=u32(b,24), phoff=u32(b,28);
        int phents=u16(b,42), phnum=u16(b,44);
        if(phnum<=0||phnum>MAX_SEGMENTS||phents<32||!range(phoff,(long)phents*phnum,elf.length))return "MIPS_DEEP=INVALID_PHDR_TABLE";
        List<Segment> exec=new ArrayList<>();
        for(int i=0;i<phnum;i++){
            int p=(int)phoff+i*phents;
            long pt=u32(b,p), off=u32(b,p+4), va=u32(b,p+8), fs=u32(b,p+16), ms=u32(b,p+20), fl=u32(b,p+24);
            if(pt==1&&(fl&1)!=0&&fs>=4&&range(off,fs,elf.length)&&ms>=fs)exec.add(new Segment(off,va,fs,ms,fl));
        }
        if(exec.isEmpty())return "MIPS_DEEP=NO_FILE_BACKED_EXECUTABLE_SEGMENT";

        Set<Long> blockStarts=new TreeSet<>(), callTargets=new TreeSet<>(), indirectSites=new TreeSet<>();
        Set<String> edges=new TreeSet<>();
        List<Insn> insns=new ArrayList<>();
        long words=0, branches=0, directCalls=0, indirectCalls=0, jumps=0, returns=0;
        for(Segment s:exec){
            long n=s.filesz/4;
            if(n>8_000_000L-words) n=8_000_000L-words;
            for(long i=0;i<n;i++){
                long pc=(s.va+i*4)&U32; int off=(int)(s.off+i*4);
                Insn x=new Insn(pc,u32(b,off)); insns.add(x); words++;
                if(x.control){
                    if(x.conditional){branches++;blockStarts.add(x.target);blockStarts.add((pc+8)&U32);}
                    else if(x.op==3){directCalls++;callTargets.add(x.target);blockStarts.add(x.target);}
                    else if(x.op==2){jumps++;blockStarts.add(x.target);}
                    else if(x.indirect){indirectSites.add(pc);if(x.call)indirectCalls++;else if(x.terminal)returns++;}
                    if(x.terminal)blockStarts.add((pc+8)&U32);
                }
            }
        }
        blockStarts.add(entry);
        // Keep only addresses actually mapped into file-backed executable bytes.
        Set<Long> validStarts=new TreeSet<>();
        for(Long a:blockStarts)if(mapped(a,exec))validStarts.add(a);
        for(Insn x:insns){
            if(x.conditional&&mapped(x.target,exec))edges.add(hex(x.pc)+" -> "+hex(x.target)+" [branch]");
            if(x.conditional&&mapped((x.pc+8)&U32,exec))edges.add(hex(x.pc)+" -> "+hex((x.pc+8)&U32)+" [fallthrough]");
            if(x.op==3&&mapped(x.target,exec))edges.add(hex(x.pc)+" -> "+hex(x.target)+" [direct-call]");
            if(x.op==2&&mapped(x.target,exec))edges.add(hex(x.pc)+" -> "+hex(x.target)+" [jump]");
        }
        StringBuilder out=new StringBuilder("MIPS_DEEP{MODEL=STATIC_CANDIDATE_ONLY,ELF_TYPE=").append(type)
            .append(",EXEC_SEGMENTS=").append(exec.size()).append(",WORDS_SCANNED=").append(words)
            .append(",BRANCHES=").append(branches).append(",DIRECT_JAL=").append(directCalls)
            .append(",INDIRECT_JALR=").append(indirectCalls).append(",DIRECT_J=").append(jumps)
            .append(",JR_RETURN_CANDIDATES=").append(returns).append(",INDIRECT_JR_JALR_SITES=").append(indirectSites.size())
            .append(",BLOCK_START_CANDIDATES=").append(validStarts.size()).append(",CONTROL_EDGES=").append(edges.size());
        out.append(",ENTRY_MAPPED=").append(mapped(entry,exec));
        out.append(",FUNCTION_SYMBOLS=").append(readFunctionSymbols(elf,b));
        out.append(",CALL_TARGETS="); appendSet(out,callTargets,100);
        out.append(",INDIRECT_SITES="); appendSet(out,indirectSites,80);
        out.append(",BLOCK_STARTS="); appendSet(out,validStarts,MAX_BLOCKS_PRINTED);
        out.append(",EDGES="); int k=0; for(String e:edges){if(k++>=MAX_EDGES_PRINTED)break;if(k>1)out.append(';');out.append(e);} if(edges.size()>MAX_EDGES_PRINTED)out.append(";TRUNCATED");
        out.append(",LIMITATIONS=NO_FULL_R5900_DISASSEMBLY_NO_DATA_FLOW_NO_DYNAMIC_TARGET_RESOLUTION_NO_PROOF_OF_FUNCTION_BOUNDARIES}");
        return out.toString();
    }

    private static int readFunctionSymbols(byte[] elf,ByteBuffer b){
        long shoff=u32(b,32); int shents=u16(b,46), shnum=u16(b,48);
        if(shnum<=0||shnum>8192||shents<40||!range(shoff,(long)shents*shnum,elf.length))return 0;
        int count=0;
        for(int i=0;i<shnum;i++){
            int s=(int)shoff+i*shents; long t=u32(b,s+4), off=u32(b,s+16), size=u32(b,s+20), ent=u32(b,s+36);
            if((t==2||t==11)&&ent>=16&&size%ent==0&&range(off,size,elf.length)){
                long n=size/ent; for(long j=0;j<n;j++){int p=(int)(off+j*ent);long info=u32(b,p+12);if((info&15)==2)count++;}
            }
        }
        return count;
    }
    private static boolean mapped(long va,List<Segment> ss){for(Segment s:ss)if(va>=s.va&&va-s.va<s.filesz)return true;return false;}
    private static void appendSet(StringBuilder o,Set<Long> s,int max){int n=0;for(Long v:s){if(n>=max)break;if(n++>0)o.append(';');o.append(hex(v));}if(s.size()>max)o.append(";TRUNCATED");}
    private static boolean range(long o,long n,long size){return o>=0&&n>=0&&o<=size&&n<=size-o;}
    private static int u16(ByteBuffer b,int o){return b.getShort(o)&0xffff;}
    private static long u32(ByteBuffer b,int o){return b.getInt(o)&U32;}
    private static String hex(long v){return String.format(Locale.ROOT,"0x%08X",v&U32);}
}
