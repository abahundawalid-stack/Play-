package com.virtualapplications.play;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.TreeSet;

/**
 * Bounded, read-only second-pass analyzer for ELF32 little-endian MIPS.
 * Results are static candidates, not proof of instruction boundaries or runtime behavior.
 */
public final class Ps2MipsDeepAnalyzer {
    private static final long U32_MASK = 0xFFFFFFFFL;
    private static final long U32_SPACE = 0x1_0000_0000L;
    private static final int MAX_PROGRAM_HEADERS = 128;
    private static final int MAX_SECTION_HEADERS = 8192;
    private static final int MAX_RETAINED_ADDRESSES = 4096;
    private static final int MAX_RETAINED_EDGES = 4096;
    private static final int MAX_PRINTED_ADDRESSES = 96;
    private static final int MAX_PRINTED_EDGES = 160;
    private static final int MAX_PRINTED_SYMBOLS = 64;
    private static final int MAX_SYMBOL_NAME_BYTES = 256;

    private Ps2MipsDeepAnalyzer() {}

    private static final class Segment {
        final long offset, vaddr, fileSize, memorySize, flags, align;
        Segment(long offset, long vaddr, long fileSize, long memorySize, long flags, long align) {
            this.offset = offset; this.vaddr = vaddr; this.fileSize = fileSize;
            this.memorySize = memorySize; this.flags = flags; this.align = align;
        }
    }

    private static final class Counts {
        long words, branches, directCalls, indirectCalls, directJumps;
        long jrSites, jalrSites, returnCandidates, syscall, breakCount;
        long unmappedControlTargets, invalidControlTargets;
    }

