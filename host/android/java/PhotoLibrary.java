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

final class PhotoLibrary {
    interface Completion { void complete(long token, long code, String description); }
    interface Events {
        void selected(long token,int result,String metadata,byte[] bytes);
        void read(long token,byte[] bytes,String metadata,int error);
        void authorized(long token,int status);
    }
    private static final int STORAGE_REQUEST = 23041;
    private final Activity activity;
    private final Handler main;
    private final Executor workers;
    private final Completion completion;
    private final Events events;
    private final List<Request> pendingPermission = new ArrayList<>();
    private boolean permissionPending;
    private volatile boolean closed;
    private static final class Request {
        final long token;
        final byte[] bytes;
        Request(long token, byte[] bytes) { this.token = token; this.bytes = bytes; }
    }
    PhotoLibrary(Activity activity, Handler main, Executor workers, Completion completion,Events events) {
        this.activity = activity; this.main = main; this.workers = workers; this.completion = completion;this.events=events;
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
        if(code==READ_REQUEST){readPermissionPending=false;List<long[]> ready=new ArrayList<>(authorizations);authorizations.clear();for(long[] request:ready){int status=authorization((int)request[1]);if(status==0)authorize(request[0],(int)request[1]);else events.authorized(request[0],status);}return true;}
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
        if(pickerRead!=null){cancelRead(pickerRead);pickerRead=null;}
        if(picker!=0){long token=picker;picker=0;events.selected(token,0,null,null);}
        for(long token:new ArrayList<>(reads.keySet()))cancel(token);
        for(long[] request:authorizations)events.authorized(request[0],authorization((int)request[1]));authorizations.clear();
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
    private static final int PICK_REQUEST=23042,READ_REQUEST=23043;
    private long picker;
    private ReadRequest pickerRead;
    private boolean readPermissionPending;
    private final List<long[]> authorizations=new ArrayList<>();
    private final java.util.concurrent.ConcurrentHashMap<Long,ReadRequest> reads=new java.util.concurrent.ConcurrentHashMap<>();
    private static final class ReadRequest {
        final long token;final String uri;final int[] options;final android.os.CancellationSignal cancellation=new android.os.CancellationSignal();
        volatile boolean cancelled;
        volatile java.io.InputStream input;
        ReadRequest(long token,String uri,int[] options){this.token=token;this.uri=uri;this.options=options;}
    }
    private android.content.Intent pickerIntent(){
        android.content.Intent intent=new android.content.Intent(Build.VERSION.SDK_INT>=33 ? MediaStore.ACTION_PICK_IMAGES : android.content.Intent.ACTION_OPEN_DOCUMENT);
        if(Build.VERSION.SDK_INT>=33){intent.setType("image/*");if(intent.resolveActivity(activity.getPackageManager())==null)intent=new android.content.Intent(android.content.Intent.ACTION_OPEN_DOCUMENT);}
        intent.setType("image/*");if(android.content.Intent.ACTION_OPEN_DOCUMENT.equals(intent.getAction()))intent.addCategory(android.content.Intent.CATEGORY_OPENABLE);
        intent.addFlags(android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION|android.content.Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);return intent;
    }
    boolean sourceAvailable(int source){return (source==0 || source==2) && pickerIntent().resolveActivity(activity.getPackageManager())!=null;}
    void pick(long token){main.post(()->{
        if(closed || picker!=0 || activity.isFinishing() || activity.isDestroyed()){events.selected(token,-1,"The photo picker is unavailable.",null);return;}
        picker=token;
        try{activity.startActivityForResult(pickerIntent(),PICK_REQUEST);events.selected(token,2,null,null);}
        catch(RuntimeException failure){picker=0;events.selected(token,-1,"No Android photo picker is available.",null);}
    });}
    void dismiss(long token){main.post(()->{if(picker==token){picker=0;if(pickerRead!=null){cancelRead(pickerRead);pickerRead=null;}activity.finishActivity(PICK_REQUEST);}});}
    boolean result(int request,int result,android.content.Intent data){
        if(request!=PICK_REQUEST)return false;long token=picker;if(token==0)return true;
        Uri uri=result==Activity.RESULT_OK && data!=null ? data.getData() : null;
        if(uri==null){picker=0;events.selected(token,0,null,null);return true;}
        try{activity.getContentResolver().takePersistableUriPermission(uri,android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION);}catch(SecurityException ignored){}
        ReadRequest selected=new ReadRequest(token,uri.toString(),new int[]{1,1,0,0,0,0,0});pickerRead=selected;
        try{workers.execute(()->{
            byte[] bytes=null;String metadata=null;int outcome=1;
            try{Object[] value=readNow(selected);bytes=(byte[])value[0];metadata=(String)value[1];if((int)value[2]!=0){outcome=-1;metadata="Android could not read the selected image.";}}
            catch(RuntimeException failure){outcome=-1;metadata="Android could not read the selected image.";}
            byte[] image=bytes;String info=metadata;int status=outcome;main.post(()->{if(picker!=token)return;picker=0;pickerRead=null;events.selected(token,status,info,image);});
        });}catch(java.util.concurrent.RejectedExecutionException failure){picker=0;events.selected(token,-1,"The photo worker is unavailable.",null);}
        return true;
    }
    int authorization(int access){
        if(access==1 && Build.VERSION.SDK_INT>=29)return 3;
        String permission=access==1 ? Manifest.permission.WRITE_EXTERNAL_STORAGE : Build.VERSION.SDK_INT>=33 ? Manifest.permission.READ_MEDIA_IMAGES : Manifest.permission.READ_EXTERNAL_STORAGE;
        if(activity.checkSelfPermission(permission)==PackageManager.PERMISSION_GRANTED)return 3;
        if(access!=1 && Build.VERSION.SDK_INT>=34 && activity.checkSelfPermission(Manifest.permission.READ_MEDIA_VISUAL_USER_SELECTED)==PackageManager.PERMISSION_GRANTED)return 4;
        return activity.getPreferences(Activity.MODE_PRIVATE).getBoolean("photo.permission."+access,false) ? 2 : 0;
    }
    void authorize(long token,int access){main.post(()->{
        int status=authorization(access);if(status==3 || status==4 || closed){events.authorized(token,status);return;}
        String permission=access==1 ? Manifest.permission.WRITE_EXTERNAL_STORAGE : Build.VERSION.SDK_INT>=33 ? Manifest.permission.READ_MEDIA_IMAGES : Manifest.permission.READ_EXTERNAL_STORAGE;
        try{String[] declared=activity.getPackageManager().getPackageInfo(activity.getPackageName(),PackageManager.GET_PERMISSIONS).requestedPermissions;
            if(declared==null || !java.util.Arrays.asList(declared).contains(permission)){events.authorized(token,2);return;}
        }catch(PackageManager.NameNotFoundException missing){events.authorized(token,2);return;}
        authorizations.add(new long[]{token,access});if(readPermissionPending)return;readPermissionPending=true;
        activity.getPreferences(Activity.MODE_PRIVATE).edit().putBoolean("photo.permission."+access,true).apply();
        String[] requested=access!=1 && Build.VERSION.SDK_INT>=34 ? new String[]{permission,Manifest.permission.READ_MEDIA_VISUAL_USER_SELECTED} : new String[]{permission};
        try{activity.requestPermissions(requested,READ_REQUEST);}catch(RuntimeException failure){readPermissionPending=false;for(long[] pending:authorizations)events.authorized(pending[0],2);authorizations.clear();}
    });}
    private static void field(org.json.JSONObject record,String key,Object value){try{record.put(key,value);}catch(org.json.JSONException failure){throw new IllegalArgumentException(failure);}}
    private org.json.JSONObject metadata(Uri uri) throws IOException {
        if(!"content".equals(uri.getScheme()))throw new IOException("Unsupported photo URI");
        ContentResolver resolver=activity.getContentResolver();String mime=resolver.getType(uri);if(mime==null || !mime.startsWith("image/"))throw new IOException("Not an image");
        org.json.JSONObject record=new org.json.JSONObject();field(record,"uri",uri.toString());field(record,"mime",mime);
        try(android.database.Cursor cursor=resolver.query(uri,null,null,null,null)){
            if(cursor!=null && cursor.moveToFirst()){
                String[] columns={"width","height","datetaken","date_modified","_size","_display_name","is_favorite"};String[] keys={"width","height","date","modified","size","name","favorite"};
                for(int i=0;i<columns.length;i++){int index=cursor.getColumnIndex(columns[i]);if(index>=0 && !cursor.isNull(index))field(record,keys[i],i==5 ? cursor.getString(index) : cursor.getLong(index));}
            }
        }
        return record;
    }
    String[] assets(String[] identifiers,int limit){
        ArrayList<String> result=new ArrayList<>();java.util.HashSet<String> seen=new java.util.HashSet<>();
        if(identifiers==null){
            if(authorization(2)!=3 && authorization(2)!=4)return new String[0];
            String[] columns={"_id","width","height","datetaken","date_modified","mime_type"};android.os.Bundle query=new android.os.Bundle();query.putStringArray(ContentResolver.QUERY_ARG_SORT_COLUMNS,new String[]{"_id"});query.putInt(ContentResolver.QUERY_ARG_SORT_DIRECTION,ContentResolver.QUERY_SORT_DIRECTION_ASCENDING);if(limit>0)query.putInt(ContentResolver.QUERY_ARG_LIMIT,limit);
            try(android.database.Cursor cursor=activity.getContentResolver().query(MediaStore.Images.Media.EXTERNAL_CONTENT_URI,columns,query,null)){
                if(cursor!=null)while(cursor.moveToNext()){
                    org.json.JSONObject record=new org.json.JSONObject();field(record,"uri",android.content.ContentUris.withAppendedId(MediaStore.Images.Media.EXTERNAL_CONTENT_URI,cursor.getLong(0)).toString());field(record,"width",cursor.getLong(1));field(record,"height",cursor.getLong(2));field(record,"date",cursor.getLong(3));field(record,"modified",cursor.getLong(4));field(record,"mime",cursor.getString(5));result.add(record.toString());if(limit>0 && result.size()>=limit)break;
                }
            }catch(SecurityException denied){return new String[0];}
            return result.toArray(new String[0]);
        }
        for(String identifier:identifiers){if(identifier==null || !seen.add(identifier))continue;
            try{Uri uri=Uri.parse(identifier);org.json.JSONObject record=metadata(uri);if(record.optInt("width")<=0 || record.optInt("height")<=0){android.graphics.BitmapFactory.Options bounds=new android.graphics.BitmapFactory.Options();bounds.inJustDecodeBounds=true;try(java.io.InputStream stream=activity.getContentResolver().openInputStream(uri)){android.graphics.BitmapFactory.decodeStream(stream,null,bounds);}if(bounds.outWidth<=0 || bounds.outHeight<=0)continue;field(record,"width",bounds.outWidth);field(record,"height",bounds.outHeight);}result.add(record.toString());}catch(IOException|RuntimeException unavailable){}
            if(limit>0 && result.size()>=limit)break;
        }
        return result.toArray(new String[0]);
    }
    Object[] read(String uri,int[] options){return readNow(new ReadRequest(0,uri,options));}
    void request(long token,String uri,int[] options){
        ReadRequest request=new ReadRequest(token,uri,options);reads.put(token,request);
        if(closed){cancel(token);return;}
        try{workers.execute(()->{Object[] result=readNow(request);main.post(()->{if(reads.remove(token,request))events.read(token,(byte[])result[0],(String)result[1],(int)result[2]);});});}
        catch(java.util.concurrent.RejectedExecutionException failure){if(reads.remove(token,request))main.post(()->events.read(token,null,null,3302));}
    }
    private void cancelRead(ReadRequest request){request.cancelled=true;request.cancellation.cancel();java.io.InputStream stream=request.input;if(stream!=null)try{stream.close();}catch(IOException ignored){}}
    void cancel(long token){ReadRequest request=reads.remove(token);if(request==null)return;cancelRead(request);main.post(()->events.read(token,null,null,3072));}
    private Object[] readNow(ReadRequest request){
        try{
            if(request.cancelled)throw new android.os.OperationCanceledException();
            int[] o=request.options;if(o[2]!=0)return new Object[]{null,null,3303};
            Uri uri=Uri.parse(request.uri);String authority=uri.getAuthority();
            String path=uri.getPath();boolean local="com.android.providers.media.documents".equals(authority) || "media".equals(authority) && path!=null && (path.startsWith("/external/") || path.startsWith("/external_primary/") || path.startsWith("/internal/") || path.contains("/com.android.providers.media.photopicker/"));
            if(o[1]==0 && !local)return new Object[]{null,null,3303};
            org.json.JSONObject record=metadata(uri);byte[] bytes;
            try(android.content.res.AssetFileDescriptor file=activity.getContentResolver().openAssetFileDescriptor(uri,"r",request.cancellation)){
                if(file==null)throw new IOException("Image unavailable");
                try(java.io.InputStream input=file.createInputStream();java.io.ByteArrayOutputStream output=new java.io.ByteArrayOutputStream()){
                    request.input=input;if(request.cancelled)throw new android.os.OperationCanceledException();byte[] buffer=new byte[32768];for(int n;(n=input.read(buffer))!=-1;){if(request.cancelled)throw new android.os.OperationCanceledException();output.write(buffer,0,n);}bytes=output.toByteArray();
                }finally{request.input=null;}
            }
            int orientation=1;try{orientation=new android.media.ExifInterface(new java.io.ByteArrayInputStream(bytes)).getAttributeInt(android.media.ExifInterface.TAG_ORIENTATION,1);}catch(IOException ignored){}
            if(orientation<1 || orientation>8)orientation=1;field(record,"orientation",orientation);
            android.graphics.BitmapFactory.Options bounds=new android.graphics.BitmapFactory.Options();bounds.inJustDecodeBounds=true;android.graphics.BitmapFactory.decodeByteArray(bytes,0,bytes.length,bounds);
            if(bounds.outWidth>0 && bounds.outHeight>0){field(record,"width",bounds.outWidth);field(record,"height",bounds.outHeight);}
            if(o[0]!=0){
                if(bounds.outWidth<=0 || bounds.outHeight<=0)throw new IOException("Unsupported image format");
                android.graphics.BitmapFactory.Options decode=new android.graphics.BitmapFactory.Options();int targetWidth=o[3],targetHeight=o[4];
                if(targetWidth>0 && targetHeight>0 && o[6]!=0){int width=orientation>=5 ? bounds.outHeight : bounds.outWidth,height=orientation>=5 ? bounds.outWidth : bounds.outHeight;while(width/(decode.inSampleSize==0 ? 2 : decode.inSampleSize*2)>=targetWidth && height/(decode.inSampleSize==0 ? 2 : decode.inSampleSize*2)>=targetHeight)decode.inSampleSize=decode.inSampleSize==0 ? 2 : decode.inSampleSize*2;}
                android.graphics.Bitmap bitmap=android.graphics.BitmapFactory.decodeByteArray(bytes,0,bytes.length,decode);if(bitmap==null)throw new IOException("Image decode failed");
                android.graphics.Matrix matrix=new android.graphics.Matrix();switch(orientation){case 2:matrix.setScale(-1,1);break;case 3:matrix.setRotate(180);break;case 4:matrix.setScale(1,-1);break;case 5:matrix.setRotate(90);matrix.postScale(-1,1);break;case 6:matrix.setRotate(90);break;case 7:matrix.setRotate(-90);matrix.postScale(-1,1);break;case 8:matrix.setRotate(-90);break;}
                if(!matrix.isIdentity()){android.graphics.Bitmap rotated=android.graphics.Bitmap.createBitmap(bitmap,0,0,bitmap.getWidth(),bitmap.getHeight(),matrix,true);if(rotated!=bitmap)bitmap.recycle();bitmap=rotated;}
                if(targetWidth>0 && targetHeight>0 && o[6]!=0){float scale=o[5]==1 ? Math.max((float)targetWidth/bitmap.getWidth(),(float)targetHeight/bitmap.getHeight()) : Math.min((float)targetWidth/bitmap.getWidth(),(float)targetHeight/bitmap.getHeight());
                    int width=Math.max(1,Math.round(bitmap.getWidth()*scale)),height=Math.max(1,Math.round(bitmap.getHeight()*scale));android.graphics.Bitmap resized=android.graphics.Bitmap.createScaledBitmap(bitmap,width,height,true);if(resized!=bitmap)bitmap.recycle();bitmap=resized;
                    if(o[5]==1){android.graphics.Bitmap cropped=android.graphics.Bitmap.createBitmap(bitmap,Math.max(0,(width-targetWidth)/2),Math.max(0,(height-targetHeight)/2),Math.min(width,targetWidth),Math.min(height,targetHeight));if(cropped!=bitmap)bitmap.recycle();bitmap=cropped;}}
                try(java.io.ByteArrayOutputStream output=new java.io.ByteArrayOutputStream()){if(!bitmap.compress(android.graphics.Bitmap.CompressFormat.PNG,100,output))throw new IOException("Image encoding failed");bytes=output.toByteArray();}finally{bitmap.recycle();}
                field(record,"mime","image/png");field(record,"orientation",1);
            }
            return new Object[]{bytes,record.toString(),0};
        }catch(android.os.OperationCanceledException cancelled){return new Object[]{null,null,3072};}
        catch(SecurityException denied){return new Object[]{null,null,3311};}
        catch(IOException|RuntimeException failure){return new Object[]{null,null,3302};}
    }
    private static boolean isPng(byte[] bytes) {
        return bytes != null && bytes.length >= 33 && bytes[0] == (byte)137 && bytes[1] == 80 && bytes[2] == 78 && bytes[3] == 71 &&
                bytes[4] == 13 && bytes[5] == 10 && bytes[6] == 26 && bytes[7] == 10;
    }
}
