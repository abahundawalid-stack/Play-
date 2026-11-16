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
import java.util.List;
import java.util.Locale;
import java.util.HashSet;
import java.util.Set;

public final class Ps2IsoForensicScanner
{
    private static final int SECTOR_SIZE = 2048;
    private static final int MAX_NESTED_DEPTH = 4;
    private static final long MAX_DIRECTORY_SIZE = 64L * 1024L * 1024L;

    private Ps2IsoForensicScanner()
    {
    }

    private static long checkedRegionEnd(
        long baseOffset,
        long regionSize,
        long channelSize)
    {
        if(baseOffset < 0 ||
           regionSize < 0 ||
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
        long lba;         
        long size;

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

        void addExtent(long extentLba, long extentSize)
        {
            extents.add(new IsoExtent(extentLba, extentSize));

            if(extents.size() == 1)
            {
                lba = extentLba;
            }

            size += extentSize;
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
                report.append("Image size: ")
                    .append(imageSize)
                    .append(" bytes\n");

                report.append("Sector size: ")
                    .append(SECTOR_SIZE)
                    .append(" bytes\n");

                report.append("Analysis mode: READ ONLY\n");
                report.append("Filesystem: ISO9660 detection\n\n");

                csv.append(
                    "filesystem,path,type,size_bytes,"
                    + "local_lba,absolute_byte_offset,"
                    + "sha256,signature,details\n");

                scanRegion(
                    channel,
                    0,
                    imageSize,
                    "ROOT",
                    0,
                    result,
                    report,
                    csv);

                report.append("\n====================================\n");
                report.append("ANALYSIS SUMMARY\n");
                report.append("====================================\n");

                report.append("Files: ")
                    .append(result.files).append('\n');

                report.append("Directories: ")
                    .append(result.directories).append('\n');

                report.append("Nested ISO images: ")
                    .append(result.nestedImages).append('\n');

                report.append("ELF executables: ")
                    .append(result.executables).append('\n');

                report.append("Errors: ")
                    .append(result.errors).append('\n');

                report.append("\nNOTES\n");
                report.append(
                    "- LBA is relative to its own ISO filesystem.\n");
                report.append(
                    "- Absolute byte offset includes nested image offset.\n");
                report.append(
                    "- ELF entry point is a virtual address, "
                    + "not a file offset.\n");
                report.append(
                    "- Unknown proprietary formats are not decoded.\n");
                report.append(
                    "- This report is static analysis; "
                    + "it does not prove runtime compatibility.\n");

                result.report = report.toString();
                result.csv = csv.toString();
            }
        }

        return result;
    }

    long regionEnd = checkedRegionEnd(
        baseOffset,
        regionSize,
        channel.size());

    if(regionEnd < 0)
    {
        result.errors++;
        report.append("REGION END OVERFLOW OR INVALID\n");
        return;
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
            report.append("NESTED DEPTH LIMIT: ")
                .append(label).append('\n');
            return;
        }

        if(baseOffset < 0 ||
           regionSize < 17L * SECTOR_SIZE ||
           baseOffset > channel.size() ||
           regionSize > channel.size() - baseOffset)
        {
            result.errors++;
            report.append("INVALID REGION: ")
                .append(label).append('\n');
            return;
        }

        byte[] pvd = null;
        String volumeId = "UNKNOWN";
        long volumeSectors = 0;

        long availableSectors = regionSize / SECTOR_SIZE;
        int descriptorLimit = (int)Math.min(64L, availableSectors);

        for(int sector = 16; sector < descriptorLimit; sector++)
        {
            byte[] descriptor = readAt(
                channel,
                baseOffset + (long)sector * SECTOR_SIZE,
                SECTOR_SIZE);

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
                report.append("INVALID VOLUME SPACE SIZE\n");
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
            report.append("NOT ISO9660: ")
                .append(label).append('\n');
            return;
        }

        report.append("\n------------------------------------\n");
        report.append("FILESYSTEM: ").append(label).append('\n');
        report.append("Volume ID: ").append(volumeId).append('\n');
        report.append("Volume sectors: ")
            .append(volumeSectors).append('\n');
        report.append("Image base offset: ")
            .append(baseOffset).append('\n');
        report.append("Region size: ")
            .append(regionSize).append('\n');

        long declaredBytes = volumeSectors * SECTOR_SIZE;

        if(declaredBytes > regionSize)
        {
            report.append(
                "WARNING: Declared volume exceeds available region.\n");
        }

