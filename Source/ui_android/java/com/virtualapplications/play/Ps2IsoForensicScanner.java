package com.virtualapplications.play;

import android.content.ContentResolver;
import android.content.Context;
import android.net.Uri;
import android.os.ParcelFileDescriptor;

import java.io.FileInputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.FileChannel;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.TreeSet;

public final class Ps2IsoForensicScanner
{
    private static final int SECTOR_SIZE = 2048;
    private static final int MAX_NESTED_DEPTH = 4;
    private static final long MAX_DIRECTORY_SIZE = 64L * 1024L * 1024L;

    private Ps2IsoForensicScanner()
    {
    }

    public static final class Result
    {
        public String report = "";
        public String csv = "";
        public int files;
        public int directories;
        public int nestedImages;
        public int executables;
        public int errors;
        public long totalBytes;

        private final Set<String> visitedDirectories = new HashSet<>();
    }

    private static final class IsoExtent
    {
        final long lba;
        final long size;

        IsoExtent(long lba, long size)
        {
            this.lba = lba;
            this.size = size;
        }
    }

    private static final class IsoRecord
    {
        String name;
        long lba;
        long size;
        boolean directory;
        boolean multiExtent;

        final List<IsoExtent> extents = new ArrayList<>();

        boolean addExtent(long extentLba, long extentSize)
        {
            if(extentLba < 0 || extentSize < 0 ||
               size > Long.MAX_VALUE - extentSize)
            {
                return false;
            }

            extents.add(new IsoExtent(extentLba, extentSize));

            if(extents.size() == 1)
            {
                lba = extentLba;
            }

            size += extentSize;
            return true;
        }
    }

    public static Result analyze(Context context, Uri uri)
        throws Exception
    {
        Result result = new Result();
        ContentResolver resolver = context.getContentResolver();

        try(ParcelFileDescriptor pfd =
                resolver.openFileDescriptor(uri, "r"))
        {
            if(pfd == null)
            {
                throw new Exception("File ISO tidak dapat dibuka.");
            }

            try(FileInputStream input =
                    new FileInputStream(pfd.getFileDescriptor());
                FileChannel channel = input.getChannel())
            {
                long imageSize = channel.size();

                StringBuilder report = new StringBuilder();
                StringBuilder csv = new StringBuilder();

                report.append("ORBIT2SX - PS2 ISO FORENSIC ANALYZER\n");
                report.append("====================================\n\n");
                report.append("Image size: ").append(imageSize).append(" bytes\n");
                report.append("Sector size: ").append(SECTOR_SIZE).append(" bytes\n");
                report.append("Analysis mode: READ ONLY\n");
                report.append("Filesystem: ISO9660 detection\n\n");

                csv.append("filesystem,path,type,size_bytes,local_lba,")
                    .append("absolute_byte_offset,sha256,signature,details\n");

                scanRegion(
                    channel, 0, imageSize, "ROOT", 0,
                    result, report, csv);

                report.append("\n====================================\n");
                report.append("ANALYSIS SUMMARY\n");
                report.append("====================================\n");
                report.append("Files: ").append(result.files).append('\n');
                report.append("Directories: ").append(result.directories).append('\n');
                report.append("Nested ISO images: ").append(result.nestedImages).append('\n');
                report.append("ELF executables: ").append(result.executables).append('\n');
                report.append("Total file bytes: ").append(result.totalBytes).append('\n');
                report.append("Errors: ").append(result.errors).append('\n');

                report.append("\nNOTES\n");
                report.append("- LBA is relative to its own ISO filesystem.\n");
                report.append("- Absolute byte offset includes nested image offset.\n");
                report.append("- Multi-extent file hashes use extent order.\n");
                report.append("- ELF entry point is a virtual address, not a file offset.\n");
                report.append("- Unknown proprietary formats are not decoded.\n");
                report.append("- Static analysis does not prove runtime compatibility.\n");

                result.report = report.toString();
                result.csv = csv.toString();
            }
        }

        return result;
    }

