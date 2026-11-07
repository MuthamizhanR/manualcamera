package com.motioncam;

import android.app.Application;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.util.Log;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

public class MotionCamApp extends Application {
    private static final String TAG = "MotionCamApp";

    @Override
    public void onCreate() {
        super.onCreate();
        installCrashHandler(this);
    }

    public static void installCrashHandler(final Context context) {
        final Thread.UncaughtExceptionHandler defaultHandler = Thread.getDefaultUncaughtExceptionHandler();

        Thread.setDefaultUncaughtExceptionHandler((thread, throwable) -> {
            try {
                String report = buildCrashReport(context, thread, throwable);
                Log.e(TAG, "Uncaught exception caught by MotionCam crash handler:\n" + report);

                Intent intent = new Intent(context, CrashActivity.class);
                intent.putExtra(CrashActivity.EXTRA_CRASH_LOG, report);
                intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
                context.startActivity(intent);
            } catch (Throwable t) {
                Log.e(TAG, "Failed to launch CrashActivity", t);
                if (defaultHandler != null) {
                    defaultHandler.uncaughtException(thread, throwable);
                    return;
                }
            }

            // Terminate the crashing process cleanly so CrashActivity (in :crash_handler process) displays
            android.os.Process.killProcess(android.os.Process.myPid());
            System.exit(10);
        });
    }

    public static String buildCrashReport(Context context, Thread thread, Throwable throwable) {
        StringBuilder sb = new StringBuilder();
        SimpleDateFormat sdf = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US);
        sb.append("=== MotionCam Crash Report ===\n");
        sb.append("Time: ").append(sdf.format(new Date())).append("\n");
        sb.append("Device: ").append(Build.MANUFACTURER).append(" ").append(Build.MODEL).append("\n");
        sb.append("Android SDK: ").append(Build.VERSION.SDK_INT).append(" (Release: ").append(Build.VERSION.RELEASE).append(")\n");
        sb.append("App Version: ").append(BuildConfig.VERSION_NAME).append(" (Code: ").append(BuildConfig.VERSION_CODE).append(")\n");
        if (thread != null) {
            sb.append("Thread: ").append(thread.getName()).append(" (id: ").append(thread.getId()).append(")\n");
        }
        sb.append("\n=== Java Stack Trace ===\n");
        if (throwable != null) {
            StringWriter sw = new StringWriter();
            PrintWriter pw = new PrintWriter(sw);
            throwable.printStackTrace(pw);
            sb.append(sw.toString());
        } else {
            sb.append("(No throwable provided)\n");
        }

        sb.append("\n=== Recent Error & Native Logcat (*:E) ===\n");
        try {
            Process process = Runtime.getRuntime().exec(new String[]{"logcat", "-d", "-v", "time", "*:E"});
            BufferedReader reader = new BufferedReader(new InputStreamReader(process.getInputStream()));
            String line;
            int count = 0;
            while ((line = reader.readLine()) != null && count < 300) {
                sb.append(line).append("\n");
                count++;
            }
            reader.close();
            process.destroy();
        } catch (Exception e) {
            sb.append("Failed to read logcat: ").append(e.getMessage()).append("\n");
        }

        sb.append("\n=== MotionCam & Native Logcat (MotionCam:V, DEBUG:V, libc:V) ===\n");
        try {
            Process process = Runtime.getRuntime().exec(new String[]{
                    "logcat", "-d", "-v", "time", "-s", "MotionCam:V", "MotionCamService:V", "DEBUG:V", "libc:V"
            });
            BufferedReader reader = new BufferedReader(new InputStreamReader(process.getInputStream()));
            String line;
            int count = 0;
            while ((line = reader.readLine()) != null && count < 300) {
                sb.append(line).append("\n");
                count++;
            }
            reader.close();
            process.destroy();
        } catch (Exception e) {
            sb.append("Failed to read MotionCam logcat: ").append(e.getMessage()).append("\n");
        }

        return sb.toString();
    }
}