    public static String analyze(byte[] elf) {
        if (elf == null || elf.length < 52) return "MIPS_DEEP=INVALID_SHORT_ELF";
        if ((elf[0] & 255) != 0x7F || elf[1] != 'E' || elf[2] != 'L' || elf[3] != 'F')
            return "MIPS_DEEP=INVALID_MAGIC";
        if ((elf[4] & 255) != 1 || (elf[5] & 255) != 1)
            return "MIPS_DEEP=SKIPPED_REQUIRES_ELF32_LE";

        ByteBuffer b = ByteBuffer.wrap(elf).order(ByteOrder.LITTLE_ENDIAN);
        int type = u16(b, 16), machine = u16(b, 18), ehsize = u16(b, 40);
        if (machine != 8) return "MIPS_DEEP=SKIPPED_MACHINE_" + machine;
        if (ehsize < 52) return "MIPS_DEEP=INVALID_EHDR_SIZE";

        long entry = u32(b, 24), phoff = u32(b, 28), shoff = u32(b, 32);
        int phentsize = u16(b, 42), phnum = u16(b, 44);
        int shentsize = u16(b, 46), shnum = u16(b, 48);
        if (phnum <= 0 || phnum > MAX_PROGRAM_HEADERS || phentsize < 32 ||
            !range(phoff, (long) phentsize * phnum, elf.length)) return "MIPS_DEEP=INVALID_PHDR_TABLE";

        List<Segment> executable = new ArrayList<>();
        List<Segment> loads = new ArrayList<>();
        long rejectedLoads = 0, overlappingLoads = 0, alignmentWarnings = 0;
        for (int i = 0; i < phnum; i++) {
            int p = (int) phoff + i * phentsize;
            long pType = u32(b, p), off = u32(b, p + 4), va = u32(b, p + 8);
            long fileSize = u32(b, p + 16), memorySize = u32(b, p + 20);
            long flags = u32(b, p + 24), align = u32(b, p + 28);
            if (pType != 1) continue;
            if (fileSize > memorySize || !range(off, fileSize, elf.length) ||
                va + memorySize > U32_SPACE || va + memorySize < va) {
                rejectedLoads++; continue;
            }
            if (align > 1 && ((align & (align - 1)) != 0 || ((va - off) & (align - 1)) != 0))
                alignmentWarnings++;
            Segment s = new Segment(off, va, fileSize, memorySize, flags, align);
            for (Segment old : loads) {
                long a0 = s.vaddr, a1 = s.vaddr + s.memorySize;
                long b0 = old.vaddr, b1 = old.vaddr + old.memorySize;
                if (a0 < b1 && b0 < a1) overlappingLoads++;
            }
            loads.add(s);
            if ((flags & 1L) != 0 && fileSize >= 4) executable.add(s);
        }
        if (executable.isEmpty()) return "MIPS_DEEP=NO_VALID_FILE_BACKED_EXECUTABLE_SEGMENT";

        TreeSet<Long> blockStarts = new TreeSet<>();
        TreeSet<Long> callTargets = new TreeSet<>();
        TreeSet<Long> indirectSites = new TreeSet<>();
        TreeSet<String> edges = new TreeSet<>();
        Counts c = new Counts();
        long droppedAddresses = 0, droppedEdges = 0;

        for (Segment s : executable) {
            long words = s.fileSize / 4;
            for (long i = 0; i < words; i++) {
                long pc = (s.vaddr + i * 4) & U32_MASK;
                long word = u32(b, (int) (s.offset + i * 4));
                c.words++;
                int op = (int) (word >>> 26), rs = (int) ((word >>> 21) & 31);
                int rt = (int) ((word >>> 16) & 31), rd = (int) ((word >>> 11) & 31);
                int fn = (int) (word & 63);
                boolean conditional = false, directCall = false, directJump = false;
                boolean indirect = false, indirectCall = false, terminalReturn = false;
                long target = -1;

                if (op == 2 || op == 3) {
                    directCall = op == 3; directJump = op == 2;
                    target = (((pc + 4) & 0xF0000000L) | ((word & 0x03FFFFFFL) << 2)) & U32_MASK;
                } else if (op == 4 || op == 5 || op == 6 || op == 7 ||
                           op == 20 || op == 21 || op == 22 || op == 23) {
                    conditional = true;
                } else if (op == 1 && (rt == 0 || rt == 1 || rt == 2 || rt == 3 ||
                                       rt == 16 || rt == 17 || rt == 18 || rt == 19)) {
                    conditional = true;
                    directCall = rt >= 16;
                } else if (op == 0 && (fn == 8 || fn == 9)) {
                    indirect = true; indirectCall = fn == 9 && rd != 0;
                    terminalReturn = fn == 8 && rs == 31;
                } else if (op == 0 && fn == 12) c.syscall++;
                else if (op == 0 && fn == 13) c.breakCount++;

                if (conditional) {
                    c.branches++;
                    target = (pc + 4 + (((long) (short) word) << 2)) & U32_MASK;
                    droppedAddresses += addBounded(blockStarts, target, MAX_RETAINED_ADDRESSES) ? 0 : 1;
                    droppedAddresses += addBounded(blockStarts, (pc + 8) & U32_MASK, MAX_RETAINED_ADDRESSES) ? 0 : 1;
                    if (mapped(target, executable)) droppedEdges += addBounded(edges, edge(pc, target, "branch"), MAX_RETAINED_EDGES) ? 0 : 1;
                    else c.unmappedControlTargets++;
                    long fall = (pc + 8) & U32_MASK;
                    if (mapped(fall, executable)) droppedEdges += addBounded(edges, edge(pc, fall, "fallthrough"), MAX_RETAINED_EDGES) ? 0 : 1;
                }
                if (directCall) {
                    c.directCalls++;
                    if (op == 1) target = (pc + 4 + (((long) (short) word) << 2)) & U32_MASK;
                    if (addBounded(callTargets, target, MAX_RETAINED_ADDRESSES)) { /* retained */ }
                    else droppedAddresses++;
                    if (mapped(target, executable)) {
                        droppedAddresses += addBounded(blockStarts, target, MAX_RETAINED_ADDRESSES) ? 0 : 1;
                        droppedEdges += addBounded(edges, edge(pc, target, "direct-call"), MAX_RETAINED_EDGES) ? 0 : 1;
                    } else c.unmappedControlTargets++;
                    // A call's architectural return continuation is after its delay slot.
                    droppedAddresses += addBounded(blockStarts, (pc + 8) & U32_MASK, MAX_RETAINED_ADDRESSES) ? 0 : 1;
                } else if (directJump) {
                    c.directJumps++;
                    if (mapped(target, executable)) {
                        droppedAddresses += addBounded(blockStarts, target, MAX_RETAINED_ADDRESSES) ? 0 : 1;
                        droppedEdges += addBounded(edges, edge(pc, target, "jump"), MAX_RETAINED_EDGES) ? 0 : 1;
                    } else c.unmappedControlTargets++;
                }
                if (indirect) {
                    if (!addBounded(indirectSites, pc, MAX_RETAINED_ADDRESSES)) droppedAddresses++;
                    if (indirectCall) c.indirectCalls++;
                    else { c.jrSites++; if (terminalReturn) c.returnCandidates++; }
                }
            }
        }
        if (!addBounded(blockStarts, entry, MAX_RETAINED_ADDRESSES)) droppedAddresses++;
        int symbols = 0;
        String symbolSummary = readFunctionSymbols(b, elf.length, shoff, shentsize, shnum);
        int marker = symbolSummary.indexOf('|');
        if (marker >= 0) {
            try { symbols = Integer.parseInt(symbolSummary.substring(0, marker)); }
            catch (NumberFormatException ignored) { symbols = 0; }
            symbolSummary = symbolSummary.substring(marker + 1);
        }

        StringBuilder out = new StringBuilder(8192);
        out.append("MIPS_DEEP{MODEL=BOUNDED_STATIC_CANDIDATES,ELF_TYPE=").append(type)
            .append(",ELF_BYTES=").append(elf.length)
            .append(",LOAD_SEGMENTS=").append(loads.size())
            .append(",EXEC_SEGMENTS=").append(executable.size())
            .append(",REJECTED_LOAD_SEGMENTS=").append(rejectedLoads)
            .append(",OVERLAPPING_LOAD_PAIRS=").append(overlappingLoads)
            .append(",ALIGNMENT_WARNINGS=").append(alignmentWarnings)
            .append(",WORDS_SCANNED=").append(c.words)
            .append(",BRANCHES=").append(c.branches)
            .append(",DIRECT_CALLS_JAL_OR_BAL=").append(c.directCalls)
            .append(",INDIRECT_CALLS_JALR=").append(c.indirectCalls)
            .append(",DIRECT_JUMPS=").append(c.directJumps)
            .append(",JR_SITES=").append(c.jrSites)
            .append(",JR_RA_RETURN_CANDIDATES=").append(c.returnCandidates)
            .append(",SYSCALL_WORDS=").append(c.syscall)
            .append(",BREAK_WORDS=").append(c.breakCount)
            .append(",UNMAPPED_CONTROL_TARGETS=").append(c.unmappedControlTargets)
            .append(",ENTRY_MAPPED_FILE_BACKED_EXEC=").append(mapped(entry, executable))
            .append(",FUNCTION_SYMBOL_COUNT=").append(symbols)
            .append(",FUNCTION_SYMBOLS=").append(symbolSummary)
            .append(",RETAINED_BLOCK_STARTS=").append(blockStarts.size())
            .append(",RETAINED_CALL_TARGETS=").append(callTargets.size())
            .append(",RETAINED_INDIRECT_SITES=").append(indirectSites.size())
            .append(",RETAINED_EDGES=").append(edges.size())
            .append(",DROPPED_ADDRESS_CANDIDATES=").append(droppedAddresses)
            .append(",DROPPED_EDGE_CANDIDATES=").append(droppedEdges)
            .append(",BLOCK_STARTS="); appendLongSet(out, blockStarts, MAX_PRINTED_ADDRESSES);
        out.append(",CALL_TARGETS="); appendLongSet(out, callTargets, MAX_PRINTED_ADDRESSES);
        out.append(",INDIRECT_SITES="); appendLongSet(out, indirectSites, MAX_PRINTED_ADDRESSES);
        out.append(",EDGES="); appendStringSet(out, edges, MAX_PRINTED_EDGES);
        out.append(",LIMITATIONS=NO_FULL_DISASSEMBLY_NO_PROVEN_FUNCTION_BOUNDARIES_NO_INDIRECT_TARGET_RESOLUTION_NO_EXTENDED_SECTION_INDEX_SUPPORT_NO_RUNTIME_PROOF_DATA_BYTES_MAY_RESEMBLE_INSTRUCTIONS}");
        return out.toString();
    }