    private static void scanRegion(
        FileChannel channel,
        long baseOffset,
        long regionSize,
        String label,
        int depth,
        Result result,
        StringBuilder report,
        StringBuilder csv)
        throws Exception
    {
        if(depth > MAX_NESTED_DEPTH)
        {
            report.append("NESTED DEPTH LIMIT: ").append(label).append('\n');
            return;
        }

        long regionEnd = checkedRegionEnd(
            baseOffset, regionSize, channel.size());

        if(regionEnd < 0 || regionSize < 17L * SECTOR_SIZE)
        {
            result.errors++;
            report.append("INVALID REGION: ").append(label).append('\n');
            return;
        }

        byte[] pvd = null;
        String volumeId = "UNKNOWN";
        long volumeSectors = 0;

        long availableSectors = regionSize / SECTOR_SIZE;
        int descriptorLimit = (int)Math.min(64L, availableSectors);

        for(int sector = 16; sector < descriptorLimit; sector++)
        {
            long descriptorOffset = checkedAbsoluteOffset(
                baseOffset, sector);

            if(descriptorOffset < 0 ||
               !isWithinRegion(
                   descriptorOffset, SECTOR_SIZE, baseOffset, regionEnd))
            {
                break;
            }

            byte[] descriptor = readAt(
                channel, descriptorOffset, SECTOR_SIZE);

            if(descriptor == null)
            {
                break;
            }

            if(!isCd001(descriptor))
            {
                continue;
            }

            int type = descriptor[0] & 0xFF;

            if(type == 1)
            {
                pvd = descriptor;
                volumeId = ascii(descriptor, 40, 32).trim();
                volumeSectors = u32Both(descriptor, 80, 84);

                if(volumeSectors <= 0)
                {
                    result.errors++;
                    report.append("INVALID VOLUME SPACE SIZE: ")
                        .append(label).append('\n');
                    return;
                }
                break;
            }

            if(type == 255)
            {
                break;
            }
        }

        if(pvd == null)
        {
            report.append("NOT ISO9660: ").append(label).append('\n');
            return;
        }

        report.append("\n------------------------------------\n");
        report.append("FILESYSTEM: ").append(label).append('\n');
        report.append("Volume ID: ").append(volumeId).append('\n');
        report.append("Volume sectors: ").append(volumeSectors).append('\n');
        report.append("Image base offset: ").append(baseOffset).append('\n');
        report.append("Region size: ").append(regionSize).append('\n');

        long declaredBytes = volumeSectors * SECTOR_SIZE;
        if(declaredBytes > regionSize)
        {
            report.append("WARNING: Declared volume exceeds available region.\n");
        }
        else if(declaredBytes < regionSize)
        {
            report.append("NOTE: Bytes after declared volume are excluded from this filesystem.\n");
            regionSize = declaredBytes;
            regionEnd = checkedRegionEnd(
                baseOffset, regionSize, channel.size());
            if(regionEnd < 0)
            {
                result.errors++;
                report.append("INVALID DECLARED VOLUME BOUNDARY\n");
                return;
            }
        }

        byte[] rootRecord = slice(pvd, 156, 34);
        if(rootRecord == null || (rootRecord[0] & 0xFF) < 34)
        {
            result.errors++;
            report.append("INVALID ROOT DIRECTORY RECORD\n");
            return;
        }

        IsoRecord root = parseRecord(rootRecord);
        if(root == null || !root.directory || root.multiExtent)
        {
            result.errors++;
            report.append("ROOT DIRECTORY INVALID\n");
            return;
        }

        walkDirectory(
            channel, baseOffset, regionSize, label, "/",
            root, depth, result, report, csv);
    }

