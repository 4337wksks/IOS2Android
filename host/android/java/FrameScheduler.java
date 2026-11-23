package local.ios.host;

import android.app.Activity;
import android.os.Handler;
import android.os.Looper;
import android.view.Choreographer;
import java.util.function.Consumer;

final class FrameScheduler implements Choreographer.FrameCallback {
    private final Activity activity;
    private final Consumer<String> failure;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private boolean enabled, foreground = true, pending,closed;
    private static native String frame(long timestamp, double refreshRate);

    FrameScheduler(Activity activity, Consumer<String> failure) {
        this.activity = activity;
        this.failure = failure;
    }

    void active(boolean value) { mainHandler.post(() -> { enabled = value; update(); }); }
    void foreground(boolean value) {
        if (Looper.myLooper() == Looper.getMainLooper()) { foreground = value; update(); }
        else mainHandler.post(() -> { foreground = value; update(); });
    }

    private void update() {
        if (!closed && enabled && foreground && !pending) {
            pending = true;
            Choreographer.getInstance().postFrameCallback(this);
        } else if ((closed || !enabled || !foreground) && pending) {
            Choreographer.getInstance().removeFrameCallback(this);
            pending = false;
        }
    }

    @Override public void doFrame(long timestamp) {
        pending = false;
        if (closed || !enabled || !foreground) return;
        double refresh = activity.getWindowManager().getDefaultDisplay().getRefreshRate();
        String error = frame(timestamp, refresh);
        if (error != null) { enabled = false; failure.accept(error); }
        update();
    }
    void close(){closed=true;enabled=false;update();mainHandler.removeCallbacksAndMessages(null);}
}