        byte[] rootRecord = slice(pvd, 156, 34);

        if(rootRecord == null || rootRecord[0] < 34)
        {
            result.errors++;
            report.append("INVALID ROOT DIRECTORY RECORD\n");
            return;
        }

        IsoRecord root = parseRecord(rootRecord);

        if(root == null || !root.directory)
        {
            result.errors++;
            report.append("ROOT DIRECTORY INVALID\n");
            return;
        }

        walkDirectory(
            channel,
            baseOffset,
            regionSize,
            label,
            "/",
            root,
            depth,
            result,
            report,
            csv);
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
        String directoryKey =
            filesystem + ":" +
            directory.lba + ":" +   
            directory.size;

        if(!result.visitedDirectories.add(directoryKey))
        {
            report.append("DIRECTORY CYCLE/REPEAT SKIPPED: ")
                .append(path)
                .append('\n');

            return;
        }
        
        if(directory.size <= 0 ||
           directory.size > MAX_DIRECTORY_SIZE)
        {
            result.errors++;
            report.append("DIRECTORY SIZE INVALID: ")
                .append(path).append('\n');
            return;
        }

        long directoryOffset = checkedAbsoluteOffset(
            baseOffset,
            directory.lba);

        if(directoryOffset < 0 ||
           !isWithinRegion(
               directoryOffset,
               directory.size,
               baseOffset,
               regionEnd))
        {
            result.errors++;
            report.append("DIRECTORY OUT OF BOUNDS: ")
                .append(path)
                .append('\n');
            return;
        }

        long absoluteOffset = checkedAbsoluteOffset(
            baseOffset,
            entry.lba);

        if(absoluteOffset < 0 ||
           !isWithinRegion(
               absoluteOffset,
               entry.size,
               baseOffset,
               regionEnd))
        {
            result.errors++;
            report.append("FILE OUT OF BOUNDS: ")
                .append(entryPath)
                .append('\n');
            continue;
        }
        
