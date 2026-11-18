package com.virtualapplications.play;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Read-only ELF32/ELF64 PT_LOAD validator.
 *
 * Separates file-backed bytes from memory-only BSS, checks file and virtual
 * overlaps independently, validates alignment and address arithmetic, and
 * refuses ambiguous VA-to-file mappings.
 */
public final class Ps2ElfMappingValidator {
    private static final long U32_SPACE = 0x1_0000_0000L;
    private static final int MAX_PHDR = 4096;

    private Ps2ElfMappingValidator() {}

    private static final class Segment {
        int index;
        long offset, vaddr, fileSize, memorySize, flags, align;
        long fileEnd, memoryEnd, fileBackedEnd;
    }

    private static final class Mapping {
        String status;
        long fileOffset = -1;
        int matches;
    }

    public static String analyze(byte[] elf) {
        if (elf == null || elf.length < 52) return "ELF_MAP=INVALID_SHORT";
        if ((elf[0] & 255) != 0x7f || elf[1] != 'E' ||
            elf[2] != 'L' || elf[3] != 'F') return "ELF_MAP=INVALID_MAGIC";

        int elfClass = elf[4] & 255;
        int encoding = elf[5] & 255;
        if ((elfClass != 1 && elfClass != 2) ||
            (encoding != 1 && encoding != 2)) {
            return "ELF_MAP=UNSUPPORTED_CLASS_OR_ENDIAN";
        }

        int headerSize = elfClass == 1 ? 52 : 64;
        if (elf.length < headerSize) return "ELF_MAP=INVALID_HEADER_SIZE";

        ByteBuffer b = ByteBuffer.wrap(elf).order(
            encoding == 1 ? ByteOrder.LITTLE_ENDIAN : ByteOrder.BIG_ENDIAN);

        long entry = value(b, elfClass, 24);
        long phoff = value(b, elfClass, elfClass == 1 ? 28 : 32);
        int phentsize = u16(b, elfClass == 1 ? 42 : 54);
        int phnum = u16(b, elfClass == 1 ? 44 : 56);
        int minimumEntrySize = elfClass == 1 ? 32 : 56;

        if (phnum > MAX_PHDR || phentsize < minimumEntrySize ||
            !range(phoff, (long) phentsize * phnum, elf.length)) {
            return "ELF_MAP=INVALID_PHDR_TABLE";
        }

        List<Segment> segments = new ArrayList<>();
        long rejected = 0;
        long badAlignment = 0;
        long fileOverlapPairs = 0;
        long virtualOverlapPairs = 0;
        long fileVirtualConflictPairs = 0;
        long bssBytes = 0;
        long fileBackedBytes = 0;

        for (int i = 0; i < phnum; i++) {
            int p = (int) phoff + i * phentsize;
            if (u32(b, p) != 1) continue; // PT_LOAD

            Segment s = new Segment();
            s.index = i;

            if (elfClass == 1) {
                s.offset = u32(b, p + 4);
                s.vaddr = u32(b, p + 8);
                s.fileSize = u32(b, p + 16);
                s.memorySize = u32(b, p + 20);
                s.flags = u32(b, p + 24);
                s.align = u32(b, p + 28);
            } else {
                s.flags = u32(b, p + 4);
                s.offset = positiveLong(b.getLong(p + 8));
                s.vaddr = positiveLong(b.getLong(p + 16));
                s.fileSize = positiveLong(b.getLong(p + 32));
                s.memorySize = positiveLong(b.getLong(p + 40));
                s.align = positiveLong(b.getLong(p + 48));
            }

            if (s.offset < 0 || s.vaddr < 0 || s.fileSize < 0 ||
                s.memorySize < 0 || s.align < 0 ||
                s.fileSize > s.memorySize ||
                !range(s.offset, s.fileSize, elf.length) ||
                !validAddressRange(s.vaddr, s.memorySize, elfClass)) {
                rejected++;
                continue;
            }

            if (s.align > 1) {
                if (!isPowerOfTwo(s.align) ||
                    ((s.vaddr - s.offset) & (s.align - 1)) != 0) {
                    badAlignment++;
                }
            }

            s.fileEnd = s.offset + s.fileSize;
            s.memoryEnd = s.vaddr + s.memorySize;
            s.fileBackedEnd = s.vaddr + s.fileSize;

            fileBackedBytes += s.fileSize;
            bssBytes += s.memorySize - s.fileSize;
            segments.add(s);
        }

        for (int i = 0; i < segments.size(); i++) {
            Segment a = segments.get(i);
            for (int j = i + 1; j < segments.size(); j++) {
                Segment c = segments.get(j);

                boolean fileOverlap =
                    a.offset < c.fileEnd && c.offset < a.fileEnd;
                boolean virtualOverlap =
                    a.vaddr < c.memoryEnd && c.vaddr < a.memoryEnd;
                boolean backedVirtualOverlap =
                    a.vaddr < c.fileBackedEnd &&
                    c.vaddr < a.fileBackedEnd;

                if (fileOverlap) fileOverlapPairs++;
                if (virtualOverlap) virtualOverlapPairs++;

                // Same virtual bytes backed by different file bytes is a
                // mapping conflict. Identical translation is still overlap,
                // but is not counted as a conflicting translation.
                if (backedVirtualOverlap) {
                    long start = Math.max(a.vaddr, c.vaddr);
                    long aFile = a.offset + (start - a.vaddr);
                    long cFile = c.offset + (start - c.vaddr);
                    if (aFile != cFile) fileVirtualConflictPairs++;
                }
            }
        }

        Mapping entryMapping = mapUnique(entry, 4, segments);
        boolean entryExecutable = false;
        if ("UNIQUE".equals(entryMapping.status)) {
            for (Segment s : segments) {
                if ((s.flags & 1L) != 0 &&
                    entry >= s.vaddr &&
                    entry <= s.fileBackedEnd - 4) {
                    entryExecutable = true;
                    break;
                }
            }
        }

        StringBuilder out = new StringBuilder(2048);
        out.append("ELF_MAP{CLASS=").append(elfClass == 1 ? 32 : 64)
            .append(";ENDIAN=").append(encoding == 1 ? "LE" : "BE")
            .append(";PT_LOAD_VALID=").append(segments.size())
            .append(";PT_LOAD_REJECTED=").append(rejected)
            .append(";FILE_BACKED_BYTES=").append(fileBackedBytes)
            .append(";MEMORY_ONLY_BSS_BYTES=").append(bssBytes)
            .append(";FILE_OVERLAP_PAIRS=").append(fileOverlapPairs)
            .append(";VIRTUAL_MEMORY_OVERLAP_PAIRS=").append(virtualOverlapPairs)
            .append(";CONFLICTING_VA_FILE_TRANSLATIONS=").append(fileVirtualConflictPairs)
            .append(";ALIGNMENT_ERRORS=").append(badAlignment)
            .append(";ENTRY=").append(hex(entry))
            .append(";ENTRY_MAPPING=").append(entryMapping.status)
            .append(";ENTRY_FILE_OFFSET=")
            .append(entryMapping.fileOffset < 0 ? "NONE" : hex(entryMapping.fileOffset))
            .append(";ENTRY_IN_EXECUTABLE_FILE_BACKED_RANGE=").append(entryExecutable);

        for (Segment s : segments) {
            out.append(";LOAD[").append(s.index)
                .append("]{OFF=").append(hex(s.offset))
                .append(";VA=").append(hex(s.vaddr))
                .append(";FILE_END=").append(hex(s.fileEnd))
                .append(";MEM_END=").append(hex(s.memoryEnd))
                .append(";FILE_SIZE=").append(hex(s.fileSize))
                .append(";MEM_SIZE=").append(hex(s.memorySize))
                .append(";BSS_SIZE=").append(hex(s.memorySize - s.fileSize))
                .append(";FLAGS=").append(hex(s.flags))
                .append(";ALIGN=").append(hex(s.align)).append("}");
        }

        out.append(";LIMITATIONS=NO_DYNAMIC_RELOCATION_APPLICATION}");
        return out.toString();
    }

