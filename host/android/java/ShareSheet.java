package local.ios.host;

import android.app.Activity;
import android.content.ClipData;
import android.content.ComponentName;
import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Intent;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.Handler;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import android.webkit.MimeTypeMap;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.UUID;
import java.util.concurrent.Executor;

public final class ShareSheet {
    interface Completion { void complete(long token,int result); }
    private static final int REQUEST=704;
    private final Activity activity;
    private final Handler main;
    private final Executor workers;
    private final Completion completion;
    private long active;
    private boolean closed;
    ShareSheet(Activity activity,Handler main,Executor workers,Completion completion) {
        this.activity=activity; this.main=main; this.workers=workers; this.completion=completion;
    }
    void show(long token,String text,byte[][] data,String[] paths,String[] exclusions) {
        if (closed || active!=0) { completion.complete(token,-1); return; }
        active=token;
        try { workers.execute(()->prepare(token,text,data,paths,exclusions)); }
        catch (RuntimeException failure) { finish(token,-1); }
    }
    private void prepare(long token,String text,byte[][] data,String[] paths,String[] exclusions) {
        try {
            File directory=new File(activity.getCacheDir(),"shares");
            if (!directory.isDirectory() && !directory.mkdirs()) throw new IOException("Share cache unavailable");
            File[] previous=directory.listFiles(); long cutoff=System.currentTimeMillis()-24L*60*60*1000;
            if (previous!=null) for(File file:previous) if(file.isFile() && file.lastModified()<cutoff) file.delete();
            ArrayList<Uri> attachments=new ArrayList<>(); String type=null;
            for(int i=0;i<data.length;i++) {
                if (data[i]==null && paths[i]==null) continue;
                byte[] bytes=data[i]; String extension="bin",mime="application/octet-stream";
                if (bytes!=null) {
                    if(bytes.length>=8 && bytes[0]==(byte)137 && bytes[1]==80 && bytes[2]==78 && bytes[3]==71) { extension="png"; mime="image/png"; }
                    else if(bytes.length>=3 && bytes[0]==(byte)255 && bytes[1]==(byte)216 && bytes[2]==(byte)255) { extension="jpg"; mime="image/jpeg"; }
                    else if(bytes.length>=6 && bytes[0]==71 && bytes[1]==73 && bytes[2]==70) { extension="gif"; mime="image/gif"; }
                    else if(bytes.length>=12 && bytes[0]==82 && bytes[1]==73 && bytes[2]==70 && bytes[3]==70 && bytes[8]==87 && bytes[9]==69 && bytes[10]==66 && bytes[11]==80) { extension="webp"; mime="image/webp"; }
                } else {
                    String name=new File(paths[i]).getName(); int dot=name.lastIndexOf('.');
                    if(dot>=0 && name.substring(dot+1).matches("[A-Za-z0-9]{1,16}")) extension=name.substring(dot+1).toLowerCase(java.util.Locale.ROOT);
                    String resolved=MimeTypeMap.getSingleton().getMimeTypeFromExtension(extension); if(resolved!=null) mime=resolved;
                }
                File file=new File(directory,UUID.randomUUID()+"."+extension);
                if(bytes!=null) Files.write(file.toPath(),bytes); else Files.copy(new File(paths[i]).toPath(),file.toPath());
                attachments.add(new Uri.Builder().scheme("content").authority(activity.getPackageName()+".share").appendPath(file.getName()).build());
                if(type==null) type=mime;
                else if(!type.equals(mime)) type=type.startsWith("image/") && mime.startsWith("image/") ? "image/*" : "*/*";
            }
            String mime=type==null ? "text/plain" : type;
            main.post(()->launch(token,text,attachments,mime,exclusions));
        } catch(IOException | RuntimeException failure) { main.post(()->finish(token,-1)); }
    }
    private void launch(long token,String text,ArrayList<Uri> attachments,String mime,String[] exclusions) {
        if(closed || active!=token || activity.isFinishing() || activity.isDestroyed()) { finish(token,-1); return; }
        Intent send=new Intent(attachments.size()>1 ? Intent.ACTION_SEND_MULTIPLE : Intent.ACTION_SEND).setType(mime);
        if(text!=null && !text.isEmpty()) send.putExtra(Intent.EXTRA_TEXT,text);
        if(!attachments.isEmpty()) {
            if(attachments.size()==1) send.putExtra(Intent.EXTRA_STREAM,attachments.get(0)); else send.putParcelableArrayListExtra(Intent.EXTRA_STREAM,attachments);
            ClipData clip=ClipData.newRawUri(BuildProfile.APP_NAME,attachments.get(0));
            for(int i=1;i<attachments.size();i++) clip.addItem(new ClipData.Item(attachments.get(i)));
            send.setClipData(clip); send.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        }
        HashSet<String> blocked=new HashSet<>();
        for(String type:exclusions) {
            if(type.endsWith("PostToTwitter")) blocked.add("com.twitter.android");
            if(type.endsWith("PostToFacebook")) blocked.add("com.facebook.katana");
            if(type.endsWith("PostToWeibo")) blocked.add("com.sina.weibo");
            if(type.endsWith("Mail")) { blocked.add("com.google.android.gm"); blocked.add("com.samsung.android.email.provider"); }
        }
        ArrayList<ComponentName> excluded=new ArrayList<>();
        for(android.content.pm.ResolveInfo candidate:activity.getPackageManager().queryIntentActivities(send,0))
            if(blocked.contains(candidate.activityInfo.packageName)) excluded.add(new ComponentName(candidate.activityInfo.packageName,candidate.activityInfo.name));
        Intent chooser=Intent.createChooser(send,null);
        if(!excluded.isEmpty()) chooser.putExtra(Intent.EXTRA_EXCLUDE_COMPONENTS,excluded.toArray(new ComponentName[0]));
        try { activity.startActivityForResult(chooser,REQUEST); completion.complete(token,-2); }
        catch(RuntimeException failure) { finish(token,-1); }
    }
    private void finish(long token,int result) {
        if(active==token) { active=0; completion.complete(token,result); }
    }
    boolean result(int request,int result) {
        if(request!=REQUEST || active==0) return false;
        finish(active,result==Activity.RESULT_OK ? 1 : 0); return true;
    }
    void dismiss(long token) {
        if(active!=token) return;
        try { activity.finishActivity(REQUEST); } catch(RuntimeException ignored) {}
        finish(token,0);
    }
    void close() { closed=true; if(active!=0) dismiss(active); }

