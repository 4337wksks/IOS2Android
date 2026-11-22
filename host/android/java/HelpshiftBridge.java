package local.ios.host;

import android.app.Activity;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;
import org.json.JSONArray;
import org.json.JSONObject;

final class HelpshiftBridge {
    interface Events { void deliver(String event, String data); }
    private final Activity activity;
    private final Handler mainHandler;
    private final Events events;
    private final JSONObject settings;
    private final Map<String,Object> user = new HashMap<>();
    private final Map<String,Method> sdkMethods = new HashMap<>();
    private String domain = "", language = "en";
    private Class<?> sdkClass;
    private boolean sdkReady, closed, visible;
    private ModalPresenter.WebPanel dialog;
    private ModalPresenter notice;

    HelpshiftBridge(Activity activity, Handler handler, Events events) throws Exception {
        this.activity=activity; this.mainHandler=handler; this.events=events;
        settings=new JSONObject(BuildProfile.HELPSHIFT_CONFIG);
    }
    private void emit(String event, JSONObject data) {
        if (closed) return;
        if (Looper.myLooper()!=Looper.getMainLooper()) { mainHandler.post(()->emit(event,data)); return; }
        events.deliver(event,data==null ? "{}" : data.toString());
    }
    private void session(boolean shown) {
        if (visible==shown) return;
        visible=shown; emit(shown ? "session_started" : "session_ended",null);
    }
    private void unavailable(String message) {
        Log.w("I2AHost","Helpshift: "+message);
        if (!closed && !activity.isDestroyed() && !activity.isFinishing()) {
            if(notice!=null) notice.dismiss();
            notice=new ModalPresenter(activity,1,0,"Support unavailable",message,new String[]{"Back"},new boolean[]{true},new int[]{1},
                new String[0],new String[0],new int[0],(field,value)->value,action->{ if(notice!=null) notice.dismiss(); });
            notice.setOnDismissListener(pane->{ notice=null; session(false); }); notice.show();
        }
    }
    private static Object value(Object input) throws Exception {
        if (input==JSONObject.NULL) return null;
        if (input instanceof JSONObject) return map((JSONObject)input);
        if (input instanceof JSONArray) {
            java.util.ArrayList<Object> result=new java.util.ArrayList<>(); JSONArray array=(JSONArray)input;
            for(int i=0;i<array.length();i++) result.add(value(array.get(i)));
            return result;
        }
        return input;
    }
    private static Map<String,Object> map(JSONObject object) throws Exception {
        Map<String,Object> result=new HashMap<>();
        if (object!=null) for(Iterator<String> it=object.keys();it.hasNext();) { String key=it.next(); result.put(key,value(object.get(key))); }
        return result;
    }
    private static String normalizeDomain(String raw) {
        if (raw==null || raw.isEmpty()) return "";
        Uri uri=Uri.parse(raw.contains("://") ? raw : "https://"+(raw.contains(".") ? raw : raw+".helpshift.com"));
        if (!"https".equalsIgnoreCase(uri.getScheme()) || uri.getHost()==null || uri.getUserInfo()!=null || uri.getPort()!=-1 ||
            (uri.getPath()!=null && !uri.getPath().isEmpty() && !"/".equals(uri.getPath())) || uri.getQuery()!=null || uri.getFragment()!=null)
            throw new IllegalArgumentException("Invalid Helpshift domain");
        return uri.getHost();
    }
    private Object call(String name, Object... arguments) throws Exception {
        Method method=sdkMethods.get(name);
        if (method==null) throw new UnsupportedOperationException("SDK interface unavailable: "+name);
        return method.invoke(null,arguments);
    }
    private void prepareSdk(JSONObject original) throws Exception {
        if (sdkReady) return;
        String platform=settings.optString("platform_id");
        String backend=settings.optString("backend","auto");
        if ("web".equals(backend) || "disabled".equals(backend)) return;
        if (platform.isEmpty() || !BuildProfile.HELPSHIFT_SDK) {
            if ("sdk".equals(backend)) throw new IllegalStateException("Android Helpshift configuration is incomplete");
            return;
        }
        sdkClass=Class.forName("com.helpshift.Helpshift");
        for(Method method:sdkClass.getMethods()) if (java.lang.reflect.Modifier.isStatic(method.getModifiers())) sdkMethods.put(method.getName(),method);
        Map<String,Object> config=map(original.optJSONObject("config")); config.putAll(map(settings.optJSONObject("config")));
        call("install",activity.getApplication(),platform,domain,config);
        Class<?> listener=Class.forName("com.helpshift.HelpshiftEventsListener");
        Class<?> types=Class.forName("com.helpshift.HelpshiftEvent");
        String started=(String)types.getField("SDK_SESSION_STARTED").get(null);
        String ended=(String)types.getField("SDK_SESSION_ENDED").get(null);
        String unread=(String)types.getField("RECEIVED_UNREAD_MESSAGE_COUNT").get(null);
        String count=(String)types.getField("DATA_MESSAGE_COUNT").get(null);
        Object delegate=Proxy.newProxyInstance(listener.getClassLoader(),new Class<?>[]{listener},(proxy,method,args)-> {
            if ("onEventOccurred".equals(method.getName()) && args!=null) {
                String name=(String)args[0]; Map<?,?> data=(Map<?,?>)args[1];
                if (started.equals(name)) mainHandler.post(()->session(true));
                else if (ended.equals(name)) mainHandler.post(()->session(false));
                else if (unread.equals(name)) {
                    JSONObject payload=new JSONObject(); payload.put("count",data==null ? 0 : data.get(count)); emit("unread",payload);
                }
            } else if ("onUserAuthenticationFailure".equals(method.getName())) {
                Log.w("I2AHost","Helpshift user authentication failed"); emit("authentication_failed",null);
            } else if ("hashCode".equals(method.getName())) return System.identityHashCode(proxy);
            else if ("equals".equals(method.getName())) return proxy==args[0];
            else if ("toString".equals(method.getName())) return "HelpshiftBridge.Events";
            return null;
        });
        call("setHelpshiftEventsListener",delegate); sdkReady=true;
        if (!"en".equals(language)) call("setLanguage",language);
        if (!user.isEmpty()) call("login",new HashMap<>(user));
    }
    private void updateUser(JSONObject input) throws Exception {
        for (String name:new String[]{"name","email"}) if (input.has(name)) user.put(name,input.getString(name));
        if (input.has("identifier")) user.put("userId",input.getString("identifier"));
        if (sdkReady && Boolean.FALSE.equals(call("login",new HashMap<>(user)))) emit("authentication_failed",null);
    }
    void command(String operation, String json) {
        if (closed || activity.isDestroyed()) return;
        try {
            JSONObject input=new JSONObject(json);
            if ("install".equals(operation)) {
                domain=normalizeDomain(settings.optString("domain",input.optString("domain")));
                prepareSdk(input); Log.i("I2AHost","Helpshift initialized: "+(sdkReady ? "sdk" : "web")); return;
            }
            if ("user".equals(operation) || "login".equals(operation)) { updateUser(input); return; }
            if ("logout".equals(operation)) { user.clear(); if(sdkReady) call("logout"); return; }
            if ("language".equals(operation)) { language=input.optString("language","en"); if(sdkReady) call("setLanguage",language); return; }
            if ("ios_push_token".equals(operation)) { emit("unsupported_push_token",null); return; }
            boolean show="conversation".equals(operation) || "faqs".equals(operation) || "faq".equals(operation) || "section".equals(operation);
            if (show) {
                if ("disabled".equals(settings.optString("backend"))) { unavailable("Support is disabled."); return; }
                if (domain.isEmpty()) { unavailable("The support service is not configured."); return; }
                if (sdkReady) {
                    Map<String,Object> config=map(input.optJSONObject("config"));
                    if (input.has("metadata")) config.put("customMetadata",map(input.getJSONObject("metadata")));
                    if ("faq".equals(operation) || "section".equals(operation)) call("faq".equals(operation) ? "showSingleFAQ" : "showFAQSection",activity,input.getString("publish_id"),config);
                    else call("conversation".equals(operation) ? "showConversation" : "showFAQs",activity,config);
                } else openWeb(operation,input.optString("publish_id"));
                return;
            }
            if (!sdkReady) return;
            if ("unread".equals(operation)) call("requestUnreadMessageCount",input.optBoolean("remote"));
            else if ("breadcrumb".equals(operation)) call("leaveBreadCrumb",input.optString("value"));
            else if ("clear_breadcrumbs".equals(operation)) call("clearBreadCrumbs");
            else if ("pause_notifications".equals(operation)) call("shouldPauseInAppNotification",input.optBoolean("paused"));
            else if ("push".equals(operation)) call("handlePush",map(input.optJSONObject("data")));
            else if ("push_token".equals(operation)) call("registerPushToken",input.optString("token"));
        } catch (Exception | LinkageError failure) {
            Log.w("I2AHost","Helpshift "+operation+" failed: "+failure.getClass().getSimpleName());
            emit("error",null);
            if ("conversation".equals(operation) || "faqs".equals(operation) || "faq".equals(operation) || "section".equals(operation)) unavailable("Support could not be opened. Please try again later.");
        }
    }
    private void openWeb(String operation,String identifier) {
        String locale=language.replace('_','-').toLowerCase(java.util.Locale.ROOT);
        if (!locale.matches("[a-z]{2,3}(?:-[a-z0-9]{2,8})*")) locale="en";
        Uri.Builder uri=new Uri.Builder().scheme("https").authority(domain).appendPath("hc").appendPath(locale);
        if ("faq".equals(operation) && !identifier.isEmpty()) uri.appendPath("faq").appendPath(identifier);
        else if ("section".equals(operation) && !identifier.isEmpty()) uri.appendPath("sections").appendPath(identifier);
        else if ("conversation".equals(operation)) uri.appendPath("contact-us");
        if(dialog==null) {
            dialog=new ModalPresenter.WebPanel(activity,"Help & Support");
            dialog.setOnDismissListener(pane->{ dialog=null; session(false); });
            dialog.show(); session(true);
        }
        dialog.load(uri.build());
        Log.i("I2AHost","Helpshift web view opened: "+operation);
    }
    void foreground(boolean active) {
        if (sdkReady && !closed) try { call(active ? "onAppForeground" : "onAppBackground"); } catch (Exception ignored) {}
    }
    void close() {
        if(notice!=null) notice.dismiss();
        if (dialog!=null) dialog.dismiss();
        if (sdkReady) try { call("setHelpshiftEventsListener",(Object)null); } catch (Exception ignored) {}
        closed=true; sdkMethods.clear(); user.clear();
    }
}
