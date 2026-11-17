package com.virtualapplications.play;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;

/**
 * Read-only PS2 Emotion Engine MIPS/R5900 static analysis engine.
 * Reachability-based decoding avoids treating every executable-segment word as code.
 * Unknown/reserved encodings are retained as UNKNOWN, never silently accepted.
 * This is a conservative static analyzer, not a proof of runtime execution.
 */
public final class Ps2MipsR5900Engine {
    private static final long U32 = 0xffffffffL;
    private static final int MAX_FUNCS = 50000;
    private static final int MAX_STEPS_PER_FUNC = 250000;
    private static final long MAX_GLOBAL_DECODED_WORDS = 1200000L;
    private static final long MAX_REPORTED_INSTRUCTIONS = 120000L;
    private Ps2MipsR5900Engine() {}

    public static final class Result {
        public final String report;
        public final long decoded, unknown, invalid, codeBytes, dataBytes;
        Result(String r, long d, long u, long i, long c, long data) {
            report=r; decoded=d; unknown=u; invalid=i; codeBytes=c; dataBytes=data;
        }
    }
    private static final class Seg {
        long va, off, fs, ms, flags;
        Seg(long v,long o,long f,long m,long fl){va=v;off=o;fs=f;ms=m;flags=fl;}
    }
    private static final class Ins {
        long pc, word, target=-1; String text, flow="NEXT"; boolean delay, valid;
        Ins(long p,long w,String t,String f,boolean v){pc=p;word=w;text=t;flow=f;valid=v;}
    }
    private static final class Edge {
        long from,to; String kind;
        Edge(long f,long t,String k){from=f;to=t;kind=k;}
        public String toString(){return hx(from)+" -> "+hx(to)+" ["+kind+"]";}
    }
    private static final class Func {
        long entry; TreeMap<Long,Ins> ins=new TreeMap<>(); List<Edge> edges=new ArrayList<>();
        Set<Long> blocks=new TreeSet<>(); boolean incomplete;
        Func(long e){entry=e;}
    }

