package local.ios.host;

import android.app.Activity;
import android.app.Dialog;
import android.content.res.ColorStateList;
import android.content.res.Configuration;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.os.Build;
import android.net.Uri;
import android.util.Log;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.Gravity;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;
import java.util.ArrayList;
import java.util.function.IntConsumer;

final class ModalPresenter extends Dialog {
    interface Edit { String replace(int field, String value); }
    private final Activity activity;
    private final ArrayList<Button> buttons = new ArrayList<>();
    private final int surface, foreground, muted, accent, fieldColor;
    private final boolean sheet;
    private final int cancelIndex;
    private final IntConsumer select;
    private final ScrollView contentScroll;
    private android.window.OnBackInvokedCallback back;
    private int dp(float value) { return Math.round(value * activity.getResources().getDisplayMetrics().density); }
    private static int theme(Activity activity,String name) {
        return activity.getResources().getIdentifier(name,"style",activity.getPackageName());
    }
    static final class Palette {
        final boolean dark;
        final int surface, foreground, muted, accent, field;
        Palette(Activity activity) {
            dark=(activity.getResources().getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK)==Configuration.UI_MODE_NIGHT_YES;
            surface=color(activity,dark ? "system_neutral1_900" : "system_neutral1_50",dark ? 0xff24242a : 0xfff8f5fc);
            foreground=color(activity,dark ? "system_neutral1_50" : "system_neutral1_900",dark ? 0xfff1edf5 : 0xff211e28);
            muted=color(activity,dark ? "system_neutral2_200" : "system_neutral2_700",dark ? 0xffc7c2cf : 0xff57515e);
            accent=color(activity,dark ? "system_accent1_200" : "system_accent1_600",dark ? 0xffd0bcff : 0xff6750a4);
            field=color(activity,dark ? "system_neutral2_800" : "system_neutral2_100",dark ? 0xff343039 : 0xffe8e2ec);
        }
        private static int color(Activity activity,String name,int fallback) {
            if (Build.VERSION.SDK_INT < 31) return fallback;
            int id=activity.getResources().getIdentifier(name,"color","android");
            return id==0 ? fallback : activity.getColor(id);
        }
    }
    private GradientDrawable shape(int color, int radius) {
        GradientDrawable result=new GradientDrawable(); result.setColor(color); result.setCornerRadius(dp(radius)); return result;
    }
    private TextView text(String value, float size, int color) {
        TextView view=new TextView(activity); view.setText(value); view.setTextSize(size); view.setTextColor(color);
        view.setFontFeatureSettings("kern"); view.setIncludeFontPadding(false); return view;
    }
    private LinearLayout.LayoutParams row(int top) {
        LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,-2); p.topMargin=dp(top); return p;
    }
    ModalPresenter(Activity activity, int style, int cancel, String title, String message, String[] actions, boolean[] enabled, int[] styles,
               String[] values, String[] hints, int[] options, Edit edit, IntConsumer select) {
        super(activity, theme(activity,"HostDialogTheme"));
        this.activity=activity;
        sheet=style==0; cancelIndex=cancel; this.select=select;
        Palette palette=new Palette(activity); boolean dark=palette.dark;
        surface=palette.surface; foreground=palette.foreground; muted=palette.muted; accent=palette.accent; fieldColor=palette.field;
        setCancelable(cancel>=0); setCanceledOnTouchOutside(sheet && cancel>=0);
        ScrollView scroll=new ScrollView(activity); scroll.setFillViewport(false); scroll.setClipToPadding(false);
        contentScroll=scroll;
        LinearLayout panel=new LinearLayout(activity); panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(dp(24),dp(24),dp(24),dp(24)); panel.setBackground(shape(surface,28));
        panel.setFocusableInTouchMode(true);
        if (title!=null && !title.isEmpty()) {
            TextView heading=text(title,24,foreground); heading.setTypeface(Typeface.create("sans-serif-medium",Typeface.NORMAL));
            heading.setAccessibilityHeading(true); panel.addView(heading,row(0));
        }
        if (message!=null && !message.isEmpty()) {
            TextView body=text(message,16,muted); body.setLineSpacing(dp(4),1); panel.addView(body,row(panel.getChildCount()==0 ? 0 : 16));
        }
        for (int index=0; index<values.length; ++index) {
            final int field=index;
            if (hints[index]!=null && !hints[index].isEmpty()) panel.addView(text(hints[index],12,muted),row(20));
            EditText input=new EditText(activity); input.setSingleLine(true); input.setTextSize(16); input.setTextColor(foreground); input.setHintTextColor(muted);
            TextInput ime=new TextInput(input);
            input.setMinHeight(dp(56)); input.setPadding(dp(16),dp(12),dp(16),dp(12));
            GradientDrawable background=shape(fieldColor,12); input.setBackground(background);
            input.setOnFocusChangeListener((v,focused)->{ background.setStroke(dp(focused ? 2 : 1),focused ? accent : fieldColor); });
            int type=TextInput.inputType(options[index*5],options[index*5+2],options[index*5+3]!=0);
            input.setInputType(type); input.setEnabled(options[index*5+4]!=0);
            int key=options[index*5+1];
            input.setImeOptions(TextInput.options(key));
            input.setOnClickListener(v -> ime.requestShow());
            input.setText(values[index]); input.setHint(hints[index]); input.setSelectAllOnFocus(true);
            input.addTextChangedListener(new TextWatcher() {
                boolean syncing;
                public void beforeTextChanged(CharSequence s,int start,int count,int after) {}
                public void onTextChanged(CharSequence s,int start,int before,int count) {}
                public void afterTextChanged(Editable value) {
                    if (syncing) return;
                    String accepted=edit.replace(field,value.toString());
                    if (accepted!=null && !accepted.contentEquals(value)) {
                        syncing=true; input.setText(accepted); input.setSelection(accepted.length()); syncing=false;
                    }
                }
            });
            panel.addView(input,row(8));
        }
        boolean inline=!sheet && actions.length<=2;
        LinearLayout actionList=new LinearLayout(activity); actionList.setOrientation(inline ? LinearLayout.HORIZONTAL : LinearLayout.VERTICAL); actionList.setGravity(Gravity.END);
        for (int index=0;index<actions.length;++index) {
            final int action=index; Button button=new Button(activity); button.setText(actions[index]); button.setAllCaps(false);
            button.setTextSize(14); button.setTypeface(Typeface.create("sans-serif-medium",Typeface.NORMAL));
            button.setMinHeight(dp(48)); button.setMinimumHeight(dp(48)); button.setPadding(dp(20),dp(8),dp(20),dp(8));
            boolean primary=actions.length==1;
            int color=styles[index]==2 ? (dark ? 0xffffb4ab : 0xffba1a1a) : accent;
            int ink=primary ? surface : color;
            button.setTextColor(new ColorStateList(new int[][]{new int[]{android.R.attr.state_enabled},new int[]{}},new int[]{ink,muted}));
            GradientDrawable fill=shape(primary ? color : fieldColor,24);
            button.setBackground(new RippleDrawable(ColorStateList.valueOf((foreground&0xffffff)|0x24000000),fill,null));
            button.setStateListAnimator(null); button.setElevation(0); button.setEnabled(enabled[index]);
            button.setOnClickListener(v->{ if (isShowing()) select.accept(action); });
            LinearLayout.LayoutParams layout=inline ? new LinearLayout.LayoutParams(-2,-2) : row(index==0 ? 0 : 8);
            if(inline && index>0) layout.setMarginStart(dp(8));
            buttons.add(button); actionList.addView(button,layout);
        }
        if (actions.length!=0) panel.addView(actionList,row(24));
        scroll.addView(panel); setContentView(scroll);
        Window window=getWindow(); window.setBackgroundDrawableResource(android.R.color.transparent);
        window.addFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND); window.setDimAmount(0.45f);
        window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
        window.setGravity(sheet ? Gravity.BOTTOM|Gravity.CENTER_HORIZONTAL : Gravity.CENTER);
        if (Build.VERSION.SDK_INT>=30) {
            scroll.setOnApplyWindowInsetsListener((view,insets)-> {
                android.graphics.Insets safe=insets.getInsets(WindowInsets.Type.systemBars()|WindowInsets.Type.displayCutout()|WindowInsets.Type.ime());
                layoutWindow(safe.left,safe.top,safe.right,safe.bottom); return insets;
            });
        } else scroll.setFitsSystemWindows(true);
        panel.requestFocus();
    }
    @Override public void show() {
        super.show();
        Window window=getWindow();
        if (Build.VERSION.SDK_INT>=30) {
            android.graphics.Insets safe=activity.getWindowManager().getCurrentWindowMetrics().getWindowInsets()
                .getInsets(WindowInsets.Type.systemBars()|WindowInsets.Type.displayCutout()|WindowInsets.Type.ime());
            layoutWindow(safe.left,safe.top,safe.right,safe.bottom);
            WindowInsetsController controller=window.getInsetsController();
            if (controller!=null) {
                controller.show(WindowInsets.Type.systemBars());
                int mask=WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS|WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS;
                boolean dark=(activity.getResources().getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK)==Configuration.UI_MODE_NIGHT_YES;
                controller.setSystemBarsAppearance(dark ? 0 : mask,mask);
            }
            window.getDecorView().requestApplyInsets();
        } else layoutWindow(0,0,0,0);
        if (Build.VERSION.SDK_INT>=33 && cancelIndex>=0 && back==null) {
            back=this::cancel; window.getOnBackInvokedDispatcher().registerOnBackInvokedCallback(android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT,back);
        }
    }
    private void layoutWindow(int left,int top,int right,int bottom) {
        Window window=getWindow();
        android.graphics.Rect bounds=Build.VERSION.SDK_INT>=30 ? activity.getWindowManager().getCurrentWindowMetrics().getBounds() :
            new android.graphics.Rect(0,0,activity.getResources().getDisplayMetrics().widthPixels,activity.getResources().getDisplayMetrics().heightPixels);
        int width=Math.min(dp(sheet ? 640 : 560),Math.max(dp(48),bounds.width()-left-right-dp(sheet ? 32 : 48)));
        int maxHeight=Math.max(dp(48),bounds.height()-top-bottom-dp(48));
        contentScroll.measure(View.MeasureSpec.makeMeasureSpec(width,View.MeasureSpec.EXACTLY),View.MeasureSpec.makeMeasureSpec(maxHeight,View.MeasureSpec.AT_MOST));
        WindowManager.LayoutParams attributes=window.getAttributes();
        int height=contentScroll.getMeasuredHeight(),offset=sheet ? bottom+dp(16) : 0;
        if(attributes.width!=width || attributes.height!=height || attributes.y!=offset) {
            attributes.width=width; attributes.height=height; attributes.y=offset; window.setAttributes(attributes);
        }
    }
    @Override public void cancel() { if (cancelIndex>=0 && cancelIndex<buttons.size() && buttons.get(cancelIndex).isEnabled() && isShowing()) select.accept(-2); }
    @Override public void onBackPressed() { cancel(); }
    @Override public void dismiss() {
        if (Build.VERSION.SDK_INT>=33 && back!=null) { getWindow().getOnBackInvokedDispatcher().unregisterOnBackInvokedCallback(back); back=null; }
        super.dismiss();
    }
    void actionEnabled(int index, boolean enabled) { if (index>=0 && index<buttons.size()) buttons.get(index).setEnabled(enabled); }

    static final class WebPanel extends Dialog {
        private WebView web;
        private final LinearLayout content;
        private final Palette palette;
        private final ProgressBar progress;
        private final TextView failure;
        private android.window.OnBackInvokedCallback back;
        private boolean failed;
        private int dp(float value) { return Math.round(value*getContext().getResources().getDisplayMetrics().density); }
        WebPanel(Activity activity,String title) {
            super(activity,theme(activity,"HostPanelTheme")); palette=new Palette(activity);
            content=new LinearLayout(getContext()); content.setOrientation(LinearLayout.VERTICAL); content.setBackgroundColor(palette.surface);
            LinearLayout toolbar=new LinearLayout(getContext()); toolbar.setGravity(Gravity.CENTER_VERTICAL); toolbar.setPadding(dp(8),dp(4),dp(16),dp(4));
            ImageButton close=new ImageButton(getContext()); close.setContentDescription("Back"); close.setTooltipText("Back");
            close.setImageResource(activity.getResources().getIdentifier("back","drawable",activity.getPackageName()));
            close.setImageTintList(ColorStateList.valueOf(palette.foreground)); close.setPadding(dp(12),dp(12),dp(12),dp(12));
            GradientDrawable mask=new GradientDrawable(); mask.setColor(palette.surface); mask.setCornerRadius(dp(24));
            close.setBackground(new RippleDrawable(ColorStateList.valueOf((palette.foreground&0xffffff)|0x24000000),null,mask));
            close.setOnClickListener(view->navigateBack()); toolbar.addView(close,new LinearLayout.LayoutParams(dp(48),dp(48)));
            TextView heading=new TextView(getContext()); heading.setText(title); heading.setTextColor(palette.foreground); heading.setTextSize(22);
            heading.setTypeface(Typeface.create("sans-serif-medium",Typeface.NORMAL)); heading.setAccessibilityHeading(true);
            LinearLayout.LayoutParams headingLayout=new LinearLayout.LayoutParams(-1,-2); headingLayout.setMarginStart(dp(16)); toolbar.addView(heading,headingLayout);
            content.addView(toolbar,new LinearLayout.LayoutParams(-1,dp(64)));
            progress=new ProgressBar(getContext(),null,android.R.attr.progressBarStyleHorizontal); progress.setProgressTintList(ColorStateList.valueOf(palette.accent));
            progress.setProgressBackgroundTintList(ColorStateList.valueOf(palette.field)); content.addView(progress,new LinearLayout.LayoutParams(-1,dp(3)));
            failure=new TextView(getContext()); failure.setTextColor(palette.foreground); failure.setTextSize(16); failure.setGravity(Gravity.CENTER);
            failure.setPadding(dp(24),dp(24),dp(24),dp(24)); failure.setVisibility(View.GONE); failure.setFocusable(true);
            failure.setOnClickListener(view->{ if(web!=null) web.reload(); }); content.addView(failure,new LinearLayout.LayoutParams(-1,0,1));
            web=new WebView(getContext()); web.setBackgroundColor(palette.surface);
            web.getSettings().setJavaScriptEnabled(true); web.getSettings().setDomStorageEnabled(true);
            web.getSettings().setAllowFileAccess(false); web.getSettings().setAllowContentAccess(false);
            if(Build.VERSION.SDK_INT>=33) web.getSettings().setAlgorithmicDarkeningAllowed(palette.dark);
            web.setWebChromeClient(new WebChromeClient() {
                @Override public void onProgressChanged(WebView view,int value) {
                    progress.setProgress(value); progress.setVisibility(value==100 || failed ? View.GONE : View.VISIBLE);
                }
            });
            web.setWebViewClient(new WebViewClient() {
                @Override public boolean shouldOverrideUrlLoading(WebView view,WebResourceRequest request) {
                    Uri uri=request.getUrl();
                    if("https".equalsIgnoreCase(uri.getScheme())) return false;
                    if("mailto".equalsIgnoreCase(uri.getScheme()) || "tel".equalsIgnoreCase(uri.getScheme())) {
                        try { activity.startActivity(new android.content.Intent(android.content.Intent.ACTION_VIEW,uri)); } catch(RuntimeException ignored) {}
                    }
                    return true;
                }
                @Override public void onPageStarted(WebView view,String url,android.graphics.Bitmap icon) {
                    failed=false; failure.setVisibility(View.GONE); view.setVisibility(View.VISIBLE); progress.setVisibility(View.VISIBLE);
                }
                @Override public void onPageFinished(WebView view,String url) {
                    if(!failed) Log.i("I2AHost","Web panel page loaded");
                }
                @Override public void onReceivedError(WebView view,WebResourceRequest request,WebResourceError error) {
                    if(request.isForMainFrame()) unavailable("Unable to load this page. Check your connection and tap to retry.",error.getErrorCode());
                }
                @Override public void onReceivedHttpError(WebView view,WebResourceRequest request,WebResourceResponse response) {
                    if(request.isForMainFrame()) unavailable("Service unavailable. Tap to retry.",response.getStatusCode());
                }
            });
            content.addView(web,new LinearLayout.LayoutParams(-1,0,1)); setContentView(content);
            Window window=getWindow(); window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
            if(Build.VERSION.SDK_INT>=30) {
                window.setDecorFitsSystemWindows(false);
                content.setOnApplyWindowInsetsListener((view,insets)-> {
                    android.graphics.Insets bars=insets.getInsets(WindowInsets.Type.systemBars()|WindowInsets.Type.displayCutout());
                    android.graphics.Insets keyboard=insets.getInsets(WindowInsets.Type.ime());
                    view.setPadding(bars.left,bars.top,bars.right,Math.max(bars.bottom,keyboard.bottom)); return insets;
                });
            } else content.setFitsSystemWindows(true);
        }
        private void unavailable(String message,int code) {
            if(web==null) return;
            failed=true; progress.setVisibility(View.GONE); web.setVisibility(View.GONE);
            failure.setText(message); failure.setVisibility(View.VISIBLE); Log.w("I2AHost","Web panel load failed: "+code);
        }
        void load(Uri uri) { web.loadUrl(uri.toString()); }
        private void navigateBack() { if(web!=null && web.canGoBack()) web.goBack(); else dismiss(); }
        @Override public void onBackPressed() { navigateBack(); }
        @Override public void show() {
            super.show(); Window window=getWindow(); window.setLayout(-1,-1);
            if(Build.VERSION.SDK_INT>=30) {
                WindowInsetsController controller=window.getInsetsController();
                if(controller!=null) {
                    controller.show(WindowInsets.Type.systemBars());
                    int mask=WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS|WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS;
                    controller.setSystemBarsAppearance(palette.dark ? 0 : mask,mask);
                }
                content.requestApplyInsets();
            }
            if(Build.VERSION.SDK_INT>=33 && back==null) {
                back=this::navigateBack; window.getOnBackInvokedDispatcher().registerOnBackInvokedCallback(android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT,back);
            }
        }
        @Override public void dismiss() {
            if(Build.VERSION.SDK_INT>=33 && back!=null) { getWindow().getOnBackInvokedDispatcher().unregisterOnBackInvokedCallback(back); back=null; }
            if(web!=null) { web.stopLoading(); content.removeView(web); web.destroy(); web=null; }
            super.dismiss();
        }
    }
}