    private static String readFunctionSymbols(ByteBuffer b, int fileSize, long shoff, int shentsize, int shnum) {
        if (shnum <= 0 || shnum > MAX_SECTION_HEADERS || shentsize < 40 ||
            !range(shoff, (long) shentsize * shnum, fileSize)) return "0|NO_VALID_SECTION_TABLE";
        int total = 0, shown = 0;
        StringBuilder names = new StringBuilder();
        for (int i = 0; i < shnum; i++) {
            int sh = (int) shoff + i * shentsize;
            long sectionType = u32(b, sh + 4), offset = u32(b, sh + 16), size = u32(b, sh + 20);
            long link = u32(b, sh + 24), entsize = u32(b, sh + 36);
            if ((sectionType != 2 && sectionType != 11) || entsize < 16 || size % entsize != 0 ||
                !range(offset, size, fileSize) || link >= shnum) continue;
            int strSh = (int) shoff + (int) link * shentsize;
            long strOff = u32(b, strSh + 16), strSize = u32(b, strSh + 20);
            if (!range(strOff, strSize, fileSize)) continue;
            long count = size / entsize;
            for (long j = 0; j < count; j++) {
                int sym = (int) (offset + j * entsize);
                long nameOff = u32(b, sym), value = u32(b, sym + 4), symSize = u32(b, sym + 8);
                int info = b.get(sym + 12) & 255;
                if ((info & 15) != 2) continue;
                total++;
                if (shown < MAX_PRINTED_SYMBOLS) {
                    String name = readString(b, (int) strOff, (int) strSize, nameOff);
                    if (names.length() > 0) names.append(';');
                    names.append(sanitizeSymbol(name.isEmpty() ? "<unnamed>" : name))
                         .append('@').append(hex(value)).append('+').append(hex(symSize));
                    shown++;
                }
            }
        }
        if (total == 0) return "0|NONE_FOUND_OR_STRIPPED";
        if (total > shown) names.append(";TRUNCATED_").append(total - shown);
        return total + "|" + names;
    }

