package local.ios.host;

import android.app.AlarmManager;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Build;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;
import java.lang.ref.WeakReference;
import java.util.Calendar;
import java.util.Map;

public final class NotificationScheduler {
    interface Delivery { boolean receive(String payload, boolean response); }
    private static WeakReference<Delivery> delivery = new WeakReference<>(null);
    private static final String STORE = "i2a.local-notifications";
    private static final String EXTRA = "i2a.notification";
    private static final Object lock = new Object();

    static void attach(Delivery callback) { delivery = new WeakReference<>(callback); }
    static void detach(Delivery callback) { if (delivery.get() == callback) delivery.clear(); }
    static boolean enabled(Context context) {
        return manager(context).areNotificationsEnabled() && (Build.VERSION.SDK_INT < 33 ||
            context.checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS) == android.content.pm.PackageManager.PERMISSION_GRANTED);
    }
    static int authorization(Context context) {
        if (enabled(context)) return 2;
        return preferences(context).getBoolean("requested", false) ? 1 : 0;
    }
    static void requested(Context context) { preferences(context).edit().putBoolean("requested", true).apply(); }
    private static SharedPreferences preferences(Context context) { return context.getSharedPreferences(STORE, Context.MODE_PRIVATE); }
    private static NotificationManager manager(Context context) { return (NotificationManager)context.getSystemService(Context.NOTIFICATION_SERVICE); }
    private static PendingIntent alarm(Context context, String identifier) {
        Intent intent = new Intent(context, Receiver.class).setAction("local.ios.host.NOTIFY")
            .setData(new Uri.Builder().scheme("i2a-notification").authority(context.getPackageName()).appendPath(identifier).build());
        return PendingIntent.getBroadcast(context, 0, intent, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
    }
    private static void arm(Context context, JSONObject payload) throws JSONException {
        AlarmManager alarms = (AlarmManager)context.getSystemService(Context.ALARM_SERVICE);
        alarms.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, Math.max(System.currentTimeMillis(), payload.getLong("fire")), alarm(context, payload.getString("id")));
    }
    static String schedule(Context context, String text) {
        synchronized (lock) {
            try {
                JSONObject payload = new JSONObject(text);
                String identifier = payload.getString("id");
                if (identifier.isEmpty()) return "A notification identifier is required.";
                if (!enabled(context)) return "Notifications are not authorized.";
                if (payload.optLong("repeat") < 0) return "The notification repeat interval is invalid.";
                String key = "pending:" + identifier;
                SharedPreferences store = preferences(context);
                String previous = store.getString(key, null);
                if (!store.edit().putString(key, text).commit()) return "Unable to save the notification.";
                try { arm(context, payload); }
                catch (RuntimeException | JSONException failure) {
                    SharedPreferences.Editor editor = store.edit();
                    if (previous == null) editor.remove(key); else editor.putString(key, previous);
                    editor.commit();
                    return "Unable to schedule the notification: " + failure.getClass().getSimpleName();
                }
                return null;
            } catch (JSONException failure) { return "Invalid notification data."; }
        }
    }
    private static long nextDate(JSONObject payload, long now) throws JSONException {
        long interval = payload.optLong("repeat");
        int unit = payload.optInt("calendarUnit");
        long fire = payload.getLong("fire");
        if (unit != 0) {
            Calendar calendar = Calendar.getInstance(java.util.TimeZone.getTimeZone(payload.optString("timeZone", java.util.TimeZone.getDefault().getID())));
            calendar.setTimeInMillis(fire);
            int field = unit == 4 ? Calendar.YEAR : unit == 8 ? Calendar.MONTH : unit == 16 ? Calendar.DAY_OF_MONTH :
                unit == 32 ? Calendar.HOUR_OF_DAY : unit == 64 ? Calendar.MINUTE : unit == 128 ? Calendar.SECOND : Calendar.WEEK_OF_YEAR;
            Calendar current = (Calendar)calendar.clone(); current.setTimeInMillis(now);
            long steps;
            if (field == Calendar.YEAR) steps = Math.max(1, current.get(Calendar.YEAR) - calendar.get(Calendar.YEAR));
            else if (field == Calendar.MONTH) steps = Math.max(1, (long)(current.get(Calendar.YEAR) - calendar.get(Calendar.YEAR))*12 + current.get(Calendar.MONTH) - calendar.get(Calendar.MONTH));
            else { long period = field == Calendar.SECOND ? 1000 : field == Calendar.MINUTE ? 60000 : field == Calendar.HOUR_OF_DAY ? 3600000 : field == Calendar.DAY_OF_MONTH ? 86400000 : 604800000; steps = Math.max(1, Math.max(0, now-fire)/period); }
            if (steps > Integer.MAX_VALUE) return 0;
            calendar.add(field, (int)steps);
            while (calendar.getTimeInMillis() <= now) calendar.add(field, 1);
            return calendar.getTimeInMillis();
        }
        if (interval <= 0) return 0;
        long elapsed = Math.max(0, now - fire), steps = elapsed / interval + 1;
        return steps > (Long.MAX_VALUE - fire) / interval ? 0 : fire + steps * interval;
    }
    static void show(Context context, String text, long options) {
        if ((options & (2 | 4 | 8 | 16)) == 0 || !enabled(context)) return;
        try {
            JSONObject payload = new JSONObject(text);
            String identifier = payload.getString("id"), sound = payload.optString("sound");
            String channelId = sound.isEmpty() || (options & 2) == 0 ? "i2a.local.silent" : "i2a.local.default";
            NotificationChannel channel = new NotificationChannel(channelId, "Local notifications", NotificationManager.IMPORTANCE_DEFAULT);
            if (channelId.endsWith("silent")) channel.setSound(null, null);
            else if (!sound.equals("default")) {
                int resource = context.getResources().getIdentifier(sound.replaceFirst("\\.[^.]+$", "").toLowerCase(java.util.Locale.ROOT), "raw", context.getPackageName());
                if (resource != 0) {
                    channelId = "i2a.local.sound." + resource;
                    channel = new NotificationChannel(channelId, "Local notifications", NotificationManager.IMPORTANCE_DEFAULT);
                    channel.setSound(Uri.parse("android.resource://" + context.getPackageName() + "/" + resource), new android.media.AudioAttributes.Builder().setUsage(android.media.AudioAttributes.USAGE_NOTIFICATION).build());
                }
            }
            manager(context).createNotificationChannel(channel);
            Intent launch = new Intent(context, HostActivity.class).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP | Intent.FLAG_ACTIVITY_CLEAR_TOP)
                .setData(new Uri.Builder().scheme("i2a-notification-response").authority(context.getPackageName()).appendPath(identifier).build()).putExtra(EXTRA, text);
            PendingIntent click = PendingIntent.getActivity(context, 0, launch, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
            int icon = context.getApplicationInfo().icon;
            Notification.Builder builder = new Notification.Builder(context, channelId).setSmallIcon(icon)
                .setContentTitle(payload.optString("title", context.getApplicationInfo().loadLabel(context.getPackageManager()).toString()))
                .setContentText(payload.optString("body")).setStyle(new Notification.BigTextStyle().bigText(payload.optString("body")))
                .setContentIntent(click).setAutoCancel(true).setSubText(payload.optString("subtitle"));
            if (!payload.isNull("badge")) builder.setNumber(Math.max(0, payload.optInt("badge")));
            if (!payload.optString("thread").isEmpty()) builder.setGroup(payload.optString("thread"));
            manager(context).notify("i2a.local:" + identifier, 0, builder.build());
            preferences(context).edit().putString("delivered:" + identifier, text).apply();
        } catch (JSONException | RuntimeException failure) { android.util.Log.w("I2AHost", "Local notification failed: " + failure.getClass().getSimpleName()); }
    }
    static String takeResponse(Context context, Intent intent) {
        String payload = intent.getStringExtra(EXTRA);
        if (payload != null) {
            intent.removeExtra(EXTRA);
            try { String identifier = new JSONObject(payload).getString("id"); manager(context).cancel("i2a.local:" + identifier, 0); preferences(context).edit().remove("delivered:" + identifier).apply(); }
            catch (JSONException ignored) {}
        }
        return payload;
    }
    static String query(Context context, boolean delivered) {
        JSONArray result = new JSONArray();
        synchronized (lock) {
            String prefix = delivered ? "delivered:" : "pending:";
            java.util.HashSet<String> visible = new java.util.HashSet<>();
            if (delivered) for (android.service.notification.StatusBarNotification item : manager(context).getActiveNotifications()) visible.add(item.getTag());
            for (Map.Entry<String, ?> entry : preferences(context).getAll().entrySet()) if (entry.getKey().startsWith(prefix)) {
                if (delivered && !visible.contains("i2a.local:" + entry.getKey().substring(prefix.length()))) { preferences(context).edit().remove(entry.getKey()).apply(); continue; }
                try { result.put(new JSONObject((String)entry.getValue())); } catch (JSONException ignored) {}
            }
        }
        return result.toString();
    }
    static void remove(Context context, String identifiers, boolean delivered) {
        synchronized (lock) {
            try {
                JSONArray list = identifiers == null ? null : new JSONArray(identifiers);
                String prefix = delivered ? "delivered:" : "pending:";
                java.util.ArrayList<String> names = new java.util.ArrayList<>();
                if (list == null) for (String key : preferences(context).getAll().keySet()) { if (key.startsWith(prefix)) names.add(key.substring(prefix.length())); }
                else for (int i = 0; i < list.length(); i++) names.add(list.getString(i));
                SharedPreferences.Editor editor = preferences(context).edit();
                for (String identifier : names) {
                    if (delivered) manager(context).cancel("i2a.local:" + identifier, 0);
                    else { PendingIntent pending = alarm(context, identifier); ((AlarmManager)context.getSystemService(Context.ALARM_SERVICE)).cancel(pending); pending.cancel(); }
                    editor.remove(prefix + identifier);
                }
                editor.commit();
            } catch (JSONException failure) { android.util.Log.w("I2AHost", "Invalid notification identifiers."); }
        }
    }
    public static final class Receiver extends BroadcastReceiver {
        @Override public void onReceive(Context context, Intent intent) {
            synchronized (lock) {
                SharedPreferences store = preferences(context);
                if (Intent.ACTION_BOOT_COMPLETED.equals(intent.getAction()) || Intent.ACTION_MY_PACKAGE_REPLACED.equals(intent.getAction())) {
                    for (Map.Entry<String, ?> entry : store.getAll().entrySet()) if (entry.getKey().startsWith("pending:")) {
                        try { arm(context, new JSONObject((String)entry.getValue())); } catch (JSONException | RuntimeException failure) { android.util.Log.w("I2AHost", "Unable to restore a local notification."); }
                    }
                    return;
                }
                if (intent.getData() == null) return;
                String identifier = intent.getData().getLastPathSegment(), key = "pending:" + identifier;
                String text = store.getString(key, null);
                if (text == null) return;
                try {
                    JSONObject payload = new JSONObject(text); long now = System.currentTimeMillis();
                    if (payload.getLong("fire") > now + 1000) { arm(context, payload); return; }
                    long next = nextDate(payload, now);
                    if (next == 0) store.edit().remove(key).commit();
                    else { payload.put("fire", next); store.edit().putString(key, payload.toString()).commit(); arm(context, payload); }
                    Delivery callback = delivery.get();
                    if (callback == null || !callback.receive(text, false)) show(context, text, 2 | 4);
                } catch (JSONException | RuntimeException failure) { android.util.Log.w("I2AHost", "Local notification delivery failed: " + failure.getClass().getSimpleName()); }
            }
        }
    }
}
