package local.ios.host;

import android.graphics.Rect;
import android.os.Build;
import android.view.View;
import android.view.WindowInsets;
import java.util.function.BooleanSupplier;

final class KeyboardState {
    interface Listener {
        String changed(boolean visible, int x, int y, int width, int height);
    }

    private final View root;
    private final BooleanSupplier ready;
    private final Listener listener;
    private final java.util.function.Consumer<String> failure;
    private final Rect visibleFrame = new Rect();
    private final int[] location = new int[2];
    private boolean initialized, visible;
    private int x, y, width, height;

    KeyboardState(View root, BooleanSupplier ready, Listener listener,
            java.util.function.Consumer<String> failure) {
        this.root = root; this.ready = ready; this.listener = listener; this.failure = failure;
        if (Build.VERSION.SDK_INT >= 30) {
            root.setOnApplyWindowInsetsListener((view, insets) -> {
                readInsets(insets);
                return insets;
            });
        } else {
            root.getViewTreeObserver().addOnGlobalLayoutListener(this::readVisibleFrame);
        }
        root.addOnLayoutChangeListener((view, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom) -> {
            if (right - left != oldRight - oldLeft || bottom - top != oldBottom - oldTop) refresh();
        });
    }

    void refresh() {
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsets insets = root.getRootWindowInsets();
            if (insets != null) readInsets(insets);
            root.requestApplyInsets();
        } else readVisibleFrame();
    }

    private void readInsets(WindowInsets insets) {
        int bottom = insets.getInsets(WindowInsets.Type.ime()).bottom;
        boolean shown = insets.isVisible(WindowInsets.Type.ime());

        report(shown, 0, Math.max(0, root.getHeight() - bottom), root.getWidth(), bottom);
    }

    private void readVisibleFrame() {
        root.getWindowVisibleDisplayFrame(visibleFrame);
        root.getLocationOnScreen(location);
        int bottom = Math.max(0, location[1] + root.getHeight() - visibleFrame.bottom);
        boolean shown = bottom > Math.round(100 * root.getResources().getDisplayMetrics().density);
        report(shown, 0, shown ? root.getHeight() - bottom : root.getHeight(),
                root.getWidth(), shown ? bottom : 0);
    }

    private void report(boolean shown, int nextX, int nextY, int nextWidth, int nextHeight) {
        if (!ready.getAsBoolean() || nextWidth <= 0 || root.getHeight() <= 0) return;
        if (initialized && shown == visible && nextX == x && nextY == y &&
                nextWidth == width && nextHeight == height) return;
        initialized = true; visible = shown;
        x = nextX; y = nextY; width = nextWidth; height = nextHeight;
        String error = listener.changed(shown, x, y, width, height);
        if (error != null) failure.accept(error);
    }
}