    public static final class Provider extends ContentProvider {
        @Override public boolean onCreate() { return true; }
        private File file(Uri uri) throws FileNotFoundException {
            if(uri.getPathSegments().size()!=1) throw new FileNotFoundException();
            File directory=new File(getContext().getCacheDir(),"shares"),file=new File(directory,uri.getLastPathSegment());
            try {
                if(!file.getCanonicalFile().getParentFile().equals(directory.getCanonicalFile()) || !file.isFile()) throw new FileNotFoundException();
            } catch(IOException failure) { throw new FileNotFoundException(); }
            return file;
        }
        @Override public ParcelFileDescriptor openFile(Uri uri,String mode) throws FileNotFoundException {
            if(!"r".equals(mode)) throw new FileNotFoundException();
            return ParcelFileDescriptor.open(file(uri),ParcelFileDescriptor.MODE_READ_ONLY);
        }
        @Override public String getType(Uri uri) {
            String extension=MimeTypeMap.getFileExtensionFromUrl(uri.toString());
            String type=MimeTypeMap.getSingleton().getMimeTypeFromExtension(extension); return type==null ? "application/octet-stream" : type;
        }
        @Override public Cursor query(Uri uri,String[] projection,String selection,String[] args,String order) {
            try {
                File file=file(uri); String[] columns=projection==null ? new String[]{OpenableColumns.DISPLAY_NAME,OpenableColumns.SIZE} : projection;
                MatrixCursor cursor=new MatrixCursor(columns,1); Object[] row=new Object[columns.length];
                for(int i=0;i<columns.length;i++) if(OpenableColumns.DISPLAY_NAME.equals(columns[i])) row[i]=file.getName(); else if(OpenableColumns.SIZE.equals(columns[i])) row[i]=file.length();
                cursor.addRow(row); return cursor;
            } catch(FileNotFoundException failure) { return null; }
        }
        @Override public Uri insert(Uri uri,ContentValues values) { throw new UnsupportedOperationException(); }
        @Override public int update(Uri uri,ContentValues values,String selection,String[] args) { throw new UnsupportedOperationException(); }
        @Override public int delete(Uri uri,String selection,String[] args) { throw new UnsupportedOperationException(); }
    }
}