    private static void walkDirectory(
        FileChannel channel,
        long baseOffset,
        long regionSize,
        String filesystem,
        String path,
        IsoRecord directory,
        int depth,
        Result result,
        StringBuilder report,
        StringBuilder csv)
        throws Exception
    {
        long regionEnd = checkedRegionEnd(
            baseOffset, regionSize, channel.size());

        if(regionEnd < 0)
        {
            result.errors++;
            report.append("REGION END OVERFLOW OR INVALID: ")
                .append(path).append('\n');
            return;
        }

        String directoryKey = filesystem + ":" +
            directory.lba + ":" + directory.size;

        if(!result.visitedDirectories.add(directoryKey))
        {
            report.append("DIRECTORY CYCLE/REPEAT SKIPPED: ")
                .append(path).append('\n');
            return;
        }

        if(directory.size <= 0 ||
           directory.size > MAX_DIRECTORY_SIZE ||
           directory.size > Integer.MAX_VALUE)
        {
            result.errors++;
            report.append("DIRECTORY SIZE INVALID: ")
                .append(path).append('\n');
            return;
        }

        long directoryOffset = checkedAbsoluteOffset(
            baseOffset, directory.lba);

        if(directoryOffset < 0 ||
           !isWithinRegion(
               directoryOffset, directory.size, baseOffset, regionEnd))
        {
            result.errors++;
            report.append("DIRECTORY OUT OF BOUNDS: ")
                .append(path).append('\n');
            return;
        }

        byte[] data = readAt(
            channel, directoryOffset, (int)directory.size);

        if(data == null)
        {
            result.errors++;
            report.append("DIRECTORY READ FAILED: ")
                .append(path).append('\n');
            return;
        }

        result.directories++;
        int position = 0;

        while(position < data.length)
        {
            int sectorPosition = position % SECTOR_SIZE;
            int recordLength = data[position] & 0xFF;

            if(recordLength == 0)
            {
                int skip = SECTOR_SIZE - sectorPosition;
                position += Math.min(skip, data.length - position);
                continue;
            }

            if(recordLength < 34 ||
               recordLength > data.length - position)
            {
                result.errors++;
                report.append("INVALID DIRECTORY RECORD: ")
                    .append(path).append('\n');
                break;
            }

            byte[] recordBytes = new byte[recordLength];
            System.arraycopy(data, position, recordBytes, 0, recordLength);

            IsoRecord entry = parseRecord(recordBytes);
            position += recordLength;

            if(entry == null ||
               ".".equals(entry.name) ||
               "..".equals(entry.name))
            {
                continue;
            }

            String entryPath = path.equals("/")
                ? "/" + entry.name
                : path + "/" + entry.name;

            if(entry.directory && entry.multiExtent)
            {
                result.errors++;
                report.append("MULTI-EXTENT DIRECTORY NOT SUPPORTED: ")
                    .append(entryPath).append('\n');
                continue;
            }

            if(entry.multiExtent)
            {
                boolean complete = false;
                int extentCount = 1;

                while(position < data.length)
                {
                    int nextSectorPosition = position % SECTOR_SIZE;
                    int nextLength = data[position] & 0xFF;

                    if(nextLength == 0)
                    {
                        int skip = SECTOR_SIZE - nextSectorPosition;
                        position += Math.min(skip, data.length - position);
                        continue;
                    }

                    if(nextLength < 34 ||
                       nextLength > data.length - position)
                    {
                        break;
                    }

                    byte[] nextBytes = new byte[nextLength];
                    System.arraycopy(data, position, nextBytes, 0, nextLength);

                    IsoRecord next = parseRecord(nextBytes);

                    if(next == null ||
                       !entry.name.equals(next.name) ||
                       next.directory ||
                       next.extents.size() != 1)
                    {
                        break;
                    }

                    IsoExtent nextExtent = next.extents.get(0);

                    if(!entry.addExtent(nextExtent.lba, nextExtent.size))
                    {
                        break;
                    }

                    position += nextLength;
                    extentCount++;

                    if(!next.multiExtent)
                    {
                        complete = true;
                        break;
                    }
                }

                if(!complete)
                {
                    result.errors++;
                    report.append("INCOMPLETE MULTI-EXTENT FILE: ")
                        .append(entryPath).append('\n');
                    continue;
                }

                entry.multiExtent = false;
                report.append("MULTI-EXTENT MERGED: ")
                    .append(entry.name)
                    .append(" PARTS=").append(extentCount)
                    .append(" TOTAL_SIZE=").append(entry.size)
                    .append('\n');
            }

            long absoluteOffset = checkedAbsoluteOffset(
                baseOffset, entry.lba);

            if(absoluteOffset < 0 ||
               !validateExtents(
                   baseOffset, regionEnd, entry.extents))
            {
                result.errors++;
                report.append("FILE/DIRECTORY OUT OF BOUNDS: ")
                    .append(entryPath).append('\n');
                continue;
            }

            if(entry.directory)
            {
                if(entry.extents.size() != 1 ||
                   !isWithinRegion(
                       absoluteOffset, entry.size, baseOffset, regionEnd))
                {
                    result.errors++;
                    report.append("DIRECTORY OUT OF BOUNDS: ")
                        .append(entryPath).append('\n');
                    continue;
                }

                report.append("[DIR] ").append(entryPath)
                    .append(" LBA=").append(entry.lba)
                    .append(" SIZE=").append(entry.size).append('\n');

                csv.append(csv(filesystem)).append(',')
                    .append(csv(entryPath)).append(',')
                    .append("DIRECTORY").append(',')
                    .append(entry.size).append(',')
                    .append(entry.lba).append(',')
                    .append(absoluteOffset)
                    .append(",,,").append('\n');

                walkDirectory(
                    channel, baseOffset, regionSize,
                    filesystem, entryPath, entry, depth,
                    result, report, csv);
                continue;
            }

            result.files++;
            if(result.totalBytes <= Long.MAX_VALUE - entry.size)
            {
                result.totalBytes += entry.size;
            }

            String hash = sha256Extents(
                channel, baseOffset, regionEnd, entry.extents);

            byte[] header = readPrefixExtents(
                channel, baseOffset, regionEnd, entry.extents, 64);

            String signature = detectSignature(header);
            String details = "";

            if("ELF".equals(signature))
            {
                details = inspectElf(
                    header, channel, baseOffset, regionEnd,
                    entry.extents, entry.size);
                result.executables++;
            }

            report.append("[FILE] ").append(entryPath)
                .append(" SIZE=").append(entry.size)
                .append(" LBA=").append(entry.lba)
                .append(" OFFSET=").append(absoluteOffset)
                .append(" SHA256=").append(hash)
                .append(" TYPE=").append(signature);

            if(!details.isEmpty())
            {
                report.append(" ").append(details);
            }
            report.append('\n');

            csv.append(csv(filesystem)).append(',')
                .append(csv(entryPath)).append(',')
                .append("FILE").append(',')
                .append(entry.size).append(',')
                .append(entry.lba).append(',')
                .append(absoluteOffset).append(',')
                .append(csv(hash)).append(',')
                .append(csv(signature)).append(',')
                .append(csv(details)).append('\n');

            if(depth < MAX_NESTED_DEPTH &&
               entry.extents.size() == 1 &&
               entry.name.toLowerCase(Locale.ROOT).endsWith(".iso") &&
               entry.size >= 17L * SECTOR_SIZE)
            {
                long nestedPvdOffset = safeAdd(
                    absoluteOffset, 16L * SECTOR_SIZE);

                if(nestedPvdOffset >= 0 &&
                   isWithinRegion(
                       nestedPvdOffset, SECTOR_SIZE,
                       baseOffset, regionEnd))
                {
                    byte[] nestedPvd = readAt(
                        channel, nestedPvdOffset, SECTOR_SIZE);

                    if(nestedPvd != null && isCd001(nestedPvd))
                    {
                        result.nestedImages++;
                        scanRegion(
                            channel, absoluteOffset, entry.size,
                            entryPath, depth + 1,
                            result, report, csv);
                    }
                }
            }
        }
    }

    private static boolean validateExtents(
        long baseOffset,
        long regionEnd,
        List<IsoExtent> extents)
    {
        if(extents == null || extents.isEmpty())
        {
            return false;
        }

        for(IsoExtent extent : extents)
        {
            long offset = checkedAbsoluteOffset(baseOffset, extent.lba);
            if(offset < 0 ||
               !isWithinRegion(
                   offset, extent.size, baseOffset, regionEnd))
            {
                return false;
            }
        }
        return true;
    }