    private static String sanitizeSymbol(String name) {
        return name.replace(';', '_').replace(',', '_').replace('{', '_')
                   .replace('}', '_').replace('|', '_').replace('\n', '_')
                   .replace('\r', '_');
    }

    private static String readString(ByteBuffer b, int base, int size, long relative) {
        if (relative < 0 || relative >= size) return "";
        int start = base + (int) relative;
        int max = Math.min(size - (int) relative, MAX_SYMBOL_NAME_BYTES);
        int end = start;
        while (end < start + max && b.get(end) != 0) end++;
        byte[] bytes = new byte[end - start];
        for (int i = 0; i < bytes.length; i++) bytes[i] = b.get(start + i);
        return new String(bytes, StandardCharsets.UTF_8);
    }

    private static boolean addBounded(TreeSet<Long> set, long value, int limit) {
        if (set.contains(value)) return true;
        if (set.size() >= limit) return false;
        set.add(value); return true;
    }
    private static boolean addBounded(TreeSet<String> set, String value, int limit) {
        if (set.contains(value)) return true;
        if (set.size() >= limit) return false;
        set.add(value); return true;
    }
    private static boolean mapped(long address, List<Segment> segments) {
        for (Segment s : segments) if (address >= s.vaddr && address - s.vaddr < s.fileSize) return true;
        return false;
    }
    private static String edge(long from, long to, String kind) { return hex(from) + "->" + hex(to) + "[" + kind + "]"; }
    private static void appendLongSet(StringBuilder out, TreeSet<Long> set, int max) {
        int n = 0; for (Long value : set) { if (n >= max) break; if (n++ > 0) out.append(';'); out.append(hex(value)); }
        if (set.size() > max) out.append(";PRINT_TRUNCATED");
    }
    private static void appendStringSet(StringBuilder out, TreeSet<String> set, int max) {
        int n = 0; for (String value : set) { if (n >= max) break; if (n++ > 0) out.append(';'); out.append(value); }
        if (set.size() > max) out.append(";PRINT_TRUNCATED");
    }
    private static boolean range(long offset, long length, long size) {
        return offset >= 0 && length >= 0 && offset <= size && length <= size - offset;
    }
    private static int u16(ByteBuffer b, int offset) { return b.getShort(offset) & 0xFFFF; }
    private static long u32(ByteBuffer b, int offset) { return b.getInt(offset) & U32_MASK; }
    private static String hex(long value) { return String.format(Locale.ROOT, "0x%08X", value & U32_MASK); }
}
