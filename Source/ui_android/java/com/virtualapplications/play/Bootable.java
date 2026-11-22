package com.virtualapplications.play;

import java.nio.charset.StandardCharsets;
import java.util.UUID;

/**
 * Java representation of the native Bootable database record.
 *
 * Field names and JNI descriptors must remain synchronized
 * with Bootable_ClassInfo.
 */
public class Bootable
{
    String path;
    String discId;

    public String title;
    String coverUrl;

    long lastBootedTime;

    public String overview;
    public String compatibility;

    int bootableType;
    boolean favorite;

    public String getCoverKey()
    {
        return getCoverKey(path, discId);
    }

    public static String getCoverKey(
            String path,
            String discId)
    {
        if(discId != null && !discId.trim().isEmpty())
        {
            return discId.trim().replaceAll(
                    "[^A-Za-z0-9._-]",
                    "_");
        }

        String source =
                path == null ? "" : path;

        return "path-" + UUID.nameUUIDFromBytes(
                source.getBytes(StandardCharsets.UTF_8));
    }
}