    private static IsoRecord parseRecord(byte[] data)
    {
        if(data == null || data.length < 34)
        {
            return null;
        }

        int recordLength = data[0] & 0xFF;
        int nameLength = data[32] & 0xFF;

        if(recordLength < 34 ||
           recordLength > data.length ||
           nameLength <= 0 ||
           33 + nameLength > recordLength)
        {
            return null;
        }

        long lba = u32Both(data, 2, 6);
        long size = u32Both(data, 10, 14);

        if(lba < 0 || size < 0)
        {
            return null;
        }

        IsoRecord record = new IsoRecord();
        record.directory = (data[25] & 0x02) != 0;
        record.multiExtent = (data[25] & 0x80) != 0;

        if(!record.addExtent(lba, size))
        {
            return null;
        }

        if(nameLength == 1 && data[33] == 0)
        {
            record.name = ".";
        }
        else if(nameLength == 1 && data[33] == 1)
        {
            record.name = "..";
        }
        else
        {
            record.name = new String(
                data, 33, nameLength, StandardCharsets.US_ASCII);

            int version = record.name.indexOf(';');
            if(version >= 0)
            {
                record.name = record.name.substring(0, version);
            }

            while(record.name.endsWith("."))
            {
                record.name = record.name.substring(
                    0, record.name.length() - 1);
            }
        }

        return record;
    }