        byte[] data = readAt(
            channel,
            directoryOffset,
            (int)directory.size);

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
                position += SECTOR_SIZE - sectorPosition;
                continue;
            }

            if(recordLength < 34 ||
               position + recordLength > data.length)
            {
                result.errors++;
                report.append("INVALID DIRECTORY RECORD: ")
                    .append(path).append('\n');
                break;
            }

            byte[] recordBytes = new byte[recordLength];

            System.arraycopy(
                data,
                position,
                recordBytes,
                0,
                recordLength);

            IsoRecord entry = parseRecord(recordBytes);

            position += recordLength;

            if(entry == null ||
               entry.name.equals(".") ||
               entry.name.equals(".."))
            {
                continue;
            }

            if(entry.directory && entry.multiExtent)
            {
                result.errors++;

                report.append("MULTI-EXTENT DIRECTORY NOT SUPPORTED: ")
                    .append(path)
                    .append("/")
                    .append(entry.name)
                    .append('\n');

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
                        position += SECTOR_SIZE - nextSectorPosition;
                        continue;
                    }

                    if(nextLength < 34 ||
                       position + nextLength > data.length)
                    {
                       break;
                    }

                    byte[] nextBytes = new byte[nextLength];
   
                    System.arraycopy(
                        data,
                        position,
                        nextBytes,
                        0,
                        nextLength);

                    IsoRecord next = parseRecord(nextBytes);

                    if(next == null ||
                       !entry.name.equals(next.name) ||
                       next.directory ||
                       next.extents.size() != 1)
                    {
                        break;
                    }

                    position += nextLength;

                    IsoExtent nextExtent = next.extents.get(0);

                    if(entry.size > Long.MAX_VALUE - nextExtent.size)
                    {
                        break;
                    }

                    entry.addExtent(
                        nextExtent.lba,
                        nextExtent.size);

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
                        .append(path)
                        .append("/")
                        .append(entry.name)
                        .append('\n');

                    continue;
                }

                report.append("MULTI-EXTENT MERGED: ")
                    .append(entry.name)
                    .append(" PARTS=")
                    .append(extentCount)
                    .append(" TOTAL_SIZE=")
                    .append(entry.size)
                    .append('\n');
            }
            
            if(entry.directory)
            {
                report.append("[DIR] ")
                    .append(entryPath)
                    .append(" LBA=")
                    .append(entry.lba)
                    .append(" SIZE=")
                    .append(entry.size)
                    .append('\n');

                csv.append(csv(filesystem)).append(',')
                    .append(csv(entryPath)).append(',')
                    .append("DIRECTORY").append(',')
                    .append(entry.size).append(',')
                    .append(entry.lba).append(',')
                    .append(absoluteOffset)
                    .append(",,,")
                    .append('\n');

                walkDirectory(
                    channel,
                    baseOffset,
                    regionSize,
                    filesystem,
                    entryPath,
                    entry,
                    depth,
                    result,
                    report,
                    csv);

                continue;
            }

            result.files++;

            String hash = sha256Extents(
                channel,
                baseOffset,
                entry.extents);

            byte[] header = readPrefixExtents(
                channel,
                baseOffset,
                entry.extents,
                64);

            String signature = detectSignature(header);
            String details = "";

            if("ELF".equals(signature))
            {
                details = inspectElf(header, channel, absoluteOffset);
                result.executables++;
            }

            report.append("[FILE] ")
                .append(entryPath)
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
               entry.name.toLowerCase(Locale.ROOT).endsWith(".iso") &&
               entry.size >= 17L * SECTOR_SIZE)
            {
                byte[] nestedPvd = readAt(
                    channel,
                    absoluteOffset + 16L * SECTOR_SIZE,
                    SECTOR_SIZE);

                if(nestedPvd != null && isCd001(nestedPvd))
                {
                    result.nestedImages++;

                    scanRegion(
                        channel,
                        absoluteOffset,
                        entry.size,
                        entryPath,
                        depth + 1,
                        result,
                        report,
                        csv);
                }
            }
        }
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

        record.addExtent(lba, size);

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
                data,
                33,
                nameLength,
                StandardCharsets.US_ASCII);

            int version = record.name.indexOf(';');

            if(version >= 0)
            {
                record.name = record.name.substring(0, version);
            }

            while(record.name.endsWith("."))
            {
                        record.name = record.name.substring(
                            0,
                            record.name.length() - 1);
            }
        }

        return record;
    }

    private static long u32be(byte[] data, int offset)
    {  
        return
            ((data[offset] & 0xFFL) << 24) |
            ((data[offset + 1] & 0xFFL) << 16) |
            ((data[offset + 2] & 0xFFL) << 8) |
            (data[offset + 3] & 0xFFL);
    }

    private static long u32Both(
        byte[] data,
        int littleOffset,
        int bigOffset)
    {
        if(data == null ||
           littleOffset < 0 ||
           bigOffset < 0 ||
           littleOffset + 4 > data.length ||
           bigOffset + 4 > data.length)
        {
            return -1;
        }

        long little = u32le(data, littleOffset);
        long big = u32be(data, bigOffset);
        
        return little == big ? little : -1;
    }
    
    private static String inspectElf(
        byte[] header,
        FileChannel channel,
        long offset)
        throws Exception
    {
        byte[] elfHeader = readAt(channel, offset, 64);

        if(elfHeader == null || elfHeader.length < 52)
        {
            return "ELF_HEADER_INCOMPLETE";
        }

        int elfClass = elfHeader[4] & 0xFF;
        int dataEncoding = elfHeader[5] & 0xFF;

        ByteOrder order =
            dataEncoding == 2
                ? ByteOrder.BIG_ENDIAN
                : ByteOrder.LITTLE_ENDIAN;

        ByteBuffer buffer =
            ByteBuffer.wrap(elfHeader).order(order);

        int machine = buffer.getShort(18) & 0xFFFF;

        long entry;

        if(elfClass == 2)
        {
            if(elfHeader.length < 64)
            {
                return "ELF64_HEADER_INCOMPLETE";
            }

            entry = buffer.getLong(24);
        }
        else
        {
            entry = buffer.getInt(24) & 0xFFFFFFFFL;
        }

        return String.format(
            Locale.ROOT,
            "ELF_CLASS=%d ENDIAN=%s MACHINE=%d ENTRY_VADDR=0x%X",
            elfClass,
            dataEncoding == 2 ? "BIG" : "LITTLE",
            machine,
            entry);
    }

    private static String detectSignature(byte[] data)
    {
        if(data == null || data.length < 4)
        {
            return "EMPTY_OR_SHORT";
        }

        if(data[0] == 0x7F &&
           data[1] == 'E' &&
           data[2] == 'L' &&
           data[3] == 'F')
        {
            return "ELF";
        }

        if(data[0] == 'P' &&
           data[1] == 'K' &&
           data[2] == 0x03 &&
           data[3] == 0x04)
        {
            return "ZIP";
        }

        if(data[0] == 'R' &&
           data[1] == 'I' &&
           data[2] == 'F' &&
           data[3] == 'F')
        {
            return "RIFF";
        }

        if(data[0] == 'O' &&
           data[1] == 'g' &&
           data[2] == 'g' &&
           data[3] == 'S')
        {
            return "OGG";
        }

        if(data[0] == 'P' &&
           data[1] == 'S' &&
           data[2] == 'F' &&
           data[3] == ' ')
        {
            return "PSF";
        }

        return "UNKNOWN";
    }

    private static String sha256(
        FileChannel channel,
        long offset,
        long size)
        throws Exception
    {
        MessageDigest digest =
            MessageDigest.getInstance("SHA-256");

        ByteBuffer buffer = ByteBuffer.allocate(64 * 1024);

        long remaining = size;
        long position = offset;

        while(remaining > 0)
        {
            buffer.clear();

            int wanted = (int)Math.min(
                buffer.capacity(),
                remaining);

            buffer.limit(wanted);

            int count = channel.read(buffer, position);

            if(count <= 0)
            {
                throw new Exception(
                    "Gagal membaca data untuk SHA-256.");
            }

            digest.update(buffer.array(), 0, count);

            position += count;
            remaining -= count;
        }

        private static String sha256Extents(
            FileChannel channel,
            long baseOffset,
            List<IsoExtent> extents)
            throws Exception
        {
            MessageDigest digest =
                MessageDigest.getInstance("SHA-256");

            ByteBuffer buffer =
                ByteBuffer.allocate(64 * 1024);

            for(IsoExtent extent : extents)
            {
                long offset = checkedAbsoluteOffset(
                    baseOffset,
                    extent.lba);

                if(offset < 0 ||
                   extent.size < 0 ||
                   extent.size > channel.size() - offset)
                {
                    throw new Exception(
                        "Invalid multi-extent boundary.");
                }

                long remaining = extent.size;
                long position = offset;

                while(remaining > 0)
                {
                    buffer.clear();

                    int wanted = (int)Math.min(
                        buffer.capacity(),
                        remaining);

                    buffer.limit(wanted);

                    int count = channel.read(buffer, position);

                    if(count <= 0)
                    {
                        throw new Exception(
                            "Multi-extent read failed.");
                    }

                    digest.update(buffer.array(), 0, count);

                    position += count;
                    remaining -= count;
                }
            }

            StringBuilder output = new StringBuilder();

            for(byte value : digest.digest())
            {
                output.append(String.format(
                    Locale.ROOT,
                    "%02x",
                    value & 0xFF));
            }

            return output.toString();
        }

        StringBuilder result = new StringBuilder();

        for(byte value : digest.digest())
        {
            result.append(
                String.format(
                    Locale.ROOT,
                    "%02x",
                    value & 0xFF));
        }

        return result.toString();
    }

    private static byte[] readAt(
        FileChannel channel,
        long offset,
        int length)
        throws Exception
    {
        long channelSize = channel.size();

        if(offset < 0 ||
           length < 0 ||
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

    private static boolean isCd001(byte[] data)
    {
        return data != null &&
            data.length >= 7 &&
            data[1] == 'C' &&
            data[2] == 'D' &&
            data[3] == '0' &&
            data[4] == '0' &&
            data[5] == '1' &&
            data[6] == 1;
    }

    private static long u32le(byte[] data, int offset)
    {
        return
            (data[offset] & 0xFFL) |
            ((data[offset + 1] & 0xFFL) << 8) |
            ((data[offset + 2] & 0xFFL) << 16) |
            ((data[offset + 3] & 0xFFL) << 24);
    }

    private static String ascii(
        byte[] data,
        int offset,
        int length)
    {
        return new String(
            data,
            offset,
            length,
            StandardCharsets.US_ASCII);
    }

    private static byte[] slice(
        byte[] data,
        int offset,
        int length)
    {
        if(data == null ||
           offset < 0 ||
           length < 0 ||
           offset + length > data.length)
        {
            return null;
        }

        byte[] result = new byte[length];

        System.arraycopy(
            data,
            offset,
            result,
            0,
            length);

        return result;
    }

    private static String csv(String value)
    {
        if(value == null)
        {
            return "\"\"";
        }

        return "\"" +
            value.replace("\"", "\"\"") +
            "\"";
    }
}
