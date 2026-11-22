package local.ios.host;

import android.os.Build;
import android.text.InputType;
import android.view.View;
import android.view.ViewTreeObserver;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;

final class TextInput implements View.OnAttachStateChangeListener,
        ViewTreeObserver.OnGlobalLayoutListener, ViewTreeObserver.OnWindowFocusChangeListener {
    private final EditText editor;
    private final Runnable show = this::showWhenReady;
    private ViewTreeObserver observer;
    private boolean pending;
    // DEVELOPMENT_BEGIN
    private int diagnosticSequence;
    private static final class ShowResult extends android.os.ResultReceiver {
        private final java.lang.ref.WeakReference<TextInput> owner;
        private final int sequence;
        ShowResult(TextInput ime, int sequence) {
            super(ime.editor.getHandler()); this.owner = new java.lang.ref.WeakReference<>(ime); this.sequence = sequence;
        }
        @Override protected void onReceiveResult(int code, android.os.Bundle data) {
            TextInput ime = owner.get();
            if (ime != null && sequence == ime.diagnosticSequence)
                android.util.Log.i("I2AGuest", "[DIAG-ime] request=" + sequence + " result=" + code +
                        " (0=alreadyShown,1=stillHidden,2=shown,3=hidden)");
        }
    }
    // DEVELOPMENT_END

    TextInput(EditText editor) {
        this.editor = editor;
        editor.setFocusableInTouchMode(true);
        editor.setShowSoftInputOnFocus(true);
        editor.addOnAttachStateChangeListener(this);
    }

    static int inputType(int keyboard, int capitals, boolean secure) {
        int type = InputType.TYPE_CLASS_TEXT;
        if (keyboard == 3) type |= InputType.TYPE_TEXT_VARIATION_URI;
        else if (keyboard == 7) type |= InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS;
        else if (keyboard == 4 || keyboard == 11) type = InputType.TYPE_CLASS_NUMBER;
        else if (keyboard == 5) type = InputType.TYPE_CLASS_PHONE;
        else if (keyboard == 8) type = InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_FLAG_DECIMAL;
        else if (keyboard == 6) type |= InputType.TYPE_TEXT_VARIATION_PERSON_NAME;
        else if (keyboard == 10) type |= InputType.TYPE_TEXT_VARIATION_WEB_EDIT_TEXT;
        if (secure) {
            if ((type & InputType.TYPE_MASK_CLASS) == InputType.TYPE_CLASS_NUMBER)
                return type | InputType.TYPE_NUMBER_VARIATION_PASSWORD;
            return InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD;
        }
         
        if ((type & InputType.TYPE_MASK_CLASS) == InputType.TYPE_CLASS_TEXT) {
            if (capitals == 1) type |= InputType.TYPE_TEXT_FLAG_CAP_WORDS;
            else if (capitals == 2) type |= InputType.TYPE_TEXT_FLAG_CAP_SENTENCES;
            else if (capitals == 3) type |= InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS;
        }
        return type;
    }

    static int options(int returnKey) {
        int action = returnKey == 1 ? EditorInfo.IME_ACTION_GO :
                returnKey == 4 ? EditorInfo.IME_ACTION_NEXT :
                returnKey == 6 ? EditorInfo.IME_ACTION_SEARCH :
                returnKey == 7 ? EditorInfo.IME_ACTION_SEND : EditorInfo.IME_ACTION_DONE;
        return action | EditorInfo.IME_FLAG_NO_FULLSCREEN | EditorInfo.IME_FLAG_NO_EXTRACT_UI;
    }

    void requestShow() {
        pending = true;
        // DEVELOPMENT_BEGIN
        ++diagnosticSequence;
        android.util.Log.i("I2AGuest", "[DIAG-ime] request=" + diagnosticSequence +
                " attached=" + editor.isAttachedToWindow() + " windowFocus=" + editor.hasWindowFocus());
        // DEVELOPMENT_END
        listen();
        editor.requestFocus();
        schedule();
    }

    void cancel() {
        pending = false;
        editor.removeCallbacks(show);
        unlisten();
        // DEVELOPMENT_BEGIN
        ++diagnosticSequence;
        // DEVELOPMENT_END
    }

    private void listen() {
        if (!pending || !editor.isAttachedToWindow()) return;
        ViewTreeObserver current = editor.getViewTreeObserver();
        if (observer == current) return;
        unlisten(); observer = current;
        observer.addOnGlobalLayoutListener(this);
        observer.addOnWindowFocusChangeListener(this);
    }

    private void unlisten() {
        if (observer != null && observer.isAlive()) {
            observer.removeOnGlobalLayoutListener(this);
            observer.removeOnWindowFocusChangeListener(this);
        }
        observer = null;
    }

    private void schedule() {
        editor.removeCallbacks(show);
        if (pending && editor.isAttachedToWindow()) editor.post(show);
    }

    private void showWhenReady() {
        if (!pending || !editor.isAttachedToWindow() || !editor.isLaidOut() ||
                editor.isLayoutRequested() || !editor.hasWindowFocus() || !editor.isShown() ||
                !editor.isEnabled()) return;
        if (!editor.hasFocus() && !editor.requestFocus()) return;
        pending = false; unlisten();
        InputMethodManager manager = (InputMethodManager) editor.getContext().getSystemService(android.content.Context.INPUT_METHOD_SERVICE);
        android.os.ResultReceiver result = null;
        // DEVELOPMENT_BEGIN
        final int sequence = diagnosticSequence;
        result = new ShowResult(this, sequence);
        // DEVELOPMENT_END

        boolean accepted = manager.showSoftInput(editor, 0, result);
        // DEVELOPMENT_BEGIN
        android.util.Log.i("I2AGuest", "[DIAG-ime] request=" + sequence + " accepted=" + accepted +
                " served=" + manager.isActive(editor) + " focus=" + editor.hasFocus() +
                " windowFocus=" + editor.hasWindowFocus() + " size=" + editor.getWidth() + "x" + editor.getHeight() +
                " options=0x" + Integer.toHexString(editor.getImeOptions()));
        if (Build.VERSION.SDK_INT >= 30) editor.postDelayed(() -> {
            if (sequence != diagnosticSequence || !editor.isAttachedToWindow()) return;
            android.view.WindowInsets insets = editor.getRootWindowInsets();
            android.util.Log.i("I2AGuest", "[DIAG-ime] request=" + sequence +
                    " visible=" + (insets != null && insets.isVisible(android.view.WindowInsets.Type.ime())));
        }, 500);
        // DEVELOPMENT_END
    }

    @Override public void onViewAttachedToWindow(View view) { listen(); schedule(); }
    @Override public void onViewDetachedFromWindow(View view) { cancel(); }
    @Override public void onGlobalLayout() { schedule(); }
    @Override public void onWindowFocusChanged(boolean focused) { if (focused) schedule(); }
}