    /*
     * Static ELF inspection. All file reads are translated through the ISO
     * extent list, so this also works when an ELF file is stored as multiple
     * non-contiguous ISO extents.
     *
     * MIPS instruction analysis is intentionally conservative: it reports
     * direct J/JAL targets and a short entry-point disassembly. It does not
     * pretend to resolve indirect calls or produce a complete control-flow
     * graph.
     */
    private static String inspectElf(
        byte[] elfHeader,
        FileChannel channel,
        long baseOffset,
        long regionEnd,
        List<IsoExtent> extents,
        long fileSize)
        throws Exception
    {
        if(elfHeader == null || elfHeader.length < 52)
        {
            return "ELF_HEADER_INCOMPLETE";
        }

        if(elfHeader[0] != 0x7F ||
           elfHeader[1] != 'E' ||
           elfHeader[2] != 'L' ||
           elfHeader[3] != 'F')
        {
            return "INVALID_ELF_MAGIC";
        }

        int elfClass = elfHeader[4] & 0xFF;
        int dataEncoding = elfHeader[5] & 0xFF;

        if((elfClass != 1 && elfClass != 2) ||
           (dataEncoding != 1 && dataEncoding != 2))
        {
            return "UNSUPPORTED_ELF_FORMAT";
        }

        int headerSize = elfClass == 2 ? 64 : 52;
        if(elfHeader.length < headerSize || fileSize < headerSize)
        {
            return "ELF_HEADER_INCOMPLETE";
        }

        ByteOrder order = dataEncoding == 2
            ? ByteOrder.BIG_ENDIAN : ByteOrder.LITTLE_ENDIAN;
        ByteBuffer h = ByteBuffer.wrap(elfHeader).order(order);

        int type = h.getShort(16) & 0xFFFF;
        int machine = h.getShort(18) & 0xFFFF;
        long entry = elfClass == 2
            ? positiveLong(h.getLong(24)) : u32At(h, 24);
        long phoff = elfClass == 2
            ? positiveLong(h.getLong(32)) : u32At(h, 28);
        long shoff = elfClass == 2
            ? positiveLong(h.getLong(40)) : u32At(h, 32);
        int ehsize = h.getShort(elfClass == 2 ? 52 : 40) & 0xFFFF;
        int phentsize = h.getShort(elfClass == 2 ? 54 : 42) & 0xFFFF;
        int phnum = h.getShort(elfClass == 2 ? 56 : 44) & 0xFFFF;
        int shentsize = h.getShort(elfClass == 2 ? 58 : 46) & 0xFFFF;
        int shnum = h.getShort(elfClass == 2 ? 60 : 48) & 0xFFFF;
        int shstrndx = h.getShort(elfClass == 2 ? 62 : 50) & 0xFFFF;

        if(entry < 0 || phoff < 0 || shoff < 0 ||
           ehsize < headerSize)
        {
            return "ELF_INVALID_HEADER_FIELDS";
        }

        StringBuilder out = new StringBuilder();
        out.append(String.format(
            Locale.ROOT,
            "ELF_CLASS=%d ENDIAN=%s TYPE=%d MACHINE=%d ENTRY_VADDR=0x%X",
            elfClass, dataEncoding == 2 ? "BIG" : "LITTLE",
            type, machine, entry));

        final int expectedPhSize = elfClass == 2 ? 56 : 32;
        final int expectedShSize = elfClass == 2 ? 64 : 40;
        if(phnum > 4096 || shnum > 4096)
        {
            out.append(" ELF_TABLE_LIMIT_EXCEEDED");
            return out.toString();
        }

        List<ElfLoadSegment> loads = new ArrayList<>();
        int loadCount = 0;
        int executableLoadCount = 0;
        long phTableBytes = (long)phentsize * phnum;

        if(phnum > 0)
        {
            if(phentsize < expectedPhSize ||
               phTableBytes > 8L * 1024L * 1024L ||
               !validFileRange(phoff, phTableBytes, fileSize))
            {
                out.append(" PHDR_TABLE_INVALID");
            }
            else
            {
                byte[] table = readExtentsRange(
                    channel, baseOffset, regionEnd, extents,
                    phoff, (int)phTableBytes, fileSize);
                if(table == null)
                {
                    out.append(" PHDR_TABLE_READ_FAILED");
                }
                else
                {
                    ByteBuffer p = ByteBuffer.wrap(table).order(order);
                    for(int i = 0; i < phnum; i++)
                    {
                        int at = i * phentsize;
                        long pType = u32At(p, at);
                        long flags;
                        long offset;
                        long vaddr;
                        long filesz;
                        long memsz;
                        long align;

                        if(elfClass == 2)
                        {
                            flags = u32At(p, at + 4);
                            offset = positiveLong(p.getLong(at + 8));
                            vaddr = positiveLong(p.getLong(at + 16));
                            filesz = positiveLong(p.getLong(at + 32));
                            memsz = positiveLong(p.getLong(at + 40));
                            align = positiveLong(p.getLong(at + 48));
                        }
                        else
                        {
                            offset = u32At(p, at + 4);
                            vaddr = u32At(p, at + 8);
                            filesz = u32At(p, at + 16);
                            memsz = u32At(p, at + 20);
                            flags = u32At(p, at + 24);
                            align = u32At(p, at + 28);
                        }

                        if(offset < 0 || vaddr < 0 ||
                           filesz < 0 || memsz < 0 || align < 0 ||
                           !validFileRange(offset, filesz, fileSize) ||
                           memsz < filesz)
                        {
                            out.append(" PHDR[").append(i)
                                .append("]=INVALID");
                            continue;
                        }

                        if(pType == 1)
                        {
                            ElfLoadSegment segment = new ElfLoadSegment(
                                offset, vaddr, filesz, memsz, flags, align);
                            loads.add(segment);
                            loadCount++;
                            if((flags & 1L) != 0) executableLoadCount++;

                            out.append(String.format(
                                Locale.ROOT,
                                " PT_LOAD[%d]{OFF=0x%X,VA=0x%X,FILE=0x%X,MEM=0x%X,FLAGS=0x%X}",
                                i, offset, vaddr, filesz, memsz, flags));
                        }
                        else
                        {
                            out.append(String.format(
                                Locale.ROOT, " PHDR[%d]{TYPE=0x%X}",
                                i, pType));
                        }
                    }
                }
            }
        }

        long shTableBytes = (long)shentsize * shnum;
        if(shnum > 0)
        {
            if(shentsize < expectedShSize ||
               shTableBytes > 8L * 1024L * 1024L ||
               !validFileRange(shoff, shTableBytes, fileSize))
            {
                out.append(" SHDR_TABLE_INVALID");
            }
            else
            {
                byte[] table = readExtentsRange(
                    channel, baseOffset, regionEnd, extents,
                    shoff, (int)shTableBytes, fileSize);
                if(table == null)
                {
                    out.append(" SHDR_TABLE_READ_FAILED");
                }
                else
                {
                    ByteBuffer sec = ByteBuffer.wrap(table).order(order);
                    int shown = 0;
                    for(int i = 0; i < shnum; i++)
                    {
                        int at = i * shentsize;
                        long sectionType = u32At(sec, at + 4);
                        long flags;
                        long address;
                        long offset;
                        long size;
                        long link;
                        long info;
                        long alignment;
                        long entrySize;

                        if(elfClass == 2)
                        {
                            flags = positiveLong(sec.getLong(at + 8));
                            address = positiveLong(sec.getLong(at + 16));
                            offset = positiveLong(sec.getLong(at + 24));
                            size = positiveLong(sec.getLong(at + 32));
                            link = u32At(sec, at + 40);
                            info = u32At(sec, at + 44);
                            alignment = positiveLong(sec.getLong(at + 48));
                            entrySize = positiveLong(sec.getLong(at + 56));
                        }
                        else
                        {
                            flags = u32At(sec, at + 8);
                            address = u32At(sec, at + 12);
                            offset = u32At(sec, at + 16);
                            size = u32At(sec, at + 20);
                            link = u32At(sec, at + 24);
                            info = u32At(sec, at + 28);
                            alignment = u32At(sec, at + 32);
                            entrySize = u32At(sec, at + 36);
                        }

                        // SHT_NOBITS (8) occupies memory but not file bytes.
                        boolean noBits = sectionType == 8;
                        boolean rangeOk = noBits ||
                            (offset >= 0 && size >= 0 &&
                             validFileRange(offset, size, fileSize));

                        if(shown < 16)
                        {
                            out.append(String.format(
                                Locale.ROOT,
                                " SHDR[%d]{TYPE=%d,VA=0x%X,OFF=0x%X,SIZE=0x%X,FLAGS=0x%X%s}",
                                i, sectionType, address, offset, size,
                                flags, rangeOk ? "" : ",INVALID_RANGE"));
                            shown++;
                        }
                    }
                    if(shnum > shown)
                    {
                        out.append(" SHDR_OMITTED=").append(shnum - shown);
                    }
                    if(shstrndx != 0 && shstrndx >= shnum)
                    {
                        out.append(" SHSTRNDX_INVALID");
                    }
                }
            }
        }

        long entryFileOffset = mapVirtualAddress(
            entry, 4, loads);
        if(entryFileOffset >= 0)
        {
            out.append(String.format(
                Locale.ROOT, " ENTRY_FILE_OFFSET=0x%X",
                entryFileOffset));
            int codeLength = (int)Math.min(
                96L, fileSize - entryFileOffset);
            byte[] code = readExtentsRange(
                channel, baseOffset, regionEnd, extents,
                entryFileOffset, codeLength, fileSize);
            if(code != null && machine == 8 && elfClass == 1)
            {
                out.append(" ENTRY_DISASM{");
                ByteBuffer instructions =
                    ByteBuffer.wrap(code).order(order);
                int count = Math.min(24, code.length / 4);
                for(int i = 0; i < count; i++)
                {
                    long pc = entry + (long)i * 4L;
                    long word = u32At(instructions, i * 4);
                    out.append(String.format(
                        Locale.ROOT, "%s0x%08X:%08X",
                        i == 0 ? "" : ";", pc, word));
                    String decoded = decodeMips(word, pc);
                    if(!decoded.isEmpty())
                    {
                        out.append("(").append(decoded).append(")");
                    }
                }
                out.append("}");
            }
        }
        else
        {
            out.append(" ENTRY_NOT_IN_FILE_BACKED_PT_LOAD");
        }

        if(machine == 8 && elfClass == 1 && executableLoadCount > 0)
        {
            Set<Long> callTargets = new TreeSet<>();
            int scannedInstructions = 0;
            int directCalls = 0;
            long droppedCallTargets = 0;
            long droppedCallEdges = 0;
            long scanBudget = 32L * 1024L * 1024L;
            Set<String> callEdges = new TreeSet<>();
            for(ElfLoadSegment segment : loads)
            {
                if((segment.flags & 1L) == 0 || segment.filesz <= 0)
                {
                    continue;
                }
                // Bound total work on hostile or unusually large ELF files.
                if(scanBudget <= 0) break;
                long scanLength = Math.min(
                    segment.filesz, Math.min(
                        scanBudget, 16L * 1024L * 1024L));
                byte[] text = readExtentsRange(
                    channel, baseOffset, regionEnd, extents,
                    segment.offset, (int)scanLength, fileSize);
                if(text == null) continue;
                scanBudget -= scanLength;

                ByteBuffer instructions =
                    ByteBuffer.wrap(text).order(order);
                int words = text.length / 4;
                for(int i = 0; i < words; i++)
                {
                    long word = u32At(instructions, i * 4);
                    long pc = segment.vaddr + (long)i * 4L;
                    int opcode = (int)((word >>> 26) & 0x3F);
                    if(opcode == 3)
                    {
                        long target = ((pc + 4L) & 0xF0000000L) |
                            ((word & 0x03FFFFFFL) << 2);
                        callTargets.add(target);
                        callEdges.add(String.format(
                            Locale.ROOT, "0x%X->0x%X", pc, target));
                        directCalls++;
                    }
                }
                scannedInstructions += words;
            }

        if(machine == 8 && elfClass == 1 && dataEncoding == 1)
        {
            final long MAX_DEEP_ELF_BYTES = 16L * 1024L * 1024L;
            
            if(fileSize <= MAX_DEEP_ELF_BYTES &&
               fileSize <= Integer.MAX_VALUE)
            {
                byte[] fullElf = readExtentsRange(
                    channel,
                    baseOffset,
                    regionEnd,
                    extents,
                    0,
                    (int)fileSize,
                    fileSize);

                if(fullElf != null)
                {
                    out.append(" ")
                        .append(Ps2MipsDeepAnalyzer.analyze(fullElf));
                }
                else
                {
                    out.append(" MIPS_DEEP=FULL_ELF_READ_FAILED");
                }
            }
            else
            {
                out.append(" MIPS_DEEP=SKIPPED_ELF_SIZE_LIMIT");
            }
        }
            
            out.append(" MIPS_STATIC{LOAD_SEGMENTS=")
                .append(loadCount)
                .append(",EXEC_SEGMENTS=").append(executableLoadCount)
                .append(",SCANNED_WORDS=").append(scannedInstructions)
                .append(",JAL_COUNT=").append(directCalls)
                .append(",UNIQUE_JAL_TARGETS=").append(callTargets.size())
                .append(",CALL_EDGES=");

            int edgeShown = 0;
            for(String edge : callEdges)
            {
                if(edgeShown >= 32) break;
                out.append(edgeShown == 0 ? "" : ";")
                    .append(edge);
                edgeShown++;
            }
            if(callEdges.size() > edgeShown) out.append(";...");
            out.append(",TARGETS=");

            int shown = 0;
            for(Long target : callTargets)
            {
                if(shown >= 32) break;
                out.append(String.format(
                    Locale.ROOT, "%s0x%X",
                    shown == 0 ? ",TARGETS=" : ";", target));
                shown++;
            }
            if(callTargets.size() > shown)
            {
                out.append(";...");
            }
            out.append("}");
        }

        return out.toString();
    }

