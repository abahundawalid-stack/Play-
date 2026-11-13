package com.virtualapplications.play;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.ByteArrayOutputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

public class DiagnosticsActivity extends Activity
{
    private static final int REQUEST_EXPORT_TXT = 7101;
    private static final int REQUEST_EXPORT_ZIP = 7102;

    private String currentReport = "";

    @Override
    protected void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);

        currentReport = DiagnosticsManager.buildReport(this);
        buildScreen();
    }

    @Override
    protected void onResume()
    {
        super.onResume();

        currentReport = DiagnosticsManager.buildReport(this);
    }

    private void buildScreen()
    {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(20, 20, 20, 20);

        TextView title = new TextView(this);
        title.setText("ANALISA PERFORMA PLAY!");
        title.setTextSize(21);
        title.setPadding(0, 0, 0, 16);
        root.addView(title);

        TextView subtitle = new TextView(this);
        subtitle.setText(
            "Laporan sesi game, grafis, audio, dan perangkat");
        subtitle.setTextSize(13);
        subtitle.setPadding(0, 0, 0, 16);
        root.addView(subtitle);

        Button refresh = new Button(this);
        refresh.setText("Perbarui Analisa");
        refresh.setOnClickListener(v -> {
            currentReport = DiagnosticsManager.buildReport(this);
            reportView.setText(currentReport);
        });
        root.addView(refresh);

        Button exportTxt = new Button(this);
        exportTxt.setText("Download Laporan TXT");
        exportTxt.setOnClickListener(v -> createDocument(REQUEST_EXPORT_TXT));
        root.addView(exportTxt);

        Button exportZip = new Button(this);
        exportZip.setText("Download Paket Analisa ZIP");
        exportZip.setOnClickListener(v -> createDocument(REQUEST_EXPORT_ZIP));
        root.addView(exportZip);

        reportView = new TextView(this);
        reportView.setTextSize(12);
        reportView.setTextIsSelectable(true);
        reportView.setPadding(0, 20, 0, 20);
        reportView.setText(currentReport);

        ScrollView scroll = new ScrollView(this);
        scroll.addView(reportView);

        root.addView(scroll,
            new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                0, 1));

        setContentView(root);
    }

    private TextView reportView;

    private void createDocument(int requestCode)
    {
        String stamp = new SimpleDateFormat(
            "yyyyMMdd_HHmmss", Locale.US).format(new Date());

        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);

        if(requestCode == REQUEST_EXPORT_TXT)
        {
            intent.setType("text/plain");
            intent.putExtra(
                Intent.EXTRA_TITLE,
                "PLAY_DIAGNOSTIC_" + stamp + ".txt");
        }
        else
        {
            intent.setType("application/zip");
            intent.putExtra(
                Intent.EXTRA_TITLE,
                "PLAY_DIAGNOSTIC_" + stamp + ".zip");
        }

        startActivityForResult(intent, requestCode);
    }

    @Override
    protected void onActivityResult(
        int requestCode, int resultCode, Intent data)
    {
        super.onActivityResult(requestCode, resultCode, data);

        if(resultCode != RESULT_OK || data == null ||
           data.getData() == null)
        {
            return;
        }

        try
        {
            byte[] output;

            if(requestCode == REQUEST_EXPORT_TXT)
            {
                output = currentReport.getBytes(StandardCharsets.UTF_8);
            }
            else if(requestCode == REQUEST_EXPORT_ZIP)
            {
                output = createZip();
            }
            else
            {
                return;
            }

            writeDocument(data.getData(), output);

            Toast.makeText(
                this,
                "Laporan berhasil disimpan.",
                Toast.LENGTH_LONG).show();
        }
        catch(Exception e)
        {
            Toast.makeText(
                this,
                "Gagal menyimpan laporan: " + e.getMessage(),
                Toast.LENGTH_LONG).show();
        }
    }

    private byte[] createZip() throws Exception
    {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();

        ZipOutputStream zip = new ZipOutputStream(bytes);

        addZipEntry(
            zip,
            "diagnostic-report.txt",
            currentReport.getBytes(StandardCharsets.UTF_8));

        String json =
            "{\n" +
            "  \"application\": \"Play! Android\",\n" +
            "  \"report_format\": 1,\n" +
            "  \"report_file\": \"diagnostic-report.txt\"\n" +
            "}\n";

        addZipEntry(
            zip,
            "diagnostic-report.json",
            json.getBytes(StandardCharsets.UTF_8));

        zip.finish();
        zip.close();

        return bytes.toByteArray();
    }

    private void addZipEntry(
        ZipOutputStream zip, String name, byte[] content)
        throws Exception
    {
        zip.putNextEntry(new ZipEntry(name));
        zip.write(content);
        zip.closeEntry();
    }

    private void writeDocument(Uri uri, byte[] content)
        throws Exception
    {
        OutputStream stream =
            getContentResolver().openOutputStream(uri, "w");

        if(stream == null)
        {
            throw new Exception("Output stream tidak tersedia.");
        }

        try
        {
            stream.write(content);
            stream.flush();
        }
        finally
        {
            stream.close();
        }
    }
}
