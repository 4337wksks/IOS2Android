package local.ios.host;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.location.Address;
import android.location.Geocoder;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.os.Build;
import android.os.CancellationSignal;
import android.os.Handler;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.concurrent.Executor;
import java.util.function.Consumer;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

final class LocationSource {
    interface Delivery { String location(long token,int generation,double[] values,int code,String message); }
    interface Authorization { String changed(long token,int status); }
    interface Geocode { String completed(long token,int generation,String places,int code,String message); }
    private static final int PERMISSION_REQUEST=0x4932;
    private final Activity activity;
    private final LocationManager manager;
    private final Handler main;
    private final Executor workers;
    private final Delivery delivery;
    private final Authorization authorization;
    private final Geocode geocode;
    private final Consumer<String> failure;
    private final HashMap<Long,Connection> connections=new HashMap<>();
    private final HashMap<Long,GeocodeRequest> geocodes=new HashMap<>();
    private final Set<Long> permissionRequests=new HashSet<>();
    private final Set<String> manifestPermissions=new HashSet<>();
    private boolean closed,permissionPending,permissionRequested;

    LocationSource(Activity activity,Handler main,Executor workers,Delivery delivery,Authorization authorization,Geocode geocode,Consumer<String> failure){
        this.activity=activity;this.main=main;this.workers=workers;this.delivery=delivery;this.authorization=authorization;this.geocode=geocode;this.failure=failure;
        manager=(LocationManager)activity.getSystemService(Activity.LOCATION_SERVICE);
        permissionRequested=activity.getPreferences(Activity.MODE_PRIVATE).getBoolean("locationPermissionRequested",false);
        try{String[] names=activity.getPackageManager().getPackageInfo(activity.getPackageName(),PackageManager.GET_PERMISSIONS).requestedPermissions;
            if(names!=null)java.util.Collections.addAll(manifestPermissions,names);
        }catch(PackageManager.NameNotFoundException ignored){}
    }
    private boolean granted(String permission){return activity.checkSelfPermission(permission)==PackageManager.PERMISSION_GRANTED;}
    int authorization(){
        if(!granted(Manifest.permission.ACCESS_FINE_LOCATION) && !granted(Manifest.permission.ACCESS_COARSE_LOCATION))
            return permissionRequested || !declared(Manifest.permission.ACCESS_COARSE_LOCATION) ? 2 : 0;
        return Build.VERSION.SDK_INT<29 || granted(Manifest.permission.ACCESS_BACKGROUND_LOCATION) ? 3 : 4;
    }
    boolean enabled(){return manager!=null && manager.isLocationEnabled();}
    private boolean declared(String permission){
        return manifestPermissions.contains(permission);
    }
    void requestAuthorization(long token){main.post(()->{
        if(closed)return;
        permissionRequests.add(token);
        if(authorization()>=3 || !declared(Manifest.permission.ACCESS_COARSE_LOCATION)){completePermissions();return;}
        if(permissionPending)return;
        permissionPending=true;
        permissionRequested=true;
        activity.getPreferences(Activity.MODE_PRIVATE).edit().putBoolean("locationPermissionRequested",true).apply();
        ArrayList<String> permissions=new ArrayList<>();permissions.add(Manifest.permission.ACCESS_COARSE_LOCATION);
        if(declared(Manifest.permission.ACCESS_FINE_LOCATION))permissions.add(Manifest.permission.ACCESS_FINE_LOCATION);
        activity.requestPermissions(permissions.toArray(new String[0]),PERMISSION_REQUEST);
    });}
    void permissionResult(int request){if(request==PERMISSION_REQUEST){permissionPending=false;completePermissions();}}
    private void completePermissions(){
        int status=authorization();Set<Long> pending=new HashSet<>(permissionRequests);permissionRequests.clear();
        for(long token:pending){String error=authorization.changed(token,status);if(error!=null)failure.accept(error);}
    }
    private String provider(double accuracy){
        if(manager==null)return null;
        List<String> available=manager.getProviders(true);
        if(Build.VERSION.SDK_INT>=31 && available.contains(LocationManager.FUSED_PROVIDER))return LocationManager.FUSED_PROVIDER;
        if(granted(Manifest.permission.ACCESS_FINE_LOCATION) && accuracy<=10 && available.contains(LocationManager.GPS_PROVIDER))return LocationManager.GPS_PROVIDER;
        if(available.contains(LocationManager.NETWORK_PROVIDER))return LocationManager.NETWORK_PROVIDER;
        return granted(Manifest.permission.ACCESS_FINE_LOCATION) && available.contains(LocationManager.GPS_PROVIDER) ? LocationManager.GPS_PROVIDER : null;
    }
    static double[] values(Location value){
        if(value==null)return null;
        return new double[]{value.getTime()*0.001,value.getLatitude(),value.getLongitude(),value.hasAltitude() ? value.getAltitude() : 0,
            value.hasAccuracy() ? value.getAccuracy() : -1,value.hasVerticalAccuracy() ? value.getVerticalAccuracyMeters() : -1,
            value.hasBearing() ? value.getBearing() : -1,value.hasSpeed() ? value.getSpeed() : -1,
            value.hasBearingAccuracy() ? value.getBearingAccuracyDegrees() : -1,value.hasSpeedAccuracy() ? value.getSpeedAccuracyMetersPerSecond() : -1,
            Build.VERSION.SDK_INT>=31 ? (value.isMock() ? 1 : 0) : (value.isFromMockProvider() ? 1 : 0)};
    }
    double[] lastLocation(){
        if(authorization()<3 || manager==null)return null;
        try{Location latest=null;for(String provider:manager.getProviders(true)){Location value=manager.getLastKnownLocation(provider);if(value!=null && (latest==null || value.getTime()>latest.getTime()))latest=value;}return values(latest);}
        catch(SecurityException denied){return null;}
    }
    void start(long token,int generation,double accuracy,double distance,boolean once){main.post(()->{
        stop(token);if(closed)return;
        Connection connection=new Connection(token,generation,once);connections.put(token,connection);
        if(authorization()<3){connection.error(1,"Location permission is not granted to this Android application.");return;}
        String provider=provider(accuracy);if(!enabled() || provider==null){connection.error(0,"A location provider is not available.");return;}
        try{
            if(Build.VERSION.SDK_INT>=31){
                android.location.LocationRequest request=new android.location.LocationRequest.Builder(0)
                        .setQuality(accuracy<=10 ? android.location.LocationRequest.QUALITY_HIGH_ACCURACY : accuracy<=100 ? android.location.LocationRequest.QUALITY_BALANCED_POWER_ACCURACY : android.location.LocationRequest.QUALITY_LOW_POWER)
                        .setMinUpdateDistanceMeters(once ? 0 : (float)Math.max(0,distance)).build();
                if(once){connection.cancellation=new CancellationSignal();manager.getCurrentLocation(provider,request,connection.cancellation,main::post,connection::received);}
                else manager.requestLocationUpdates(provider,request,main::post,connection);
            }
            else if(once && Build.VERSION.SDK_INT>=30){connection.cancellation=new CancellationSignal();manager.getCurrentLocation(provider,connection.cancellation,main::post,connection::received);}
            else manager.requestLocationUpdates(provider,0,once ? 0 : (float)Math.max(0,distance),connection,main.getLooper());
        }catch(SecurityException denied){connection.error(1,"Location permission is not granted to this Android application.");}
        catch(IllegalArgumentException unavailable){connection.error(0,"A location provider is not available.");}
    });}
    void stop(long token){
        Connection previous=connections.remove(token);if(previous==null)return;previous.active=false;
        if(previous.cancellation!=null)previous.cancellation.cancel();
        if(manager!=null)manager.removeUpdates(previous);
    }
    void stopAsync(long token){main.post(()->stop(token));}
    private final class Connection implements LocationListener {
        final long token;final int generation;final boolean once;boolean active=true;CancellationSignal cancellation;
        Connection(long token,int generation,boolean once){this.token=token;this.generation=generation;this.once=once;}
        void error(int code,String message){if(!active)return;stop(token);String error=delivery.location(token,generation,null,code,message);if(error!=null)failure.accept(error);}
        void received(Location value){
            if(!active)return;if(value==null){error(0,"The location provider has no current position.");return;}
            if(once)stop(token);String error=delivery.location(token,generation,values(value),0,null);
            if(error!=null){stop(token);failure.accept(error);}
        }
        @Override public void onLocationChanged(Location value){received(value);}
        @Override public void onProviderDisabled(String provider){error(0,"The location provider was disabled.");}
        @Override public void onProviderEnabled(String provider){}
        @Override public void onStatusChanged(String provider,int status,android.os.Bundle extras){}
    }
    private final class GeocodeRequest {
        final long token;final int generation;
        GeocodeRequest(long token,int generation){this.token=token;this.generation=generation;}
        void completed(List<Address> addresses,int code,String message){main.post(()->{
            if(closed || geocodes.get(token)!=this)return;geocodes.remove(token);String places=null;
            int resultCode=code;String resultMessage=message;
            if(resultCode==0){try{places=encode(addresses);if(addresses==null || addresses.isEmpty()){resultCode=8;resultMessage="No place was found for this request.";}}
                catch(JSONException invalid){resultCode=2;resultMessage="The geocoding service returned invalid address data.";}}
            String error=geocode.completed(token,generation,places,resultCode,resultMessage);if(error!=null)failure.accept(error);
        });}
    }
    void geocode(long token,int generation,String query,double latitude,double longitude,double[] bounds,String language){main.post(()->{
        if(closed)return;
        GeocodeRequest request=new GeocodeRequest(token,generation);geocodes.put(token,request);
        if(!Geocoder.isPresent()){request.completed(null,2,"A geocoding service is not available on this Android device.");return;}
        Locale locale=language==null ? activity.getResources().getConfiguration().getLocales().get(0) : Locale.forLanguageTag(language.replace('_','-'));
        Geocoder service=new Geocoder(activity,locale);
        if(Build.VERSION.SDK_INT>=33){
            Geocoder.GeocodeListener listener=new Geocoder.GeocodeListener(){
                @Override public void onGeocode(List<Address> addresses){request.completed(addresses,0,null);}
                @Override public void onError(String message){request.completed(null,2,message==null ? "The geocoding service could not complete this request." : message);}
            };
            try{if(query==null)service.getFromLocation(latitude,longitude,10,listener);
                else if(bounds!=null && bounds.length==4)service.getFromLocationName(query,10,bounds[0],bounds[1],bounds[2],bounds[3],listener);
                else service.getFromLocationName(query,10,listener);
            }catch(IllegalArgumentException invalid){request.completed(null,8,"The geocoding coordinates or address are invalid.");}
        }else workers.execute(()->{
            try{List<Address> addresses=query==null ? service.getFromLocation(latitude,longitude,10) : bounds!=null && bounds.length==4 ? service.getFromLocationName(query,10,bounds[0],bounds[1],bounds[2],bounds[3]) : service.getFromLocationName(query,10);request.completed(addresses,0,null);}
            catch(IOException unavailable){request.completed(null,2,"The geocoding service could not complete this request.");}
            catch(IllegalArgumentException invalid){request.completed(null,8,"The geocoding coordinates or address are invalid.");}
        });
    });}
    void cancelGeocode(long token){main.post(()->geocodes.remove(token));}
    private static String encode(List<Address> addresses) throws JSONException {
        JSONArray result=new JSONArray();if(addresses==null)return result.toString();
        for(Address address:addresses){JSONObject item=new JSONObject();
            item.put("name",address.getFeatureName());item.put("thoroughfare",address.getThoroughfare());item.put("subThoroughfare",address.getSubThoroughfare());
            item.put("locality",address.getLocality());item.put("subLocality",address.getSubLocality());item.put("administrativeArea",address.getAdminArea());
            item.put("subAdministrativeArea",address.getSubAdminArea());item.put("postalCode",address.getPostalCode());item.put("country",address.getCountryName());item.put("ISOcountryCode",address.getCountryCode());
            if(address.hasLatitude() && address.hasLongitude())item.put("coordinate",new JSONArray(new double[]{address.getLatitude(),address.getLongitude()}));
            result.put(item);
        }
        return result.toString();
    }
    void close(){
        closed=true;for(long token:new ArrayList<>(connections.keySet()))stop(token);geocodes.clear();permissionRequests.clear();
    }
}