    private static final class ElfLoadSegment
    {
        final long offset;
        final long vaddr;
        final long filesz;
        final long memsz;
        final long flags;
        final long align;

        ElfLoadSegment(
            long offset, long vaddr, long filesz,
            long memsz, long flags, long align)
        {
            this.offset = offset;
            this.vaddr = vaddr;
            this.filesz = filesz;
            this.memsz = memsz;
            this.flags = flags;
            this.align = align;
        }
    }

    private static long positiveLong(long value)
    {
        return value < 0 ? -1 : value;
    }

    private static long u32At(ByteBuffer buffer, int offset)
    {
        return buffer.getInt(offset) & 0xFFFFFFFFL;
    }

    private static boolean validFileRange(
        long offset, long length, long fileSize)
    {
        return offset >= 0 && length >= 0 &&
            offset <= fileSize && length <= fileSize - offset;
    }

    private static long mapVirtualAddress(
        long address, long length, List<ElfLoadSegment> segments)
    {
        for(ElfLoadSegment segment : segments)
        {
            if(address >= segment.vaddr)
            {
                long delta = address - segment.vaddr;
                if(delta <= segment.filesz &&
                   length <= segment.filesz - delta &&
                   segment.offset <= Long.MAX_VALUE - delta)
                {
                    return segment.offset + delta;
                }
            }
        }
        return -1;
    }

