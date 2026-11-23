package local.ios.host;

import android.content.Context;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Handler;
import android.os.HandlerThread;
import java.util.HashMap;
import java.util.function.Consumer;

final class MotionSource {
    interface Delivery { String sample(long token, int generation, double[] values); }
    private final SensorManager manager;
    private final Sensor rotation, accelerometer, gyroscope;
    private final Delivery delivery;
    private final Consumer<String> failure;
    private final HashMap<Long, Connection> connections = new HashMap<>();
    private HandlerThread thread;
    private Handler handler;
    private boolean closed;
    MotionSource(Context context, Delivery delivery, Consumer<String> failure) {
        manager = (SensorManager)context.getSystemService(Context.SENSOR_SERVICE);
        Sensor gameRotationSensor = manager == null ? null : manager.getDefaultSensor(Sensor.TYPE_GAME_ROTATION_VECTOR);
        rotation = gameRotationSensor != null ? gameRotationSensor : manager == null ? null : manager.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR);
        accelerometer = manager == null ? null : manager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
        gyroscope = manager == null ? null : manager.getDefaultSensor(Sensor.TYPE_GYROSCOPE);
        this.delivery = delivery; this.failure = failure;
    }
    boolean available() { return rotation != null && accelerometer != null && gyroscope != null; }
    synchronized boolean start(long token, int generation, double interval) {
        stop(token);
        if (closed || !available() || !Double.isFinite(interval) || interval <= 0) return false;
        if (thread == null) { thread = new HandlerThread("I2A Motion"); thread.start(); handler = new Handler(thread.getLooper()); }
        int period = (int)Math.max(5000, Math.min(1000000, interval * 1000000));
        Connection connection = new Connection(token, generation, (long)(interval * 1000000000));
        boolean registered=false;
        try{registered = manager.registerListener(connection, accelerometer, period, handler)
            && manager.registerListener(connection, gyroscope, period, handler)
            && manager.registerListener(connection, rotation, period, handler);}catch(RuntimeException unavailable){}
        if (!registered) { connection.active = false;try{manager.unregisterListener(connection);}catch(RuntimeException unavailable){}return false; }
        connections.put(token, connection); return true;
    }
    synchronized void stop(long token) {
        Connection previous = connections.remove(token);
        if (previous != null) { previous.active = false;try{manager.unregisterListener(previous);}catch(RuntimeException unavailable){} }
    }
    synchronized void close() {
        closed=true;
        for (Connection connection : connections.values()) { connection.active = false;try{manager.unregisterListener(connection);}catch(RuntimeException unavailable){} }
        connections.clear();
        if (thread != null) { thread.quitSafely(); thread = null; handler = null; }
    }
    private final class Connection implements SensorEventListener {
        final long token, interval;
        final int generation;
        volatile boolean active = true;
        boolean accelerationReady, rateReady, originReady;
        final double[] acceleration = new double[3], rate = new double[3];
        final float[] quaternion = new float[4];
        long last;
        double origin;
        Connection(long token, int generation, long interval) { this.token = token; this.generation = generation; this.interval = interval; }
        @Override public void onAccuracyChanged(Sensor sensor, int accuracy) {}
        @Override public void onSensorChanged(SensorEvent event) {
            if (!active) return;
            if (event.sensor == accelerometer) {
                for (int i = 0; i < 3; i++) acceleration[i] = event.values[i] / SensorManager.GRAVITY_EARTH;
                accelerationReady = true; return;
            }
            if (event.sensor == gyroscope) { for (int i = 0; i < 3; i++) rate[i] = event.values[i]; rateReady = true; return; }
            if (event.sensor != rotation || !accelerationReady || !rateReady || (last != 0 && event.timestamp-last < interval)) return;
            SensorManager.getQuaternionFromVector(quaternion, event.values);
            double w = quaternion[0], x = quaternion[1], y = quaternion[2], z = quaternion[3];
            double norm = Math.sqrt(w*w+x*x+y*y+z*z);
            if (!(norm > 0)) return;
            w /= norm; x /= norm; y /= norm; z /= norm;
             
            if (!originReady) { origin = Math.atan2(2*(w*z+x*y), 1-2*(y*y+z*z)); originReady = true; }
            double c = Math.cos(-origin*0.5), s = Math.sin(-origin*0.5);
            double qx = c*x-s*y, qy = c*y+s*x, qz = c*z+s*w, qw = c*w-s*z;
            double gx = -2*(qx*qz-qw*qy), gy = -2*(qy*qz+qw*qx), gz = -(1-2*(qx*qx+qy*qy));
            double[] values = {event.timestamp * 1e-9, qx, qy, qz, qw, gx, gy, gz,
                acceleration[0]+gx, acceleration[1]+gy, acceleration[2]+gz, rate[0], rate[1], rate[2]};
            last = event.timestamp;
            String error = delivery.sample(token, generation, values);
            if (error != null) { stop(token); failure.accept(error); }
        }
    }
}
