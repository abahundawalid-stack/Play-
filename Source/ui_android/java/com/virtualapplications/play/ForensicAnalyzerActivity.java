package com.virtualapplications.play;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

public class ForensicAnalyzerActivity extends Activity
{
    private static final int REQUEST_OPEN_ISO = 8101;
    private static final int REQUEST_SAVE_TXT = 8102;
    private static final int REQUEST_SAVE_ZIP = 8103;

    private final Handler handler =
        new Handler(Looper.getMainLooper());

    private TextView reportView;
    private TextView statusView;

    private String report = "";
    private String csv = "";

    @Override
    protected void onCreate(Bundle state)
    {
        super.onCreate(state);

        buildScreen();
    }

    private void buildScreen()
    {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(20, 20, 20, 20);

        TextView title = new TextView(this);
        title.setText("ORBIT2SX - PS2 ISO FORENSIC ANALYZER");
        title.setTextSize(19);
        root.addView(title);

        TextView description = new TextView(this);
        description.setText(
            "Analisa struktur ISO9660, file, folder, "
            + "LBA, byte offset, SHA-256, ELF, dan nested ISO.");
        description.setTextSize(13);
        description.setPadding(0, 12, 0, 12);
        root.addView(description);

        Button open = new Button(this);
        open.setText("PILIH FILE ISO");
        open.setOnClickListener(v -> selectIso());
        root.addView(open);

        Button saveTxt = new Button(this);
        saveTxt.setText("EKSPOR LAPORAN TXT");
        saveTxt.setOnClickListener(
            v -> saveDocument(REQUEST_SAVE_TXT));
        root.addView(saveTxt);

        Button saveZip = new Button(this);
        saveZip.setText("EKSPOR PAKET ANALISA ZIP");
        saveZip.setOnClickListener(
            v -> saveDocument(REQUEST_SAVE_ZIP));
        root.addView(saveZip);

        statusView = new TextView(this);
        statusView.setText("Belum ada ISO yang dianalisa.");
        statusView.setPadding(0, 12, 0, 12);
        root.addView(statusView);

        reportView = new TextView(this);
        reportView.setTextSize(12);
        reportView.setTextIsSelectable(true);
        reportView.setPadding(0, 12, 0, 20);

        ScrollView scroll = new ScrollView(this);
        scroll.addView(reportView);

        root.addView(
            scroll,
            new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                0,
                1));

        setContentView(root);
    }

    private void selectIso()
    {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);

        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");

        intent.addFlags(
            Intent.FLAG_GRANT_READ_URI_PERMISSION);

        startActivityForResult(
            intent,
            REQUEST_OPEN_ISO);
    }

    private void analyze(Uri uri)
    {
        statusView.setText("Sedang menganalisa ISO...");
        reportView.setText("");

        new Thread(() -> {
            try
            {
                Ps2IsoForensicScanner.Result result =
                    Ps2IsoForensicScanner.analyze(
                        ForensicAnalyzerActivity.this,
                        uri);

                report = result.report;
                csv = result.csv;

                handler.post(() -> {
                    statusView.setText(
                        "Analisa selesai. File: " +
                        result.files +
                        " | Folder: " +
                        result.directories +
                        " | Nested ISO: " +
                        result.nestedImages +
                        " | ELF: " +
                        result.executables +
                        " | Error: " +
                        result.errors);

                    reportView.setText(report);
                });
            }
            catch(Exception error)
            {
                handler.post(() -> {
                    statusView.setText("Analisa gagal.");

                    reportView.setText(
                        error.toString());

                    Toast.makeText(
                        this,
                        "Gagal menganalisa: " +
                            error.getMessage(),
                        Toast.LENGTH_LONG).show();
                });
            }
        }).start();
    }

    private void saveDocument(int requestCode)
    {
        if(report == null || report.isEmpty())
        {
            Toast.makeText(
                this,
                "Pilih dan analisa ISO terlebih dahulu.",
                Toast.LENGTH_LONG).show();
            return;
        }

        String stamp = new SimpleDateFormat(
            "yyyyMMdd_HHmmss",
            Locale.US).format(new Date());

        Intent intent = new Intent(
            Intent.ACTION_CREATE_DOCUMENT);

        intent.addCategory(Intent.CATEGORY_OPENABLE);

        if(requestCode == REQUEST_SAVE_TXT)
        {
            intent.setType("text/plain");
            intent.putExtra(
                Intent.EXTRA_TITLE,
                "ORBIT2SX_FORENSIC_" + stamp + ".txt");
        }
        else
        {
            intent.setType("application/zip");
            intent.putExtra(
                Intent.EXTRA_TITLE,
                "ORBIT2SX_FORENSIC_" + stamp + ".zip");
        }

        startActivityForResult(intent, requestCode);
    }

    @Override
    protected void onActivityResult(
        int requestCode,
        int resultCode,
        Intent data)
    {
        super.onActivityResult(
            requestCode,
            resultCode,
            data);

        if(resultCode != RESULT_OK ||
           data == null ||
           data.getData() == null)
        {
            return;
        }

        Uri uri = data.getData();

        if(requestCode == REQUEST_OPEN_ISO)
        {
            analyze(uri);
            return;
        }

        if(requestCode != REQUEST_SAVE_TXT &&
           requestCode != REQUEST_SAVE_ZIP)
        {
            return;
        }

        try
        {
            OutputStream output =
                getContentResolver().openOutputStream(uri, "w");

            if(output == null)
            {
                throw new Exception(
                    "Output stream tidak tersedia.");
            }

            try
            {
                if(requestCode == REQUEST_SAVE_TXT)
                {
                    output.write(
                        report.getBytes(StandardCharsets.UTF_8));
                }
                else
                {
                    writeZip(output);
                }

                output.flush();
            }
            finally
            {
                output.close();
            }

            Toast.makeText(
                this,
                "Laporan forensic berhasil disimpan.",
                Toast.LENGTH_LONG).show();
        }
        catch(Exception error)
        {
            Toast.makeText(
                this,
                "Ekspor gagal: " + error.getMessage(),
                Toast.LENGTH_LONG).show();
        }
    }

    private void writeZip(OutputStream output)
        throws Exception
    {
        try(ZipOutputStream zip =
                new ZipOutputStream(output))
        {
            addEntry(
                zip,
                "01_FORENSIC_REPORT.txt",
                report);

            addEntry(
                zip,
                "02_FILE_INVENTORY.csv",
                csv);

            addEntry(
                zip,
                "03_ANALYSIS_SCOPE.txt",
                "ORBIT2SX PS2 ISO FORENSIC ANALYZER\n"
                + "Static analysis only.\n"
                + "ISO9660 filesystem detection.\n"
                + "Nested ISO recursion is bounded.\n"
                + "SHA-256 hashes are calculated per file.\n"
                + "ELF header and entry point inspection.\n"
                + "No proprietary asset decoder is included yet.\n"
                + "No runtime compatibility claim is made.\n");
        }
    }

    private void addEntry(
        ZipOutputStream zip,
        String name,
        String content)
        throws Exception
    {
        zip.putNextEntry(new ZipEntry(name));

        zip.write(
            content.getBytes(StandardCharsets.UTF_8));

        zip.closeEntry();
    }
}