    private static byte[] readExtentsRange(
        FileChannel channel,
        long baseOffset,
        long regionEnd,
        List<IsoExtent> extents,
        long fileOffset,
        int length,
        long fileSize)
        throws Exception
    {
        if(length < 0 || !validFileRange(fileOffset, length, fileSize))
        {
            return null;
        }

        byte[] output = new byte[length];
        if(length == 0) return output;

        long logicalPosition = 0;
        long requestedEnd = fileOffset + length;
        int copied = 0;

        for(IsoExtent extent : extents)
        {
            long extentEnd = logicalPosition + extent.size;
            if(extentEnd < logicalPosition) return null;

            if(fileOffset < extentEnd && requestedEnd > logicalPosition)
            {
                long from = Math.max(fileOffset, logicalPosition);
                long to = Math.min(requestedEnd, extentEnd);
                int amount = (int)(to - from);
                long withinExtent = from - logicalPosition;
                long absolute = checkedAbsoluteOffset(
                    baseOffset, extent.lba);
                if(absolute < 0 ||
                   !isWithinRegion(
                       absolute, extent.size, baseOffset, regionEnd) ||
                   absolute > Long.MAX_VALUE - withinExtent)
                {
                    return null;
                }

                long sourceOffset = absolute + withinExtent;
                ByteBuffer buffer = ByteBuffer.wrap(output, copied, amount);
                long position = sourceOffset;
                while(buffer.hasRemaining())
                {
                    int count = channel.read(buffer, position);
                    if(count <= 0) return null;
                    position += count;
                }
                copied += amount;
            }

            logicalPosition = extentEnd;
            if(copied == length) break;
        }

        return copied == length ? output : null;
    }

    private static String decodeMips(long word, long pc)
    {
        int opcode = (int)((word >>> 26) & 0x3F);
        if(opcode == 2 || opcode == 3)
        {
            long target = ((pc + 4L) & 0xF0000000L) |
                ((word & 0x03FFFFFFL) << 2);
            return (opcode == 3 ? "jal " : "j ") +
                String.format(Locale.ROOT, "0x%08X", target);
        }
        if(opcode == 4 || opcode == 5 ||
           opcode == 6 || opcode == 7)
        {
            int immediate = (short)(word & 0xFFFF);
            long target = pc + 4L + ((long)immediate << 2);
            return "branch " +
                String.format(Locale.ROOT, "0x%08X", target);
        }
        if(opcode == 0)
        {
            int funct = (int)(word & 0x3F);
            if(funct == 8) return "jr";
            if(funct == 9) return "jalr";
            if(funct == 12) return "syscall";
            if(funct == 13) return "break";
        }
        return "";
    }

    private static String detectSignature(byte[] data)
    {
        if(data == null || data.length < 4)
        {
            return "EMPTY_OR_SHORT";
        }

        if(data[0] == 0x7F &&
           data[1] == 'E' && data[2] == 'L' && data[3] == 'F')
        {
            return "ELF";
        }

        if(data[0] == 'P' && data[1] == 'K' &&
           data[2] == 0x03 && data[3] == 0x04)
        {
            return "ZIP";
        }

        if(data[0] == 'R' && data[1] == 'I' &&
           data[2] == 'F' && data[3] == 'F')
        {
            return "RIFF";
        }

        if(data[0] == 'O' && data[1] == 'g' &&
           data[2] == 'g' && data[3] == 'S')
        {
            return "OGG";
        }

        if(data[0] == 'P' && data[1] == 'S' &&
           data[2] == 'F' && data[3] == ' ')
        {
            return "PSF";
        }

        return "UNKNOWN";
    }

    private static String sha256Extents(
        FileChannel channel,
        long baseOffset,
        long regionEnd,
        List<IsoExtent> extents)
        throws Exception
    {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        ByteBuffer buffer = ByteBuffer.allocate(64 * 1024);

        for(IsoExtent extent : extents)
        {
            long offset = checkedAbsoluteOffset(baseOffset, extent.lba);

            if(offset < 0 ||
               !isWithinRegion(
                   offset, extent.size, baseOffset, regionEnd))
            {
                throw new Exception(
                    "Invalid multi-extent region boundary.");
            }

            long remaining = extent.size;
            long position = offset;

            while(remaining > 0)
            {
                buffer.clear();
                int wanted = (int)Math.min(buffer.capacity(), remaining);
                buffer.limit(wanted);

                int count = channel.read(buffer, position);
                if(count <= 0)
                {
                    throw new Exception("Multi-extent read failed.");
                }

                digest.update(buffer.array(), 0, count);
                position += count;
                remaining -= count;
            }
        }

        return toHex(digest.digest());
    }

