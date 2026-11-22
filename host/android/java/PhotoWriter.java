package local.ios.host;

import android.Manifest;
import android.app.Activity;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.os.Handler;
import android.provider.MediaStore;
import java.io.File;
import java.io.IOException;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.Executor;

final class PhotoWriter {
    interface Completion { void complete(long token, long code, String description); }
    private static final int STORAGE_REQUEST = 23041;
    private final Activity activity;
    private final Handler main;
    private final Executor workers;
    private final Completion completion;
    private final List<Request> pendingPermission = new ArrayList<>();
    private boolean permissionPending, closed;
    private static final class Request {
        final long token;
        final byte[] bytes;
        Request(long token, byte[] bytes) { this.token = token; this.bytes = bytes; }
    }
    PhotoWriter(Activity activity, Handler main, Executor workers, Completion completion) {
        this.activity = activity; this.main = main; this.workers = workers; this.completion = completion;
    }
    void save(long token, byte[] bytes) {
        main.post(() -> {
            Request request = new Request(token, bytes);
            if (closed || activity.isFinishing() || activity.isDestroyed()) { finish(request, 512, "Photo save activity is no longer available"); return; }
            if (!isPng(bytes)) { finish(request, 256, "UIImage has no valid PNG representation"); return; }
            if (Build.VERSION.SDK_INT <= 28 && activity.checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
                pendingPermission.add(request);
                if (!permissionPending) {
                    permissionPending = true;
                    activity.requestPermissions(new String[]{Manifest.permission.WRITE_EXTERNAL_STORAGE}, STORAGE_REQUEST);
                }
                return;
            }
            submit(request);
        });
    }
    boolean permissionResult(int code, int[] results) {
        if (code != STORAGE_REQUEST) return false;
        permissionPending = false;
        List<Request> requests = new ArrayList<>(pendingPermission);
        pendingPermission.clear();
        boolean granted = results.length != 0 && results[0] == PackageManager.PERMISSION_GRANTED;
        for (Request request : requests) {
            if (granted && !closed) submit(request);
            else finish(request, 513, "Permission to add a photo was denied");
        }
        return true;
    }
    void close() {
        closed = true;
        for (Request request : pendingPermission) finish(request, 512, "Photo save activity was closed");
        pendingPermission.clear();
    }
    private void finish(Request request, long code, String description) {
        main.post(() -> completion.complete(request.token, code, description));
    }
    private void submit(Request request) {
        try { workers.execute(() -> write(request)); }
        catch (java.util.concurrent.RejectedExecutionException rejected) { finish(request, 512, "Photo save worker is unavailable"); }
    }
    private void write(Request request) {
        ContentResolver resolver = activity.getContentResolver();
        Uri uri = null;
        try {
            String name = BuildProfile.APP_NAME + "-" + UUID.randomUUID() + ".png";
            ContentValues values = new ContentValues();
            values.put(MediaStore.Images.Media.DISPLAY_NAME, name);
            values.put(MediaStore.Images.Media.MIME_TYPE, "image/png");
            if (Build.VERSION.SDK_INT >= 29) {
                values.put(MediaStore.Images.Media.RELATIVE_PATH, Environment.DIRECTORY_PICTURES + "/" + BuildProfile.APP_NAME);
                values.put(MediaStore.Images.Media.IS_PENDING, 1);
            } else {
                File directory = new File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_PICTURES), BuildProfile.APP_NAME);
                if (!directory.isDirectory() && !directory.mkdirs()) throw new IOException("Cannot create photo directory");
                values.put(MediaStore.Images.Media.DATA, new File(directory, name).getAbsolutePath());
            }
            uri = resolver.insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values);
            if (uri == null) throw new IOException("MediaStore did not create a photo");
            try (OutputStream output = resolver.openOutputStream(uri, "w")) {
                if (output == null) throw new IOException("MediaStore did not open the photo");
                output.write(request.bytes);
            }
            if (Build.VERSION.SDK_INT >= 29) {
                values.clear(); values.put(MediaStore.Images.Media.IS_PENDING, 0);
                if (resolver.update(uri, values, null, null) != 1) throw new IOException("MediaStore did not publish the photo");
            }
            finish(request, 0, null);
        } catch (IOException | RuntimeException error) {
             
            if (uri != null) { try { resolver.delete(uri, null, null); } catch (RuntimeException ignored) {} }
            finish(request, error instanceof SecurityException ? 513 : 512, "Android could not save the photo: " + error.getClass().getSimpleName());
        }
    }
    private static boolean isPng(byte[] bytes) {
        return bytes != null && bytes.length >= 33 && bytes[0] == (byte)137 && bytes[1] == 80 && bytes[2] == 78 && bytes[3] == 71 &&
                bytes[4] == 13 && bytes[5] == 10 && bytes[6] == 26 && bytes[7] == 10;
    }
}
