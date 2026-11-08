package com.motioncam;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

public class CrashActivity extends Activity {
    public static final String EXTRA_CRASH_LOG = "extra_crash_log";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        final String crashReport = getIntent().getStringExtra(EXTRA_CRASH_LOG);
        final String displayLog = (crashReport != null && !crashReport.isEmpty())
                ? crashReport
                : "No crash information recorded.";

        // Root vertical layout
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Color.parseColor("#121212"));
        root.setPadding(32, 48, 32, 32);
        root.setLayoutParams(new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));

        // Header Title
        TextView title = new TextView(this);
        title.setText("MotionCam Crash Detected");
        title.setTextColor(Color.parseColor("#FF5252"));
        title.setTextSize(20);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        root.addView(title);

        // Subtitle
        TextView subtitle = new TextView(this);
        subtitle.setText("An unexpected error occurred. Copy the diagnostic log below to report or debug.");
        subtitle.setTextColor(Color.parseColor("#B0BEC5"));
        subtitle.setTextSize(13);
        subtitle.setPadding(0, 8, 0, 24);
        root.addView(subtitle);

        // Buttons Layout
        LinearLayout buttonRow = new LinearLayout(this);
        buttonRow.setOrientation(LinearLayout.HORIZONTAL);
        buttonRow.setLayoutParams(new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        buttonRow.setPadding(0, 0, 0, 16);

        Button copyBtn = new Button(this);
        copyBtn.setText("Copy Log");
        copyBtn.setBackgroundColor(Color.parseColor("#2196F3"));
        copyBtn.setTextColor(Color.WHITE);
        LinearLayout.LayoutParams btnParams = new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1.0f);
        btnParams.setMargins(0, 0, 8, 0);
        copyBtn.setLayoutParams(btnParams);
        copyBtn.setOnClickListener(v -> {
            ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
            if (cm != null) {
                cm.setPrimaryClip(ClipData.newPlainText("MotionCam Crash Log", displayLog));
                Toast.makeText(this, "Crash log copied to clipboard!", Toast.LENGTH_SHORT).show();
            }
        });
        buttonRow.addView(copyBtn);

        Button restartBtn = new Button(this);
        restartBtn.setText("Restart");
        restartBtn.setBackgroundColor(Color.parseColor("#37474F"));
        restartBtn.setTextColor(Color.WHITE);
        LinearLayout.LayoutParams restartParams = new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1.0f);
        restartParams.setMargins(8, 0, 8, 0);
        restartBtn.setLayoutParams(restartParams);
        restartBtn.setOnClickListener(v -> {
            Intent intent = new Intent(this, CameraActivity.class);
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
            startActivity(intent);
            finish();
        });
        buttonRow.addView(restartBtn);

        Button closeBtn = new Button(this);
        closeBtn.setText("Close");
        closeBtn.setBackgroundColor(Color.parseColor("#263238"));
        closeBtn.setTextColor(Color.WHITE);
        LinearLayout.LayoutParams closeParams = new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1.0f);
        closeParams.setMargins(8, 0, 0, 0);
        closeBtn.setLayoutParams(closeParams);
        closeBtn.setOnClickListener(v -> {
            finishAffinity();
            System.exit(0);
        });
        buttonRow.addView(closeBtn);

        root.addView(buttonRow);

        // Scrollable Log Viewer
        ScrollView scrollView = new ScrollView(this);
        LinearLayout.LayoutParams scrollParams = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                0, 1.0f);
        scrollView.setLayoutParams(scrollParams);
        scrollView.setBackgroundColor(Color.parseColor("#1E1E1E"));
        scrollView.setPadding(16, 16, 16, 16);

        TextView logView = new TextView(this);
        logView.setText(displayLog);
        logView.setTextColor(Color.parseColor("#CFD8DC"));
        logView.setTextSize(11);
        logView.setTypeface(Typeface.MONOSPACE);
        logView.setTextIsSelectable(true);
        scrollView.addView(logView);

        root.addView(scrollView);

        setContentView(root);
    }
}