    private static byte[] readPrefixExtents(
        FileChannel channel,
        long baseOffset,
        long regionEnd,
        List<IsoExtent> extents,
        int maxBytes)
        throws Exception
    {
        if(maxBytes < 0)
        {
            throw new IllegalArgumentException("Invalid prefix length.");
        }

        long totalSize = 0;
        for(IsoExtent extent : extents)
        {
            if(extent.size < 0 ||
               totalSize > Long.MAX_VALUE - extent.size)
            {
                throw new Exception("Invalid extent size.");
            }
            totalSize += extent.size;
        }

        int wanted = (int)Math.min(totalSize, (long)maxBytes);
        byte[] output = new byte[wanted];
        int copied = 0;

        for(IsoExtent extent : extents)
        {
            if(copied >= wanted)
            {
                break;
            }

            long offset = checkedAbsoluteOffset(baseOffset, extent.lba);

            if(offset < 0 ||
               !isWithinRegion(
                   offset, extent.size, baseOffset, regionEnd))
            {
                throw new Exception("Invalid prefix extent boundary.");
            }

            int amount = (int)Math.min(
                (long)(wanted - copied), extent.size);

            ByteBuffer buffer = ByteBuffer.wrap(
                output, copied, amount);

            long position = offset;
            while(buffer.hasRemaining())
            {
                int count = channel.read(buffer, position);
                if(count <= 0)
                {
                    throw new Exception(
                        "Multi-extent prefix read failed.");
                }
                position += count;
            }

            copied += amount;
        }

        return output;
    }

    private static byte[] readAt(
        FileChannel channel,
        long offset,
        int length)
        throws Exception
    {
        long channelSize = channel.size();

        if(offset < 0 || length < 0 ||
           offset > channelSize ||
           length > channelSize - offset)
        {
            return null;
        }

        byte[] data = new byte[length];
        ByteBuffer buffer = ByteBuffer.wrap(data);
        long position = offset;

        while(buffer.hasRemaining())
        {
            int count = channel.read(buffer, position);
            if(count <= 0)
            {
                return null;
            }
            position += count;
        }

        return data;
    }

    private static long checkedRegionEnd(
        long baseOffset,
        long regionSize,
        long channelSize)
    {
        if(baseOffset < 0 || regionSize < 0 ||
           baseOffset > channelSize ||
           regionSize > channelSize - baseOffset)
        {
            return -1;
        }
        return baseOffset + regionSize;
    }

    private static long checkedAbsoluteOffset(
        long baseOffset,
        long lba)
    {
        if(baseOffset < 0 || lba < 0 ||
           lba > (Long.MAX_VALUE - baseOffset) / SECTOR_SIZE)
        {
            return -1;
        }
        return baseOffset + lba * SECTOR_SIZE;
    }

    private static long safeAdd(long left, long right)
    {
        if(left < 0 || right < 0 || left > Long.MAX_VALUE - right)
        {
            return -1;
        }
        return left + right;
    }

    private static boolean isWithinRegion(
        long offset,
        long size,
        long regionStart,
        long regionEnd)
    {
        return offset >= regionStart &&
            offset <= regionEnd &&
            size >= 0 &&
            size <= regionEnd - offset;
    }

    private static boolean isCd001(byte[] data)
    {
        return data != null && data.length >= 7 &&
            data[1] == 'C' && data[2] == 'D' &&
            data[3] == '0' && data[4] == '0' &&
            data[5] == '1' && data[6] == 1;
    }

    private static long u32le(byte[] data, int offset)
    {
        return (data[offset] & 0xFFL) |
            ((data[offset + 1] & 0xFFL) << 8) |
            ((data[offset + 2] & 0xFFL) << 16) |
            ((data[offset + 3] & 0xFFL) << 24);
    }

    private static long u32be(byte[] data, int offset)
    {
        return ((data[offset] & 0xFFL) << 24) |
            ((data[offset + 1] & 0xFFL) << 16) |
            ((data[offset + 2] & 0xFFL) << 8) |
            (data[offset + 3] & 0xFFL);
    }

    private static long u32Both(
        byte[] data,
        int littleOffset,
        int bigOffset)
    {
        if(data == null || littleOffset < 0 || bigOffset < 0 ||
           littleOffset > data.length - 4 ||
           bigOffset > data.length - 4)
        {
            return -1;
        }

        long little = u32le(data, littleOffset);
        long big = u32be(data, bigOffset);
        return little == big ? little : -1;
    }

    private static String ascii(
        byte[] data,
        int offset,
        int length)
    {
        return new String(
            data, offset, length, StandardCharsets.US_ASCII);
    }

    private static byte[] slice(
        byte[] data,
        int offset,
        int length)
    {
        if(data == null || offset < 0 || length < 0 ||
           offset > data.length ||
           length > data.length - offset)
        {
            return null;
        }

        byte[] result = new byte[length];
        System.arraycopy(data, offset, result, 0, length);
        return result;
    }

    private static String toHex(byte[] bytes)
    {
        StringBuilder output = new StringBuilder(bytes.length * 2);
        for(byte value : bytes)
        {
            output.append(String.format(
                Locale.ROOT, "%02x", value & 0xFF));
        }
        return output.toString();
    }

    private static String csv(String value)
    {
        if(value == null)
        {
            return "\"\"";
        }

        return "\"" + value.replace("\"", "\"\"") + "\"";
    }
}