    /**
     * Public mapping result for future analyzers.
     * A mapping is UNIQUE only if the entire requested range is file-backed
     * by exactly one PT_LOAD translation.
     */
    public static String mappingStatus(
        byte[] elf, long virtualAddress, long length) {
        if (elf == null || elf.length < 52) return "INVALID_ELF";
        ByteBuffer b = ByteBuffer.wrap(elf).order(
            (elf[5] & 255) == 2 ? ByteOrder.BIG_ENDIAN : ByteOrder.LITTLE_ENDIAN);
        int cls = elf[4] & 255;
        if ((cls != 1 && cls != 2) || (elf[5] & 255) < 1 ||
            (elf[5] & 255) > 2) return "UNSUPPORTED_ELF";

        long phoff = value(b, cls, cls == 1 ? 28 : 32);
        int entsize = u16(b, cls == 1 ? 42 : 54);
        int count = u16(b, cls == 1 ? 44 : 56);
        if (count > MAX_PHDR || entsize < (cls == 1 ? 32 : 56) ||
            !range(phoff, (long) entsize * count, elf.length)) {
            return "INVALID_PHDR_TABLE";
        }

        List<Segment> segments = readLoads(b, cls, phoff, entsize, count, elf.length);
        return mapUnique(virtualAddress, length, segments).status;
    }