    public static Result analyze(byte[] elf) {
        if(elf==null || elf.length<52) return new Result("R5900=INVALID_ELF_SHORT",0,0,1,0,0);
        ByteBuffer b=ByteBuffer.wrap(elf).order(ByteOrder.LITTLE_ENDIAN);
        if((b.get(0)&255)!=0x7f || b.get(1)!='E'||b.get(2)!='L'||b.get(3)!='F'||(b.get(4)&255)!=1||(b.get(5)&255)!=1)
            return new Result("R5900=REQUIRES_ELF32_LE",0,0,1,0,0);
        if(u16(b,18)!=8) return new Result("R5900=NOT_MIPS",0,0,1,0,0);
        long entry=u32(b,24), phoff=u32(b,28), shoff=u32(b,32);
        int phent=u16(b,42), phnum=u16(b,44), shent=u16(b,46), shnum=u16(b,48);
        if(phnum==0||phnum>128||phent<32||!range(phoff,(long)phent*phnum,elf.length))
            return new Result("R5900=BAD_PHDR",0,0,1,0,0);
        List<Seg> exec=new ArrayList<>();
        for(int n=0;n<phnum;n++){int p=(int)phoff+n*phent; if(u32(b,p)!=1)continue;
            long off=u32(b,p+4),va=u32(b,p+8),fs=u32(b,p+16),ms=u32(b,p+20),fl=u32(b,p+24);
            if(fs>ms||!range(off,fs,elf.length)||va+ms>0x100000000L||va+ms<va)continue;
            if((fl&1)!=0&&fs>=4)exec.add(new Seg(va,off,fs,ms,fl));
        }
        if(exec.isEmpty())return new Result("R5900=NO_EXEC_LOAD",0,0,1,0,0);

        TreeSet<Long> seeds=new TreeSet<>(); seeds.add(entry);
        // Function symbols are authoritative seeds only when their address maps to file-backed executable bytes.
        if(shnum>0&&shnum<=8192&&shent>=40&&range(shoff,(long)shent*shnum,elf.length)){
            for(int n=0;n<shnum;n++){int s=(int)shoff+n*shent;long typ=u32(b,s+4),off=u32(b,s+16),sz=u32(b,s+20),link=u32(b,s+24),es=u32(b,s+36);
                if((typ!=2&&typ!=11)||es<16||sz%es!=0||!range(off,sz,elf.length)||link>=shnum)continue;
                int st=(int)shoff+(int)link*shent;long sto=u32(b,st+16),sts=u32(b,st+20);if(u32(b,st+4)!=3||!range(sto,sts,elf.length))continue;
                for(long q=0;q<sz/es;q++){int x=(int)(off+q*es);int info=b.get(x+12)&255;long val=u32(b,x+4);if((info&15)==2&&maps(val,exec))seeds.add(val&~3L);}
            }
        }
        Map<Long,Func> funcs=new TreeMap<>(); Set<Long> globallyDecoded=new HashSet<>();
        ArrayDeque<Long> pending=new ArrayDeque<>(seeds);
        long decoded=0,unknown=0,invalid=0,codeBytes=0; long functionCount=0;
        while(!pending.isEmpty() && functionCount<MAX_FUNCS &&
              globallyDecoded.size()<MAX_GLOBAL_DECODED_WORDS){long start=pending.removeFirst()&U32;if(funcs.containsKey(start)||!maps(start,exec))continue;
            Func f=new Func(start);funcs.put(start,f);functionCount++;
            f.blocks.add(start);
            ArrayDeque<Long> work=new ArrayDeque<>();Set<Long> seen=new HashSet<>();work.add(start);int steps=0;
            while(!work.isEmpty() && steps++<MAX_STEPS_PER_FUNC &&
                  globallyDecoded.size()<MAX_GLOBAL_DECODED_WORDS){long pc=work.removeFirst()&U32;
                if(pc!=start && seeds.contains(pc)){f.edges.add(new Edge(pc,pc,"known-function-boundary"));continue;}
                if(!seen.add(pc)||!maps(pc,exec))continue;
                Seg sg=segment(pc,exec);long delta=pc-sg.va;if(delta+4>sg.fs)continue;long fo=sg.off+delta;if(!range(fo,4,elf.length))continue;
                long w=u32(b,(int)fo);Ins in=decode(pc,w);f.ins.put(pc,in);globallyDecoded.add(pc);decoded++;if(in.text.startsWith("unknown"))unknown++;if(!in.valid)invalid++;
                if(in.flow.equals("COND")){long t=branchTarget(pc,w);in.target=t;boolean link=opcodeOf(w)==1 && regimmRt(w)>=16;f.edges.add(new Edge(pc,t,link?"conditional-call":"conditional-branch"));f.edges.add(new Edge(pc,(pc+8)&U32,"fallthrough"));f.blocks.add(t);f.blocks.add((pc+8)&U32);if(link && maps(t,exec) && !funcs.containsKey(t))pending.add(t);if(maps(t,exec))work.add(t);if(maps((pc+8)&U32,exec))work.add((pc+8)&U32);markDelay(f,b,elf,pc+4,exec);}
                else if(in.flow.equals("CALL")){int targetReg=(int)((w>>>21)&31);long t=in.text.startsWith("jalr")?resolveJalrTarget(pc,targetReg,b,exec):jumpTarget(pc,w);in.target=t;f.edges.add(new Edge(pc,t<0?0:t,t<0?"indirect-call-unresolved":(in.text.startsWith("jalr")?"indirect-call-resolved":"call")));f.edges.add(new Edge(pc,(pc+8)&U32,"call-continuation"));f.blocks.add((pc+8)&U32);if(t>=0&&maps(t,exec)&&!funcs.containsKey(t))pending.add(t);markDelay(f,b,elf,pc+4,exec);long cont=(pc+8)&U32;if(maps(cont,exec))work.add(cont);}
                else if(in.flow.equals("JUMP")){long t=jumpTarget(pc,w);in.target=t;f.edges.add(new Edge(pc,t,"jump"));f.blocks.add(t);markDelay(f,b,elf,pc+4,exec);if(maps(t,exec))work.add(t);}
                else if(in.flow.equals("INDIRECT")){int targetReg=(int)((w>>>21)&31);long t=resolveJalrTarget(pc,targetReg,b,exec);in.target=t;f.edges.add(new Edge(pc,t<0?0:t,t<0?"indirect-jump-unresolved":"indirect-jump-resolved"));markDelay(f,b,elf,pc+4,exec);if(t>=0&&maps(t,exec))work.add(t);}
                else if(in.flow.equals("RETURN")){f.edges.add(new Edge(pc,0,"return"));markDelay(f,b,elf,pc+4,exec);}
                else {long next=(pc+4)&U32;if(maps(next,exec))work.add(next);}
            }
            if(steps>=MAX_STEPS_PER_FUNC)f.incomplete=true;
        }
        // Deduplicate overlapping function decodes for byte accounting; retain function-local views and all discovered edges.
        codeBytes=globallyDecoded.size()*4L;long execBytes=0;for(Seg s:exec)execBytes+=s.fs;long dataBytes=Math.max(0,execBytes-codeBytes);
        StringBuilder out=new StringBuilder(32768);
        out.append("R5900_ANALYSIS{MODEL=REACHABILITY_BASED_STATIC;ENTRY=").append(hx(entry)).append(";EXEC_SEGMENTS=").append(exec.size())
           .append(";FUNCTION_SEEDS=").append(seeds.size()).append(";FUNCTIONS_DISCOVERED=").append(funcs.size()).append(";DECODED_UNIQUE_WORDS=").append(globallyDecoded.size())
           .append(";DECODED_BYTES=").append(codeBytes).append(";UNREACHED_EXEC_BYTES_ESTIMATE=").append(dataBytes).append(";UNKNOWN_ENCODINGS=").append(unknown).append(";UNSUPPORTED_OR_INVALID_ENCODINGS=").append(invalid).append("\n");
        long edgeTotal=0,blockTotal=0;for(Func f:funcs.values()){edgeTotal+=f.edges.size();blockTotal+=f.blocks.size();}
        out.append("FUNCTIONS_BEGIN\n");
        long reportedInstructions=0;
        for(Func f:funcs.values()){
            out.append("FUNCTION ").append(hx(f.entry)).append(" INSNS=").append(f.ins.size()).append(" BLOCK_TARGETS=").append(f.blocks.size()).append(" EDGES=").append(f.edges.size()).append(f.incomplete?" INCOMPLETE":"").append('\n');
            for(Ins i:f.ins.values()) {
                if(reportedInstructions++ >= MAX_REPORTED_INSTRUCTIONS) break;
                out.append(String.format(Locale.ROOT,"  %08X  %08X  %-9s %s%s\n",i.pc,i.word,i.flow,i.text,i.delay?" ; delay-slot":""));
            }
            for(Edge e:f.edges)out.append("  EDGE ").append(e).append('\n');
            if(reportedInstructions>=MAX_REPORTED_INSTRUCTIONS) break;
        }
        if(reportedInstructions>=MAX_REPORTED_INSTRUCTIONS)
            out.append("INSTRUCTION_LIST_TRUNCATED_AFTER=").append(MAX_REPORTED_INSTRUCTIONS).append('\n');
        out.append("FUNCTIONS_END\nTOTAL_EDGES=").append(edgeTotal).append(";TOTAL_BLOCK_TARGETS=").append(blockTotal)
           .append(";INDIRECT_TARGETS=LOCAL_LUI_ORI_ADDIU_PATTERN_ONLY;GLOBAL_BUDGET_REACHED=")
           .append(globallyDecoded.size()>=MAX_GLOBAL_DECODED_WORDS)
           .append(";LIMITATIONS=STATIC_REACHABILITY_CANNOT_PROVE_RUNTIME_PATHS_OR_ALL_COMPUTED_JUMPS;UNREACHED_BYTES_ARE_NOT_PROVEN_DATA;COP_AND_VU_SUBDECODERS_PARTIAL;INDIRECT_REGISTER_TARGETS_NOT_YET_PROPAGATED}");
        return new Result(out.toString(),decoded,unknown,invalid,codeBytes,dataBytes);
    }
    /** Resolve only the common local LUI + ORI/ADDIU address materialization pattern.
     * Stops at the first write to the target register; never guesses through loads or control flow.
     */
    private static long resolveJalrTarget(long pc,int reg,ByteBuffer b,List<Seg> segs){
        if(reg==0)return -1;
        long low=0; int lowOp=-1;
        for(int back=1;back<=8;back++){
            long at=(pc-(long)back*4)&U32; Seg s=segment(at,segs); if(s==null)break;
            long d=at-s.va; if(d+4>s.fs)break; long w=u32(b,(int)(s.off+d));
            int op=(int)(w>>>26),rs=(int)(w>>>21&31),rt=(int)(w>>>16&31);
            if(op==15 && rt==reg){
                long high=(w&0xffffL)<<16;
                long target=lowOp<0?high:(lowOp==13?(high|low):(high+low)&U32);
                target&=U32;
                return target!=0 && (target&3)==0 && maps(target,segs)?target:-1;
            }
            if((op==13||op==9||op==8) && rt==reg){
                if(rs!=reg || lowOp>=0)break;
                low=op==13?(w&0xffffL):(long)(short)w; lowOp=op; continue;
            }
            if(writesRt(op) && rt==reg)break;
        }
        return -1;
    }
    private static boolean writesRt(int op){return (op>=8&&op<=18)||(op>=24&&op<=25)||(op>=32&&op<=39)||(op>=48&&op<=55)||(op>=56&&op<=63);}
    private static void markDelay(Func f,ByteBuffer b,byte[] elf,long pc,List<Seg> segs){if(!maps(pc,segs))return;Seg s=segment(pc,segs);long d=pc-s.va;if(d+4>s.fs||!range(s.off+d,4,elf.length))return;Ins x=decode(pc,u32(b,(int)(s.off+d)));x.delay=true;f.ins.put(pc,x);}
    private static Ins decode(long pc,long w){int op=(int)(w>>>26),rs=(int)(w>>>21&31),rt=(int)(w>>>16&31),rd=(int)(w>>>11&31),sa=(int)(w>>>6&31),fn=(int)(w&63);String r="",flow="NEXT";boolean valid=true;
        if(op==0){switch(fn){case 0:r="sll";break;case 2:r="srl";break;case 3:r="sra";break;case 4:r="sllv";break;case 6:r="srlv";break;case 7:r="srav";break;case 8:r="jr $"+rs;flow=rs==31?"RETURN":"INDIRECT";break;case 9:r="jalr $"+rd+",$"+rs;flow="CALL";break;case 10:r="movz";break;case 11:r="movn";break;case 12:r="syscall";break;case 13:r="break";break;case 15:r="sync";break;case 16:r="mfhi";break;case 17:r="mthi";break;case 18:r="mflo";break;case 19:r="mtlo";break;case 20:r="dsllv";break;case 22:r="dsrlv";break;case 23:r="dsrav";break;case 24:r="mult";break;case 25:r="multu";break;case 26:r="div";break;case 27:r="divu";break;case 40:r="mfsa";break;case 41:r="mtsa";break;case 32:r="add";break;case 33:r="addu";break;case 34:r="sub";break;case 35:r="subu";break;case 36:r="and";break;case 37:r="or";break;case 38:r="xor";break;case 39:r="nor";break;case 42:r="slt";break;case 43:r="sltu";break;case 44:r="dadd";break;case 45:r="daddu";break;case 46:r="dsub";break;case 47:r="dsubu";break;case 48:r="tge";break;case 49:r="tgeu";break;case 50:r="tlt";break;case 51:r="tltu";break;case 52:r="teq";break;case 54:r="tne";break;case 56:r="dsll";break;case 58:r="dsrl";break;case 59:r="dsra";break;case 60:r="dsll32";break;case 62:r="dsrl32";break;case 63:r="dsra32";break;default:valid=false;r="unknown.special."+fn;}}
        else if(op==1){r="REGIMM."+rt;flow=(rt==0||rt==1||rt==2||rt==3||rt==16||rt==17||rt==18||rt==19)?"COND":"NEXT";if(flow.equals("NEXT"))valid=false;}
        else if(op==2||op==3){r=(op==2?"j ":"jal ")+hx(jumpTarget(pc,w));flow=op==3?"CALL":"JUMP";}
        else if(op==4||op==5||op==6||op==7||op==20||op==21||op==22||op==23){r="branch.op"+op;flow="COND";}
        else if(op>=8&&op<=15){String[] n={"addi","addiu","slti","sltiu","andi","ori","xori","lui"};r=n[op-8]+" $"+rt+",$"+rs+",0x"+Integer.toHexString((int)(w&65535));}
        else if(op==16||op==17||op==18){int cop=(int)(w>>>21&31);r=(op==16?"cop0":op==17?"cop1":"cop2")+".subdecode_required."+cop;valid=false;}
        else if(op==28){int major=(int)(w&63),sub=(int)((w>>>6)&31);r=mmi(major,sub,rs,rt,rd,sa);if(r.startsWith("unknown")||r.contains("reserved"))valid=false;}
        else if(op==24||op==25){r=(op==24?"daddi":"daddiu")+" $"+rt+",$"+rs+",0x"+Integer.toHexString((int)(w&65535));}
        else if(op==30||op==31){r=(op==30?"lq":"sq")+" $"+rt+", "+(short)w+"($"+rs+")";}
        else if(op==52){r="lqc2 vf"+rt+", "+(short)w+"($"+rs+")";}
        else if(op==60){r="sqc2 vf"+rt+", "+(short)w+"($"+rs+")";}
        else if(op>=32&&op<=63){String[] names={"lb","lh","lwl","lw","lbu","lhu","lwr","lwu","sb","sh","swl","sw","sdl","sdr","swr","cache","ll","lwc1","lwc2","pref","lld","ldc1","ldc2","ld","sc","swc1","swc2","unknown","scd","sdc1","sdc2","sd"};r=names[op-32]+" $"+rt+",imm($"+rs+")";if(r.startsWith("unknown"))valid=false;}
        else {r="unknown.opcode."+op;valid=false;}
        return new Ins(pc,w,r,flow,valid);
    }
    private static String mmi(int major,int sub,int rs,int rt,int rd,int sa){
        if(major==0)return "madd"; if(major==1)return "maddu"; if(major==4)return "plzcw";
        if(major==0x08){String[] n={"paddw","psubw","pcgtw","pmaxw","paddh","psubh","pcgth","pmaxh","paddb","psubb","pcgtb","reserved","reserved","reserved","reserved","reserved","paddsw","psubsw","pextlw","ppacw","paddsh","psubsh","pextlh","ppach","paddsb","psubsb","pextlb","ppacb","reserved","reserved","pext5","ppac5"};return "mmi0."+n[sub];}
        if(major==0x09){String[] n={"pmaddw","reserved","psllvw","psrlvw","pmsubw","reserved","reserved","reserved","pmfhi","pmflo","pinth","reserved","pmultw","pdivw","pcpyld","reserved","pmaddh","phmadh","pand","pxor","pmsubh","phmsbh","reserved","reserved","reserved","reserved","pexeh","prevh","pmulth","pdivbw","pexew","prot3w"};return "mmi2."+n[sub];}
        if(major==0x28){String[] n={"reserved","pabsw","pceqw","pminw","padsbh","pabsh","pceqh","pminh","reserved","reserved","pceqb","reserved","reserved","reserved","reserved","reserved","padduw","psubuw","pextuw","reserved","padduh","psubh","pextuh","reserved","paddub","psubub","pextub","qfsrv","reserved","reserved","reserved","reserved"};return "mmi1."+n[sub];}
        if(major==0x29){String[] n={"pmadduw","reserved","reserved","psravw","reserved","reserved","reserved","reserved","pmthi","pmtlo","pinteh","reserved","pmultuw","pdivuw","pcpyud","reserved","reserved","reserved","por","pnor","reserved","reserved","reserved","reserved","reserved","reserved","pexch","pcpyh","reserved","reserved","pexcw","reserved"};return "mmi3."+n[sub];}
        if(major==0x30)return "pmfhl";if(major==0x31)return "pmthl";if(major==0x34)return "psllh";if(major==0x36)return "psrlh";if(major==0x37)return "psrah";if(major==0x38)return "psllw";if(major==0x3a)return "psrlw";if(major==0x3b)return "psraw";
        if(major==0x12)return "mfhi1";if(major==0x13)return "mthi1";if(major==0x14)return "mflo1";if(major==0x15)return "mtlo1";if(major==0x18)return "mult1";if(major==0x19)return "multu1";if(major==0x1a)return "div1";if(major==0x1b)return "divu1";if(major==0x20)return "madd1";if(major==0x21)return "maddu1";
        return "unknown.mmi.major."+major+".sub."+sub;
    }
    private static int opcodeOf(long w){return (int)(w>>>26)&63;} private static int regimmRt(long w){return (int)(w>>>16)&31;}
    private static long branchTarget(long pc,long w){return (pc+4+((long)(short)w<<2))&U32;}
    private static long jumpTarget(long pc,long w){return (((pc+4)&0xf0000000L)|((w&0x03ffffffL)<<2))&U32;}
    private static boolean maps(long a,List<Seg>s){return (a&3L)==0 && segment(a,s)!=null;}
    private static Seg segment(long a,List<Seg>s){for(Seg x:s)if(a>=x.va&&a-x.va<x.fs)return x;return null;}
    private static int u16(ByteBuffer b,int p){return b.getShort(p)&65535;} private static long u32(ByteBuffer b,int p){return b.getInt(p)&U32;}
    private static boolean range(long o,long n,long z){return o>=0&&n>=0&&o<=z&&n<=z-o;} private static String hx(long x){return String.format(Locale.ROOT,"0x%08X",x&U32);}
}
