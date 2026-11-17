import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import com.virtualapplications.play.Ps2MipsR5900Engine;
import com.virtualapplications.play.Ps2IrxModuleParser;

public final class ValidationHarness {
    private static void put16(ByteBuffer b,int p,int v){b.putShort(p,(short)v);}
    private static void put32(ByteBuffer b,int p,long v){b.putInt(p,(int)v);}
    private static byte[] baseElf(int type,int phnum,int fileSize){
        byte[] a=new byte[fileSize]; ByteBuffer b=ByteBuffer.wrap(a).order(ByteOrder.LITTLE_ENDIAN);
        a[0]=0x7f;a[1]='E';a[2]='L';a[3]='F';a[4]=1;a[5]=1;a[6]=1;
        put16(b,16,type);put16(b,18,8);put32(b,20,1);put32(b,24,0x1000);put32(b,28,52);put32(b,32,0);
        put16(b,40,52);put16(b,42,32);put16(b,44,phnum);put16(b,46,40);put16(b,48,0);put16(b,50,0);
        return a;
    }
    private static void ph(ByteBuffer b,int p,long type,long off,long va,long fs,long ms,long flags){
        put32(b,p,type);put32(b,p+4,off);put32(b,p+8,va);put32(b,p+16,fs);put32(b,p+20,ms);put32(b,p+24,flags);put32(b,p+28,16);
    }
    private static byte[] makeExe(){
        byte[] a=baseElf(2,1,0x140);ByteBuffer b=ByteBuffer.wrap(a).order(ByteOrder.LITTLE_ENDIAN);
        ph(b,52,1,0x100,0x1000,36,36,5);
        long[] w={0x24080001L,0x0c000406L,0L,0x03e00008L,0L,0L,0x03e00008L,0L,0L};
        for(int i=0;i<w.length;i++)put32(b,0x100+i*4,w[i]);return a;
    }
    private static byte[] makeIrx(){
        byte[] a=baseElf(0xff80,2,0x500);ByteBuffer b=ByteBuffer.wrap(a).order(ByteOrder.LITTLE_ENDIAN);
        ph(b,52,1,0x100,0x1000,0x300,0x300,5);ph(b,84,0x70000080L,0x180,0x1080,24,24,4);
        put32(b,0x180,0x1100);put32(b,0x184,0x1000);put32(b,0x188,0x1080);put32(b,0x18c,0x100);put32(b,0x190,0x20);put32(b,0x194,0x40);
        put16(b,0x200,0x0102);byte[] name="JPOGDRV".getBytes();System.arraycopy(name,0,a,0x202,name.length);
        put32(b,0x220,0x41e00000L);put32(b,0x224,0);put16(b,0x228,0x0100);put16(b,0x22a,0);System.arraycopy("sifman".getBytes(),0,a,0x22c,6);
        put32(b,0x234,0x03e00008L);put32(b,0x238,0x24000005L);
        put32(b,0x260,0x41c00000L);put32(b,0x264,0);put16(b,0x268,0x0100);put16(b,0x26a,0);System.arraycopy("testlib".getBytes(),0,a,0x26c,7);
        put32(b,0x274,0x1000);put32(b,0x278,0x1010);put32(b,0x27c,0);return a;
    }
    public static void main(String[] args){
        Ps2MipsR5900Engine.Result r=Ps2MipsR5900Engine.analyze(makeExe());
        if(!r.report.contains("FUNCTIONS_DISCOVERED=2") || !r.report.contains("call-continuation") || !r.report.contains("return"))
            throw new AssertionError("R5900 control-flow smoke test failed: "+r.report);
        String irx=Ps2IrxModuleParser.analyze(makeIrx());
        if(!irx.contains("MODULE_NAME=JPOGDRV") || !irx.contains("IMPORT_TABLES=1") || !irx.contains("EXPORT_TABLES=1") || !irx.contains("FUNCTION_ENTRIES=1"))
            throw new AssertionError("IRX structure smoke test failed: "+irx);
        System.out.println("R5900 synthetic ELF: PASS");
        System.out.println("IRX module/import/export synthetic ELF: PASS");
        System.out.println(irx);
    }
}