    private static List<Segment> readLoads(
        ByteBuffer b, int cls, long phoff, int entsize, int count, int fileSize) {
        List<Segment> result = new ArrayList<>();
        for (int i = 0; i < count; i++) {
            int p = (int) phoff + i * entsize;
            if (u32(b, p) != 1) continue;
            Segment s = new Segment();
            if (cls == 1) {
                s.offset = u32(b, p + 4); s.vaddr = u32(b, p + 8);
                s.fileSize = u32(b, p + 16); s.memorySize = u32(b, p + 20);
                s.flags = u32(b, p + 24); s.align = u32(b, p + 28);
            } else {
                s.flags = u32(b, p + 4);
                s.offset = positiveLong(b.getLong(p + 8));
                s.vaddr = positiveLong(b.getLong(p + 16));
                s.fileSize = positiveLong(b.getLong(p + 32));
                s.memorySize = positiveLong(b.getLong(p + 40));
                s.align = positiveLong(b.getLong(p + 48));
            }
            if (s.offset < 0 || s.vaddr < 0 || s.fileSize < 0 ||
                s.memorySize < s.fileSize ||
                !range(s.offset, s.fileSize, fileSize) ||
                !validAddressRange(s.vaddr, s.memorySize, cls)) continue;
            s.fileEnd = s.offset + s.fileSize;
            s.memoryEnd = s.vaddr + s.memorySize;
            s.fileBackedEnd = s.vaddr + s.fileSize;
            result.add(s);
        }
        return result;
    }

    private static Mapping mapUnique(
        long address, long length, List<Segment> segments) {
        Mapping result = new Mapping();
        result.status = "UNMAPPED";

        if (address < 0 || length <= 0) {
            result.status = "INVALID_RANGE";
            return result;
        }

        for (Segment s : segments) {
            if (address < s.vaddr) continue;
            long delta = address - s.vaddr;
            if (delta <= s.fileSize && length <= s.fileSize - delta) {
                result.matches++;
                if (result.matches == 1) result.fileOffset = s.offset + delta;
            }
        }

        if (result.matches == 1) result.status = "UNIQUE";
        else if (result.matches > 1) {
            result.status = "AMBIGUOUS";
            result.fileOffset = -1;
        } else {
            for (Segment s : segments) {
                if (address >= s.vaddr && address - s.vaddr < s.memorySize) {
                    result.status = "MEMORY_ONLY_OR_PARTIAL";
                    break;
                }
            }
        }
        return result;
    }

    private static boolean validAddressRange(long start, long size, int cls) {
        if (start < 0 || size < 0 || start > Long.MAX_VALUE - size) return false;
        long end = start + size;
        return cls == 1 ? end <= U32_SPACE : end >= start;
    }

    private static boolean isPowerOfTwo(long value) {
        return value > 0 && (value & (value - 1)) == 0;
    }

    private static long value(ByteBuffer b, int cls, int offset) {
        return cls == 1 ? u32(b, offset) : positiveLong(b.getLong(offset));
    }

    private static long positiveLong(long value) {
        return value < 0 ? -1 : value;
    }

    private static boolean range(long offset, long length, long size) {
        return offset >= 0 && length >= 0 &&
            offset <= size && length <= size - offset;
    }

    private static int u16(ByteBuffer b, int p) {
        return b.getShort(p) & 0xffff;
    }

    private static long u32(ByteBuffer b, int p) {
        return b.getInt(p) & 0xffffffffL;
    }

    private static String hex(long value) {
        return String.format(Locale.ROOT, "0x%X", value);
    }
}
